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

#pragma once

#include <cstdint>

#include <Inventor/fields/SoSFColor.h>
#include <Inventor/nodes/SoSeparator.h>
#include <FCGlobal.h>

class SoLineSet;
class SoState;
class SoTransform;
class SoVertexProperty;

namespace Gui
{

/**
 * A grid on the XY plane with the X and Y axes through the origin. The spacing of
 * the grid follows the zoom level and the grid fades out towards its border, so that
 * it appears to be endless.
 */
class GuiExport SoFCGroundGrid: public SoSeparator
{
    using inherited = SoSeparator;

    SO_NODE_HEADER(Gui::SoFCGroundGrid);

public:
    static void initClass();
    static void finish();
    SoFCGroundGrid();

    SoSFColor color;  //!< color of the grid lines
    SoSFColor xAxisColor;
    SoSFColor yAxisColor;
    SoSFColor glowColor;  //!< color of the light spot on the ground

    void GLRenderBelowPath(SoGLRenderAction* action) override;
    void GLRenderInPath(SoGLRenderAction* action) override;
    void getBoundingBox(SoGetBoundingBoxAction* action) override;

protected:
    ~SoFCGroundGrid() override;

private:
    void updateGeometry(SoState* state);

    struct GeometryState
    {
        float spacing {0.0F};
        float centerX {0.0F};
        float centerY {0.0F};
        uint32_t color {0};
        uint32_t xAxisColor {0};
        uint32_t yAxisColor {0};
        uint32_t glowColor {0};

        bool operator==(const GeometryState&) const = default;
    };

    SoTransform* glowTransform {nullptr};
    SoVertexProperty* glowVertices {nullptr};
    SoVertexProperty* gridVertices {nullptr};
    SoLineSet* gridLines {nullptr};
    SoVertexProperty* axisVertices {nullptr};
    SoLineSet* axisLines {nullptr};
    GeometryState geometryState;
};

}  // namespace Gui
