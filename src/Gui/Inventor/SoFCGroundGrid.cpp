// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileNotice: Part of the FreeCAD project.

/******************************************************************************
 *                                                                            *
 *   FreeCAD is free software: you can redistribute it and/or modify          *
 *   it under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the     *
 *   License, or (at your option) any later version.                          *
 *                                                                            *
 *   FreeCAD is distributed in the hope that it will be useful, but           *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of               *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Lesser General Public License for more details.                      *
 *                                                                            *
 *   You should have received a copy of the GNU Lesser General Public         *
 *   License along with FreeCAD.  If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                         *
 *                                                                            *
 ******************************************************************************/

#include <FCConfig.h>

#ifdef FC_OS_WIN32
# include <Windows.h>
#endif
#ifdef FC_OS_MACOSX
# include <OpenGL/gl.h>
#else
# include <GL/gl.h>
#endif

#include <Inventor/SbLine.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoTransparencyType.h>
#include <Inventor/nodes/SoVertexProperty.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "SoFCGroundGrid.h"

#ifndef GL_DEPTH_CLAMP
# define GL_DEPTH_CLAMP 0x864F
#endif


using namespace Gui;

namespace
{
// Number of grid cells from the center of the grid to its border
constexpr int cellCount = 60;
// Every major line is this number of cells apart
constexpr int majorEvery = 10;
// Opacity of the lines in the center of the grid, they fade out towards the border
constexpr float minorAlpha = 0.22F;
constexpr float majorAlpha = 0.45F;
constexpr float axisAlpha = 0.9F;
// The spacing is chosen such that there are this many cells at most across the view
constexpr float maxCellsInView = 40.0F;
// Looking straight along an axis within about 0.8 degrees, as for the arrows of the
// navigation cube
constexpr float straightCosine = 0.9999F;
// The light spot on the ground: its radius relative to the size of the view, its
// opacity in the center and the number of rings and segments of the disk
constexpr float glowSize = 0.9F;
constexpr float glowAlpha = 0.55F;
constexpr int glowRings = 16;
constexpr int glowSegments = 72;

uint32_t packColor(const SbColor& color, float alpha)
{
    return color.getPackedValue(1.0F - alpha);
}

float fade(float distance)
{
    // Quadratic, so that lines near the center stay clear
    const float t = std::clamp(1.0F - distance, 0.0F, 1.0F);
    return t * t;
}
}  // namespace

SO_NODE_SOURCE(SoFCGroundGrid)

void SoFCGroundGrid::initClass()
{
    SO_NODE_INIT_CLASS(SoFCGroundGrid, SoSeparator, "Separator");
}

void SoFCGroundGrid::finish()
{
    atexit_cleanup();
}

