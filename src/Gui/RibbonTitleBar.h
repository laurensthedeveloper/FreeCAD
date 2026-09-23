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
#include <string>
#include <vector>

#include <QPointer>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QIcon;
class QLabel;
class QMenuBar;
class QToolButton;

namespace Gui
{

/**
 * The top row of the main window: logo, quick access to file and edit commands, the
 * classic menus, command search and help.
 *
 * The menu bar of the main window is used as this row, so that all menus keep working
 * as before (shortcuts, Alt key navigation, menus changed by workbenches). The other
 * parts are placed into its corners.
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
    QMenuBar* menuBar() const;
    void placeInMenuBar();
    void setupQuickAccess(QHBoxLayout* layout);
    void updateQuickAccessIcons();
    void setupSearchAndHelp(QHBoxLayout* layout);
    void setupWindowButtons(QHBoxLayout* layout);
    void enableCustomFrame();
    void updateCustomFrame();
    void updateWindowButtons();

    static constexpr int quickIconSize = 13;
    static constexpr int lineIconSize = 16;

    static QIcon glyphIcon(const QString& family, QChar glyph, const QColor& color, qreal ratio);
    static QIcon grayIcon(const QIcon& icon, const QColor& color, qreal ratio);
    static QIcon searchIcon(const QColor& color, qreal ratio);
    static QIcon helpIcon(const QColor& color, qreal ratio);

    struct QuickButton
    {
        QPointer<QToolButton> button;
        QPointer<QAction> action;
        std::string command;
    };
    std::vector<QuickButton> _quickButtons;
    QWidget* _leftPart = nullptr;
    QWidget* _rightPart = nullptr;
    QLabel* _logo;
    QWidget* _windowButtons = nullptr;
    QAction* _searchAction = nullptr;
    QToolButton* _helpButton = nullptr;
    QToolButton* _maximizeButton = nullptr;
    bool _customFrame = false;

    class NativeFilter;
    std::unique_ptr<NativeFilter> _nativeFilter;
};

}  // namespace Gui
