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

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <Inventor/SbBox3f.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoCoordinateElement.h>
#include <Inventor/elements/SoCullElement.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/misc/SoState.h>

#include "MarkerBitmaps.h"
#include "SoFCMarkerSet.h"


using namespace Gui;

namespace
{
// A highlighted point is drawn this much larger, with a halo of this size around it
constexpr float highlightScale = 1.25F;
constexpr float haloScale = 2.0F;
constexpr float haloAlpha = 0.25F;
// A faint shadow below the dot, offset downwards, so that it lifts off the background
constexpr float shadowAlpha = 0.18F;
constexpr float shadowOffset = 1.0F;

struct Dot
{
    SbVec3f position;  // in window coordinates, with the depth as in SoMarkerSet
    float radius;      // of the whole dot, including the ring
    SbColor color;
    float alpha;
    bool highlighted;
};

/// A disk around @a center, fading out over @a feather pixels at its rim, so that it is
/// drawn smoothly without relying on smoothing support of the graphics driver
void drawDisk(const SbVec3f& center, float radius, const SbColor& color, float alpha, float feather)
{
    const int segments = std::clamp(static_cast<int>(radius * 4.0F), 12, 64);
    const float inner = std::max(radius - feather / 2.0F, 0.0F);
    const float outer = radius + feather / 2.0F;
    auto vertex = [&](int segment, float r) {
        const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(segment)
            / static_cast<float>(segments);
        glVertex3f(center[0] + r * std::cos(angle), center[1] + r * std::sin(angle), center[2]);
    };

    glColor4f(color[0], color[1], color[2], alpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3fv(center.getValue());
    for (int segment = 0; segment <= segments; ++segment) {
        vertex(segment, inner);
    }
    glEnd();

    // The soft rim must not hide what is drawn behind it
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glDepthMask(GL_FALSE);
    glBegin(GL_TRIANGLE_STRIP);
    for (int segment = 0; segment <= segments; ++segment) {
        glColor4f(color[0], color[1], color[2], alpha);
        vertex(segment, inner);
        glColor4f(color[0], color[1], color[2], 0.0F);
        vertex(segment, outer);
    }
    glEnd();
    glDepthMask(depthMask);
}
}  // namespace

SO_NODE_SOURCE(SoFCMarkerSet)

void SoFCMarkerSet::initClass()
{
    SO_NODE_INIT_CLASS(SoFCMarkerSet, SoMarkerSet, "MarkerSet");
}

void SoFCMarkerSet::finish()
{
    atexit_cleanup();
}

SoFCMarkerSet::SoFCMarkerSet()
{
    SO_NODE_CONSTRUCTOR(SoFCMarkerSet);
    SO_NODE_ADD_FIELD(fillColor, (SbColor(1.0F, 1.0F, 1.0F)));
    SO_NODE_ADD_FIELD(ringWidth, (1.75F));
    SO_NODE_ADD_FIELD(highlightIndex, (-1));
    SO_NODE_ADD_FIELD(revealNearCursor, (FALSE));
    SO_NODE_ADD_FIELD(alwaysVisible, (TRUE));
    SO_NODE_ADD_FIELD(cursorPosition, (SbVec2f(-1.0e6F, -1.0e6F)));
    SO_NODE_ADD_FIELD(revealRadius, (60.0F));
}

SoFCMarkerSet::~SoFCMarkerSet() = default;

bool SoFCMarkerSet::hasOnlyFilledCircles() const
{
    for (int i = 0; i < markerIndex.getNum(); ++i) {
        const int index = markerIndex[i];
        if (index != NONE && !Inventor::MarkerBitmaps::isMarker("CIRCLE_FILLED", index)) {
            return false;
        }
    }
    return markerIndex.getNum() > 0;
}

void SoFCMarkerSet::GLRender(SoGLRenderAction* action)
{
    if (!hasOnlyFilledCircles()) {
        inherited::GLRender(action);
        return;
    }

    // The setup follows SoMarkerSet::GLRender()
    SoState* state = action->getState();
    state->push();
    SoLazyElement::setLightModel(state, SoLazyElement::BASE_COLOR);
    SoMultiTextureEnabledElement::disableAll(state);

    if (vertexProperty.getValue()) {
        vertexProperty.getValue()->GLRender(action);
    }

    const SoCoordinateElement* coords = nullptr;
    const SbVec3f* normals = nullptr;
    getVertexData(state, coords, normals, FALSE);

    if (!shouldGLRender(action)) {
        state->pop();
        return;
    }
    SoGLCacheContextElement::shouldAutoCache(state, SoGLCacheContextElement::DONT_AUTO_CACHE);

    const bool perVertex = SoMaterialBindingElement::get(state) != SoMaterialBindingElement::OVERALL;
    const SoLazyElement* lazy = SoLazyElement::getInstance(state);
    const int numColors = std::max(lazy->getNumDiffuse(), 1);
    const int numTransparencies = std::max(lazy->getNumTransparencies(), 1);

    const SbMatrix projection = SoModelMatrixElement::get(state) * SoViewingMatrixElement::get(state)
        * SoProjectionMatrixElement::get(state);
    const SbVec2s viewportSize = SoViewportRegionElement::get(state).getViewportSizePixels();

    const int32_t start = startIndex.getValue();
    int32_t count = numPoints.getValue();
    if (count < 0) {
        count = coords->getNum() - start;
    }

    std::vector<Dot> dots;
    dots.reserve(std::max(count, 0));
    for (int i = 0; i < count; ++i) {
        const int marker = markerIndex[std::min(i, markerIndex.getNum() - 1)];
        SbVec3f point = coords->get3(start + i);
        if (marker == NONE || SoCullElement::cullTest(state, SbBox3f(point, point), TRUE)) {
            continue;
        }

        projection.multVecMatrix(point, point);
        point[0] = (point[0] + 1.0F) * 0.5F * viewportSize[0];
        point[1] = (point[1] + 1.0F) * 0.5F * viewportSize[1];
        point[2] = -point[2];

        const bool highlighted = i == highlightIndex.getValue();
        float reveal = 1.0F;
        const bool alwaysShown = i < alwaysVisible.getNum() && alwaysVisible[i];
        if (revealNearCursor.getValue() && !highlighted && !alwaysShown) {
            // Fades in smoothly as the cursor approaches
            const SbVec2f offset = SbVec2f(point[0], point[1]) - cursorPosition.getValue();
            const float radius = std::max(revealRadius.getValue(), 1.0F);
            const float t = 1.0F - offset.length() / radius;
            if (t <= 0.0F) {
                continue;
            }
            reveal = t * t * (3.0F - 2.0F * t);
        }

        // The size of the bitmap is the size of the dot, so that it matches picking
        SbVec2s size;
        const unsigned char* bytes = nullptr;
        SbBool isLSBFirst = FALSE;
        SoMarkerSet::getMarker(marker, size, bytes, isLSBFirst);

        const int material = perVertex ? i : 0;
        Dot dot;
        dot.position = point;
        dot.radius = static_cast<float>(size[0]) / 2.0F + 0.5F;
        dot.color = SoLazyElement::getDiffuse(state, std::min(material, numColors - 1));
        dot.alpha = reveal
            * (1.0F - SoLazyElement::getTransparency(state, std::min(material, numTransparencies - 1)));
        dot.highlighted = highlighted;
        dots.push_back(dot);
    }

    // As in SoMarkerSet, the dots are drawn in window coordinates, without the clipping
    // planes, which would otherwise let markers vanish for certain view angles
    GLint numPlanes = 0;
    glGetIntegerv(GL_MAX_CLIP_PLANES, &numPlanes);
    std::vector<GLboolean> planesEnabled(numPlanes);
    for (GLint i = 0; i < numPlanes; ++i) {
        planesEnabled[i] = glIsEnabled(GL_CLIP_PLANE0 + i);
        glDisable(GL_CLIP_PLANE0 + i);
    }

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // The fill is drawn at the same depth as the ring below it
    glDepthFunc(GL_LEQUAL);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, viewportSize[0], 0, viewportSize[1], -1.0F, 1.0F);

    const SbColor& fill = fillColor.getValue();
    const SbColor shadow(0.0F, 0.0F, 0.0F);
    const float ring = std::max(ringWidth.getValue(), 0.0F);
    for (const Dot& dot : dots) {
        const float radius = dot.highlighted ? dot.radius * highlightScale : dot.radius;

        // The halo and the shadow are soft and must not hide what is behind them
        GLboolean depthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glDepthMask(GL_FALSE);
        if (dot.highlighted) {
            drawDisk(dot.position, radius * haloScale, dot.color, haloAlpha * dot.alpha, radius);
        }
        SbVec3f shadowPosition = dot.position;
        shadowPosition[1] -= shadowOffset;
        drawDisk(shadowPosition, radius + 0.5F, shadow, shadowAlpha * dot.alpha, 2.0F);
        glDepthMask(depthMask);

        drawDisk(dot.position, radius, dot.color, dot.alpha, 1.0F);
        if (radius - ring > 0.5F) {
            drawDisk(dot.position, radius - ring, fill, dot.alpha, 1.0F);
        }
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();

    for (GLint i = 0; i < numPlanes; ++i) {
        if (planesEnabled[i]) {
            glEnable(GL_CLIP_PLANE0 + i);
        }
    }

    state->pop();
}