SoFCGroundGrid::SoFCGroundGrid()
{
    SO_NODE_CONSTRUCTOR(SoFCGroundGrid);
    SO_NODE_ADD_FIELD(color, (SbColor(1.0F, 1.0F, 1.0F)));
    SO_NODE_ADD_FIELD(xAxisColor, (SbColor(0.85F, 0.2F, 0.2F)));
    SO_NODE_ADD_FIELD(yAxisColor, (SbColor(0.2F, 0.7F, 0.2F)));
    SO_NODE_ADD_FIELD(zAxisColor, (SbColor(0.2F, 0.3F, 0.9F)));
    SO_NODE_ADD_FIELD(glowColor, (SbColor(1.0F, 1.0F, 1.0F)));

    // The geometry depends on the camera, so it must not be cached
    renderCaching = SoSeparator::OFF;
    boundingBoxCaching = SoSeparator::OFF;
    renderCulling = SoSeparator::OFF;

    // The lines are semi-transparent. With the sorted transparency of the viewer,
    // transparent lines are not drawn at all, so they are blended right away.
    auto transparencyType = new SoTransparencyType;
    transparencyType->value = SoTransparencyType::BLEND;
    addChild(transparencyType);

    // The grid is an underlay: it is drawn before the scene, and without depth, so that
    // everything else is drawn over it. Otherwise it would cut through flat markers, such
    // as the points of sketches, and flicker with faces on the ground.
    auto depthBuffer = new SoDepthBuffer;
    depthBuffer->test = FALSE;
    depthBuffer->write = FALSE;
    addChild(depthBuffer);

    auto lightModel = new SoLightModel;
    lightModel->model = SoLightModel::BASE_COLOR;
    addChild(lightModel);

    auto pickStyle = new SoPickStyle;
    pickStyle->style = SoPickStyle::UNPICKABLE;
    addChild(pickStyle);

    // A soft light spot on the ground below the grid lines
    auto glowSeparator = new SoSeparator;
    // Its scale changes without notification, so a cached bounding box would be outdated
    // and the spot culled as soon as that box leaves the view
    glowSeparator->renderCaching = SoSeparator::OFF;
    glowSeparator->boundingBoxCaching = SoSeparator::OFF;
    glowSeparator->renderCulling = SoSeparator::OFF;
    glowTransform = new SoTransform;
    glowSeparator->addChild(glowTransform);
    glowVertices = new SoVertexProperty;
    glowVertices->materialBinding = SoVertexProperty::PER_VERTEX_INDEXED;
    auto glowFaces = new SoIndexedFaceSet;
    glowFaces->vertexProperty = glowVertices;
    glowSeparator->addChild(glowFaces);
    addChild(glowSeparator);

    // A disk of radius 1 made of rings, so that its opacity can fade out towards the rim
    std::vector<SbVec3f> diskPoints {SbVec3f(0.0F, 0.0F, 0.0F)};
    std::vector<int32_t> diskFaces;
    auto ringVertex = [](int ring, int segment) {
        return 1 + (ring - 1) * glowSegments + segment % glowSegments;
    };
    for (int ring = 1; ring <= glowRings; ++ring) {
        const float radius = static_cast<float>(ring) / glowRings;
        for (int segment = 0; segment < glowSegments; ++segment) {
            const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(segment) / glowSegments;
            diskPoints.emplace_back(radius * std::cos(angle), radius * std::sin(angle), 0.0F);

            if (ring == 1) {
                diskFaces.insert(diskFaces.end(), {0, ringVertex(1, segment), ringVertex(1, segment + 1), -1});
            }
            else {
                diskFaces.insert(
                    diskFaces.end(),
                    {ringVertex(ring - 1, segment),
                     ringVertex(ring, segment),
                     ringVertex(ring, segment + 1),
                     ringVertex(ring - 1, segment + 1),
                     -1}
                );
            }
        }
    }
    glowVertices->vertex.setValues(0, static_cast<int>(diskPoints.size()), diskPoints.data());
    glowFaces->coordIndex.setValues(0, static_cast<int>(diskFaces.size()), diskFaces.data());

    auto gridStyle = new SoDrawStyle;
    gridStyle->lineWidth = 1.0F;
    addChild(gridStyle);

    gridVertices = new SoVertexProperty;
    gridVertices->materialBinding = SoVertexProperty::PER_VERTEX;
    gridLines = new SoLineSet;
    gridLines->vertexProperty = gridVertices;
    addChild(gridLines);

    auto axisStyle = new SoDrawStyle;
    axisStyle->lineWidth = 1.0F;
    addChild(axisStyle);

    axisVertices = new SoVertexProperty;
    axisVertices->materialBinding = SoVertexProperty::PER_VERTEX;
    axisLines = new SoLineSet;
    axisLines->vertexProperty = axisVertices;
    addChild(axisLines);
}

SoFCGroundGrid::~SoFCGroundGrid() = default;

namespace
{
// The near and far planes follow the objects only, as the grid is not part of the
// bounding box of the scene. So the grid must not be clipped by them.
class DepthClamp
{
public:
    DepthClamp()
        : wasEnabled(glIsEnabled(GL_DEPTH_CLAMP))
    {
        glEnable(GL_DEPTH_CLAMP);
    }
    ~DepthClamp()
    {
        if (!wasEnabled) {
            glDisable(GL_DEPTH_CLAMP);
        }
    }
    DepthClamp(const DepthClamp&) = delete;
    DepthClamp& operator=(const DepthClamp&) = delete;

private:
    GLboolean wasEnabled;
};
}  // namespace

void SoFCGroundGrid::GLRenderBelowPath(SoGLRenderAction* action)
{
    updateGeometry(action->getState());
    DepthClamp clamp;
    inherited::GLRenderBelowPath(action);
}

void SoFCGroundGrid::GLRenderInPath(SoGLRenderAction* action)
{
    updateGeometry(action->getState());
    DepthClamp clamp;
    inherited::GLRenderInPath(action);
}

void SoFCGroundGrid::getBoundingBox(SoGetBoundingBoxAction* /*action*/)
{
    // Not part of the bounding box: neither when fitting the view, nor for the near and
    // far planes. The grid extends far beyond the objects, which would make the depth
    // range too coarse, and its position depends on the view, which would feed back.
}

