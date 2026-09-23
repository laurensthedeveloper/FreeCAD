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

#include <Inventor/fields/SoMFBool.h>
#include <Inventor/fields/SoSFBool.h>
#include <Inventor/fields/SoSFColor.h>
#include <Inventor/fields/SoSFVec2f.h>
#include <Inventor/fields/SoSFFloat.h>
#include <Inventor/fields/SoSFInt32.h>
#include <Inventor/nodes/SoMarkerSet.h>
#include <FCGlobal.h>

namespace Gui
{

/**
 * A marker set that draws filled circle markers as smooth round dots: a light fill
 * inside a ring in the color of the point, with a faint shadow. One point can be
 * highlighted, for example while it is preselected: it is drawn larger and with a soft
 * halo in its color around it.
 *
 * Markers other than the filled circles of MarkerBitmaps are drawn as in SoMarkerSet.
 * Picking is the same as in SoMarkerSet.
 */
class GuiExport SoFCMarkerSet: public SoMarkerSet
{
    using inherited = SoMarkerSet;

    SO_NODE_HEADER(Gui::SoFCMarkerSet);

public:
    static void initClass();
    static void finish();
    SoFCMarkerSet();

    SoSFColor fillColor;       //!< inside of the dots, the ring has the color of the point
    SoSFFloat ringWidth;       //!< in pixels
    SoSFInt32 highlightIndex;  //!< point drawn highlighted, counted from startIndex, -1 for none

    /** With revealNearCursor, only the points with alwaysVisible set and the highlighted
     *  point are always drawn. The others fade in when the cursor, in viewport pixels,
     *  comes within revealRadius pixels of them. */
    SoSFBool revealNearCursor;
    SoMFBool alwaysVisible;
    SoSFVec2f cursorPosition;
    SoSFFloat revealRadius;

    void GLRender(SoGLRenderAction* action) override;

protected:
    ~SoFCMarkerSet() override;

private:
    bool hasOnlyFilledCircles() const;
};

}  // namespace Gui
