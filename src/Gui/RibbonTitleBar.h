// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2026 laurensthedeveloper                                 *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#pragma once

#include <memory>

#include <QWidget>

class QHBoxLayout;
class QIcon;
class QLabel;
class QToolButton;

namespace Gui
{

/**
 * The top row of the ribbon: logo, quick access to file and edit commands, the window
 * title, command search and help.
 *
 * On Windows it can replace the title bar of the main window: the system caption is
 * removed and this row takes its place, with its own minimize, maximize and close
 * buttons. Its empty parts still behave like the system caption (dragging, double click,
 * snapping), as they are reported to Windows as caption.
 */
class RibbonTitleBar: public QWidget
{
    Q_OBJECT

public:
    explicit RibbonTitleBar(QWidget* parent);
    ~RibbonTitleBar() override;

    /// Returns true if a mouse press at \a posInWindow on \a child should move the window
    bool isCaption(const QWidget* child, const QPoint& posInWindow) const;

protected:
    bool eventFilter(QObject* source, QEvent* ev) override;

private:
    void setupQuickAccess(QHBoxLayout* layout);
    void setupSearchAndHelp(QHBoxLayout* layout);
    void setupWindowButtons(QHBoxLayout* layout);
    void enableCustomFrame();
    void updateCustomFrame();
    void updateWindowButtons();
    QIcon searchIcon() const;

    QLabel* _logo;
    QLabel* _title;
    QWidget* _windowButtons = nullptr;
    QToolButton* _maximizeButton = nullptr;
    bool _customFrame = false;

    class NativeFilter;
    std::unique_ptr<NativeFilter> _nativeFilter;
};

}  // namespace Gui