void SoFCGroundGrid::updateGeometry(SoState* state)
{
    SoCacheElement::invalidate(state);

    const SbViewVolume& volume = SoViewVolumeElement::get(state);
    SbLine viewLine;
    volume.projectPointToLine(SbVec2f(0.5F, 0.5F), viewLine);
    const SbVec3f position = viewLine.getPosition();
    const SbVec3f direction = viewLine.getDirection();

    // The grid lies on the ground, the XY plane. When looking straight along one of the
    // axes, as in the front or the right view, it lies in the plane facing the viewer
    // instead, as the ground would only be seen edge-on.
    int normalAxis = 2;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(direction[axis]) > straightCosine) {
            normalAxis = axis;
        }
    }
    const int uAxis = normalAxis == 0 ? 1 : 0;
    const int vAxis = normalAxis == 2 ? 1 : 2;
    auto unit = [](int axis) {
        SbVec3f vector(0.0F, 0.0F, 0.0F);
        vector[axis] = 1.0F;
        return vector;
    };
    const SbVec3f u = unit(uAxis);
    const SbVec3f v = unit(vAxis);
    const SbVec3f normal = unit(normalAxis);

    // The grid is centered where the view direction meets its plane. For a view along the
    // plane, or when looking away from it, the point of the view direction nearest to the
    // origin is taken instead, dropped onto the plane. This must not depend on the near
    // and far planes, which change with every frame while navigating.
    SbVec3f center;
    const float facing = direction.dot(normal);
    const float hitDistance = std::fabs(facing) > 0.15F ? -position.dot(normal) / facing : -1.0F;
    if (hitDistance > 0.0F) {
        center = position + direction * hitDistance;
    }
    else {
        float distance = -position.dot(direction);
        if (volume.getProjectionType() == SbViewVolume::PERSPECTIVE) {
            // In front of the camera, at least as far as the camera is from the plane.
            // Not for an orthographic view, where the line starts at the near plane.
            distance = std::max(distance, std::max(std::fabs(position.dot(normal)), 1.0e-3F));
        }
        center = position + direction * distance;
    }
    center -= normal * center.dot(normal);

    const float viewSize = volume.getWorldToScreenScale(center, 1.0F);
    if (!std::isfinite(center[0]) || !std::isfinite(center[1]) || !std::isfinite(center[2])
        || !std::isfinite(viewSize) || viewSize <= 1.0e-6F) {
        return;
    }

    // The light spot is centered on the origin, lies in the plane of the grid and is scaled
    // with the zoom level on every frame. Notifications are off, as the new values are used
    // by this traversal already and must not trigger another one.
    const SbVec3f glowScale(viewSize * glowSize, viewSize * glowSize, 1.0F);
    const SbVec3f w = u.cross(v);
    // Rows are the images of the x, y and z axes of the disk
    const SbRotation glowRotation(SbMatrix(
        u[0], u[1], u[2], 0.0F,
        v[0], v[1], v[2], 0.0F,
        w[0], w[1], w[2], 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F
    ));
    if (glowTransform->scaleFactor.getValue() != glowScale
        || !glowTransform->rotation.getValue().equals(glowRotation, 1.0e-6F)) {
        const SbBool notify = glowTransform->enableNotify(FALSE);
        glowTransform->scaleFactor.setValue(glowScale);
        glowTransform->rotation.setValue(glowRotation);
        glowTransform->enableNotify(notify);
    }

    // Powers of ten, so that the lines are at round coordinates
    GeometryState next;
    next.normalAxis = normalAxis;
    next.spacing = std::pow(10.0F, std::ceil(std::log10(viewSize / maxCellsInView)));
    next.centerX = std::round(center.dot(u) / next.spacing) * next.spacing;
    next.centerY = std::round(center.dot(v) / next.spacing) * next.spacing;
    next.color = color.getValue().getPackedValue();
    next.xAxisColor = xAxisColor.getValue().getPackedValue();
    next.yAxisColor = yAxisColor.getValue().getPackedValue();
    next.zAxisColor = zAxisColor.getValue().getPackedValue();
    next.glowColor = glowColor.getValue().getPackedValue();
    if (next == geometryState) {
        return;  // also prevents a redraw on every frame, as setting the geometry notifies
    }
    if (next.glowColor != geometryState.glowColor) {
        // Smooth falloff: nearly even in the center, fading out softly towards the rim
        std::vector<uint32_t> glowColors {packColor(glowColor.getValue(), glowAlpha)};
        for (int ring = 1; ring <= glowRings; ++ring) {
            const float r = static_cast<float>(ring) / glowRings;
            const float alpha = glowAlpha * (1.0F - r * r * (3.0F - 2.0F * r));
            glowColors.insert(glowColors.end(), glowSegments, packColor(glowColor.getValue(), alpha));
        }
        glowVertices->orderedRGBA.setValues(0, static_cast<int>(glowColors.size()), glowColors.data());
    }
    geometryState = next;

    const float spacing = next.spacing;
    const float extent = spacing * cellCount;
    const float cx = next.centerX;
    const float cy = next.centerY;

    std::vector<SbVec3f> points;
    std::vector<uint32_t> colors;
    std::vector<int32_t> counts;
    points.reserve(4 * (2 * cellCount + 1) * 3);
    colors.reserve(points.capacity());
    counts.reserve(4 * (2 * cellCount + 1));

    // Each line has a vertex at its end points, which are transparent, and one in the
    // middle, so that the lines fade out towards the border of the grid
    auto addLine = [&](const SbVec3f& from, const SbVec3f& middle, const SbVec3f& to, uint32_t rgba) {
        const uint32_t clear = rgba & 0xffffff00U;
        points.push_back(from);
        points.push_back(middle);
        points.push_back(to);
        colors.push_back(clear);
        colors.push_back(rgba);
        colors.push_back(clear);
        counts.push_back(3);
    };

    // Points in the plane of the grid, by their coordinates along u and v
    auto at = [&](float a, float b) {
        return u * a + v * b;
    };

    const SbColor& gridColor = color.getValue();
    for (int i = -cellCount; i <= cellCount; ++i) {
        const float x = cx + static_cast<float>(i) * spacing;
        const float y = cy + static_cast<float>(i) * spacing;
        const float fading = fade(std::abs(static_cast<float>(i)) / cellCount);

        // The lines through the origin are drawn as axes
        const long long xIndex = std::llround(x / spacing);
        if (xIndex != 0) {
            const float alpha = (xIndex % majorEvery == 0 ? majorAlpha : minorAlpha) * fading;
            addLine(at(x, cy - extent), at(x, cy), at(x, cy + extent), packColor(gridColor, alpha));
        }
        const long long yIndex = std::llround(y / spacing);
        if (yIndex != 0) {
            const float alpha = (yIndex % majorEvery == 0 ? majorAlpha : minorAlpha) * fading;
            addLine(at(cx - extent, y), at(cx, y), at(cx + extent, y), packColor(gridColor, alpha));
        }
    }

    gridVertices->vertex.setValues(0, static_cast<int>(points.size()), points.data());
    gridVertices->vertex.setNum(static_cast<int>(points.size()));
    gridVertices->orderedRGBA.setValues(0, static_cast<int>(colors.size()), colors.data());
    gridVertices->orderedRGBA.setNum(static_cast<int>(colors.size()));
    gridLines->numVertices.setValues(0, static_cast<int>(counts.size()), counts.data());
    gridLines->numVertices.setNum(static_cast<int>(counts.size()));

    points.clear();
    colors.clear();
    counts.clear();

    // The two axes in the plane of the grid, in their own colors
    const SoSFColor* axisColors[] = {&xAxisColor, &yAxisColor, &zAxisColor};
    if (std::abs(cy) <= extent) {
        addLine(
            at(cx - extent, 0.0F),
            at(cx, 0.0F),
            at(cx + extent, 0.0F),
            packColor(axisColors[uAxis]->getValue(), axisAlpha * fade(std::abs(cy) / extent))
        );
    }
    if (std::abs(cx) <= extent) {
        addLine(
            at(0.0F, cy - extent),
            at(0.0F, cy),
            at(0.0F, cy + extent),
            packColor(axisColors[vAxis]->getValue(), axisAlpha * fade(std::abs(cx) / extent))
        );
    }

    axisVertices->vertex.setValues(0, static_cast<int>(points.size()), points.data());
    axisVertices->vertex.setNum(static_cast<int>(points.size()));
    axisVertices->orderedRGBA.setValues(0, static_cast<int>(colors.size()), colors.data());
    axisVertices->orderedRGBA.setNum(static_cast<int>(colors.size()));
    axisLines->numVertices.setValues(0, static_cast<int>(counts.size()), counts.data());
    axisLines->numVertices.setNum(static_cast<int>(counts.size()));
}
