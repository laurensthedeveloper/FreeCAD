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

#include <map>
#include <vector>

#include <QPointer>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QIcon;
class QLabel;
class QScrollArea;
class QToolBar;
class QToolButton;

namespace Gui
{

/**
 * A single captioned section of the ribbon. It hosts exactly one toolbar and shows
 * the toolbar's title above its buttons. The group follows the explicit visibility
 * of its toolbar so that hiding a toolbar also hides its section.
 */
class RibbonGroup: public QWidget
{
    Q_OBJECT

public:
    RibbonGroup(QToolBar* toolbar, QWidget* parent);

    QToolBar* toolBar() const
    {
        return _toolbar;
    }

    /// Shows the group only if it belongs to the ribbon page currently shown
    void setOnActivePage(bool onActivePage);

protected:
    bool eventFilter(QObject* source, QEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

private:
    void updateCaption();
    void updateVisibility();
    static void updateIconText(QAction* action);

    QPointer<QToolBar> _toolbar;
    QLabel* _caption;
    bool _onActivePage = true;
};

/**
 * The row of ribbon groups. Paints a vertical separator between visible groups.
 */
class RibbonPanel: public QWidget
{
    Q_OBJECT

public:
    explicit RibbonPanel(QWidget* parent);

    QHBoxLayout* groupLayout() const
    {
        return _layout;
    }

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    QHBoxLayout* _layout;
};

/**
 * Ribbon style replacement for the top toolbar area.
 *
 * The upper row holds the Home button and the workbench selector, the lower row shows the
 * toolbars as captioned groups with their text under the icons. The Home page has its own
 * set of general commands (File, Edit, ...), the workbench page shows the toolbars of the
 * active workbench except the general ones, which would duplicate Home. The groups keep
 * their natural size and scroll horizontally when they do not fit. Toolbars keep being
 * created and managed by ToolBarManager; the ribbon only hosts them.
 */
class RibbonBar: public QWidget
{
    Q_OBJECT

public:
    explicit RibbonBar(QWidget* parent);

    /// Moves \a toolbar into the ribbon. Does nothing if it is already hosted.
    void addToolBar(QToolBar* toolbar);
    /// Returns true if \a widget is a toolbar hosted by the ribbon.
    bool contains(const QWidget* widget) const;
    /// Sorts the groups to follow the given toolbar names. Unlisted groups go last.
    void setOrder(const QStringList& names);
    /// Switches between the Home page and the page of the active workbench
    void setHomeActive(bool active);

protected:
    void contextMenuEvent(QContextMenuEvent* ev) override;
    bool eventFilter(QObject* source, QEvent* ev) override;

private:
    static bool isGeneralToolBar(const QToolBar* toolbar);
    void setupSearchAndHelp();
    QIcon searchIcon() const;
    void buildHomePage();
    void onGroupDestroyed(QObject* group);
    void onWorkbenchTabClicked();
    void connectWorkbenchTabs();
    void updateScrollAreaHeight();

    bool _homeActive = false;
    QToolButton* _homeButton;
    QHBoxLayout* _tabRow;
    RibbonPanel* _panel;
    QScrollArea* _scrollArea;
    QPointer<QToolBar> _workbenchToolBar;
    std::map<const QToolBar*, QPointer<RibbonGroup>> _groups;
    std::vector<QPointer<RibbonGroup>> _homeGroups;
};

}  // namespace Gui
