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

#include <vector>

#include <QMdiArea>
#include <QPointer>
#include <QWidget>

class QHBoxLayout;
class QMdiSubWindow;
class QTabBar;
class QToolBar;
class QToolButton;

namespace Gui
{

/**
 * QMdiArea that can show a DocumentBar in place of its own tab bar.
 *
 * The own tab bar stays the model of the tabbed view (it is used to switch and close
 * windows), but it is hidden and the space QMdiArea reserves for it is given to the
 * document bar instead.
 */
class MdiArea: public QMdiArea
{
    Q_OBJECT

public:
    using QMdiArea::QMdiArea;

    void setDocumentBar(QWidget* bar);

protected:
    void resizeEvent(QResizeEvent* ev) override;
    bool viewportEvent(QEvent* ev) override;
    bool eventFilter(QObject* source, QEvent* ev) override;

private:
    void scheduleLayout();
    void layoutDocumentBar();
    void matchTabBarHeight(int height);

    QPointer<QWidget> _bar;
    QPointer<QTabBar> _tabBar;
    int _tabHeight = 0;
    bool _layoutPending = false;
};

/**
 * Bar below the 3D views: Start button, document tabs, new document button, the standard
 * views, view settings and, on the right, selected status bar items (e.g. the navigation
 * style and the unit system).
 */
class DocumentBar: public QWidget
{
    Q_OBJECT

public:
    explicit DocumentBar(QMdiArea* mdiArea, QWidget* parent = nullptr);

    /// Returns true if the status bar item with \a id is shown in this bar
    static bool hostsStatusItem(const QByteArray& id);
    /// Appends a status bar item to the right side of the bar
    void addStatusItem(QWidget* widget);
    /// Removes a status bar item from the bar, the widget is hidden but not deleted
    void removeStatusItem(QWidget* widget);

protected:
    bool eventFilter(QObject* source, QEvent* ev) override;
    void changeEvent(QEvent* ev) override;

private:
    void setupStyle();
    void setupTabs();
    void setupViews();
    void scheduleRefresh();
    void refreshTabs();
    void activateWindow(QMdiSubWindow* window);
    void onStartClicked();
    void onViewTriggered(QAction* action);

    static bool isStartView(const QMdiSubWindow* window);
    static QString tabText(const QMdiSubWindow* window);

    QPointer<QMdiArea> _mdiArea;
    QToolButton* _startButton;
    QTabBar* _tabs;
    QToolButton* _newButton;
    QToolBar* _viewsBar;
    QToolButton* _settingsButton;
    QToolBar* _rightBar;
    QHBoxLayout* _statusLayout;
    std::vector<QPointer<QMdiSubWindow>> _tabWindows;
    bool _refreshPending = false;
    int _darkStyle = -1;  // -1: style not set yet
};

}  // namespace Gui
