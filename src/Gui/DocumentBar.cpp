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

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include "DocumentBar.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "MainWindow.h"
#include "MDIView.h"

using namespace Gui;

namespace
{
// The standard views in the middle of the bar
const std::vector<const char*> viewCommands {
    "Std_ViewIsometric",
    "Std_ViewFront",
    "Std_ViewTop",
    "Std_ViewRight",
    "Separator",
    "Std_AxisCross",
    "Std_ViewRear",
    "Std_ViewBottom",
    "Std_ViewLeft",
};

// The drop-down menu of the settings button next to the views
const std::vector<const char*> settingsCommands {
    "Std_OrthographicCamera",
    "Std_PerspectiveCamera",
    "Separator",
    "Std_ViewDimetric",
    "Std_ViewTrimetric",
    "Separator",
    "Std_MainFullscreen",
};

// Status bar items that are shown on the right side of the document bar
const QList<QByteArray> hostedStatusItems {
    QByteArrayLiteral("NavigationIndicator"),
    QByteArrayLiteral("sizeLabel"),
};

void addCommands(QWidget* widget, const std::vector<const char*>& commands)
{
    auto& commandManager = Application::Instance->commandManager();
    for (const char* command : commands) {
        if (qstrcmp(command, "Separator") == 0) {
            auto separator = new QAction(widget);
            separator->setSeparator(true);
            widget->addAction(separator);
        }
        else if (commandManager.getCommandByName(command)) {
            commandManager.addTo(command, widget);
        }
    }
}
}  // namespace

// -----------------------------------------------------------

void MdiArea::setDocumentBar(QWidget* bar)
{
    // Hide the own tab bar, it keeps working as the model of the tabbed view. A zero
    // maximum size keeps it invisible even if QMdiArea shows it again.
    _tabBar = findChild<QTabBar*>(QString(), Qt::FindDirectChildrenOnly);
    if (_tabBar) {
        _tabBar->setMaximumSize(0, 0);
        _tabBar->hide();
    }

    _bar = bar;
    bar->setParent(this);
    bar->installEventFilter(this);
    bar->show();
    scheduleLayout();
}

void MdiArea::scheduleLayout()
{
    // The layout is never done from within the events that request it: changing the
    // viewport margins while QMdiArea handles a resize, or resizing the bar while it
    // handles a layout request, re-enters these events and ends in endless recursion.
    // Several requests in a row result in a single layout.
    if (!_bar || _layoutPending) {
        return;
    }
    _layoutPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            _layoutPending = false;
            layoutDocumentBar();
        },
        Qt::QueuedConnection
    );
}

void MdiArea::layoutDocumentBar()
{
    if (!_bar) {
        return;
    }

    // QMdiArea reserves the height of its own tab bar at the bottom whenever it updates
    // (tab added or removed, resize, ...). Instead of overriding that each time, which
    // makes both fight endlessly, the own tab bar gets the height of the document bar so
    // that QMdiArea reserves the right space itself.
    const int height = _bar->sizeHint().height();
    if (_tabBar && _tabBar->count() > 0) {
        matchTabBarHeight(height);
        const QMargins margins(0, 0, 0, _tabBar->sizeHint().height());
        if (viewportMargins() != margins) {
            setViewportMargins(margins);
        }
    }

    const QRect rect = contentsRect();
    _bar->setGeometry(rect.left(), rect.bottom() - height + 1, rect.width(), height);
    _bar->raise();
}

void MdiArea::matchTabBarHeight(int height)
{
    if (_tabBar->sizeHint().height() == height) {
        return;
    }

    // The size hint also contains style dependent parts, so correct the tab height by
    // the remaining difference until it fits
    int tabHeight = _tabHeight > 0 ? _tabHeight : height;
    for (int i = 0; i < 3; ++i) {
        _tabBar->setStyleSheet(
            QStringLiteral("QTabBar::tab { height: %1px; margin: 0px; padding: 0px; border: none; }")
                .arg(tabHeight)
        );
        const int difference = height - _tabBar->sizeHint().height();
        if (difference == 0) {
            break;
        }
        tabHeight += difference;
    }
    _tabHeight = tabHeight;
}

void MdiArea::resizeEvent(QResizeEvent* ev)
{
    QMdiArea::resizeEvent(ev);
    scheduleLayout();
}

bool MdiArea::viewportEvent(QEvent* ev)
{
    // QMdiArea sets the viewport margins for its own tab bar whenever a tab is added or
    // removed, which resizes the viewport
    if (ev->type() == QEvent::Resize) {
        scheduleLayout();
    }
    return QMdiArea::viewportEvent(ev);
}

bool MdiArea::eventFilter(QObject* source, QEvent* ev)
{
    if (source == _bar && ev->type() == QEvent::LayoutRequest) {
        scheduleLayout();
    }
    return QMdiArea::eventFilter(source, ev);
}

// -----------------------------------------------------------

DocumentBar::DocumentBar(QMdiArea* mdiArea, QWidget* parent)
    : QWidget(parent)
    , _mdiArea(mdiArea)
    , _startButton(new QToolButton(this))
    , _tabs(new QTabBar(this))
    , _newButton(new QToolButton(this))
    , _viewsBar(new QToolBar(this))
    , _settingsButton(new QToolButton(this))
    , _rightBar(new QToolBar(this))
    , _statusLayout(new QHBoxLayout())
{
    setObjectName(QStringLiteral("DocumentBar"));
    setAttribute(Qt::WA_StyledBackground);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(8);

    // Left: Start page, open documents and a new document
    _startButton->setObjectName(QStringLiteral("DocumentBarStart"));
    _startButton->setText(tr("Start"));
    _startButton->setToolTip(tr("Shows the start page"));
    _startButton->setIcon(BitmapFactory().iconFromTheme("Std_ViewHome"));
    _startButton->setIconSize(QSize(20, 20));
    _startButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _startButton->setCheckable(true);
    _startButton->setAutoRaise(true);
    connect(_startButton, &QToolButton::clicked, this, &DocumentBar::onStartClicked);
    layout->addWidget(_startButton);

    setupTabs();
    layout->addWidget(_tabs, 0, Qt::AlignVCenter);

    _newButton->setObjectName(QStringLiteral("DocumentBarNew"));
    _newButton->setToolTip(tr("Creates a new empty document"));
    _newButton->setIcon(BitmapFactory().iconFromTheme("list-add"));
    _newButton->setIconSize(QSize(16, 16));
    _newButton->setAutoRaise(true);
    connect(_newButton, &QToolButton::clicked, this, [] {
        Application::Instance->commandManager().runCommandByName("Std_New");
    });
    layout->addWidget(_newButton);
    layout->addStretch();

    // Middle: standard views and view settings, each in a rounded frame
    setupViews();
    auto viewsFrame = new QFrame(this);
    viewsFrame->setObjectName(QStringLiteral("DocumentBarViews"));
    auto viewsLayout = new QHBoxLayout(viewsFrame);
    viewsLayout->setContentsMargins(4, 2, 4, 2);
    viewsLayout->addWidget(_viewsBar);
    layout->addWidget(viewsFrame);

    auto settingsFrame = new QFrame(this);
    settingsFrame->setObjectName(QStringLiteral("DocumentBarViews"));
    auto settingsLayout = new QHBoxLayout(settingsFrame);
    settingsLayout->setContentsMargins(4, 2, 4, 2);
    settingsLayout->addWidget(_settingsButton);
    layout->addWidget(settingsFrame);
    layout->addStretch();

    // Right: draw style and the hosted status bar items
    _rightBar->setObjectName(QStringLiteral("DocumentBarRight"));
    _rightBar->setIconSize(QSize(24, 24));
    addCommands(_rightBar, {"Std_DrawStyle"});
    layout->addWidget(_rightBar);

    _statusLayout->setContentsMargins(0, 0, 0, 0);
    _statusLayout->setSpacing(8);
    layout->addLayout(_statusLayout);

    setupStyle();
    // Check again once the main window is shown, as the theme is fully applied only then
    QMetaObject::invokeMethod(this, &DocumentBar::setupStyle, Qt::QueuedConnection);
    refreshTabs();
}

void DocumentBar::setupStyle()
{
    // Themes do not always set an application palette matching their style sheet, so the
    // palette cannot be used directly. The menu bar is styled by every theme and its
    // polished palette follows the style sheet, so its background decides between a
    // light and a dark set of colors. Not the own palette, which the style sheet below
    // changes.
    QWidget* reference = getMainWindow() ? getMainWindow()->menuBar() : nullptr;
    QColor background = QApplication::palette().color(QPalette::Window);
    if (reference) {
        reference->ensurePolished();
        background = reference->palette().color(reference->backgroundRole());
    }
    const bool dark = background.lightness() < 128;
    if (_darkStyle == int(dark)) {
        return;
    }
    _darkStyle = int(dark);

    struct Colors
    {
        const char* bar;
        const char* panel;
        const char* border;
        const char* hover;
        const char* selected;
        const char* text;
    };
    const Colors light {"#eceef1", "#ffffff", "#d5d9df", "#e2e6eb", "#dde5f0", "#1f2328"};
    const Colors darkColors {"#2b2d31", "#3a3d42", "#4a4e55", "#45494f", "#4b5361", "#e6e8eb"};
    const Colors& c = dark ? darkColors : light;

    setStyleSheet(
        QStringLiteral(
            "#DocumentBar { background: %1; }"
            "#DocumentBar QToolButton, #DocumentBar QPushButton, #DocumentBar QLabel { color: %6; }"
            "#DocumentBar QToolButton { border: none; border-radius: 6px; padding: 4px 6px;"
            "  background: transparent; }"
            "#DocumentBar QToolButton:hover { background: %4; }"
            "#DocumentBar QToolButton:checked { background: %2; border: 1px solid %3; }"
            "#DocumentBar QToolBar { border: none; background: transparent; spacing: 4px; }"
            "#DocumentBarViews { background: %2; border: 1px solid %3; border-radius: 10px; }"
            "#DocumentBarViews QToolButton[current=\"true\"] { background: %5; }"
            "#DocumentBarTabs::tab { color: %6; background: transparent;"
            "  border: 1px solid transparent; border-radius: 8px; padding: 6px 8px; margin: 0px 2px; }"
            "#DocumentBarTabs::tab:hover { background: %4; }"
            "#DocumentBarTabs::tab:selected { background: %2; border: 1px solid %3; }"
            "#DocumentBarTabs[inactive=\"true\"]::tab:selected { background: transparent;"
            "  border: 1px solid transparent; }"
        )
            .arg(QLatin1String(c.bar),
                 QLatin1String(c.panel),
                 QLatin1String(c.border),
                 QLatin1String(c.hover),
                 QLatin1String(c.selected),
                 QLatin1String(c.text))
    );
}

void DocumentBar::changeEvent(QEvent* ev)
{
    QWidget::changeEvent(ev);
    // The theme may have switched between light and dark. Queued, as changing the
    // style sheet from within this event causes further palette changes.
    if (ev->type() == QEvent::PaletteChange) {
        QMetaObject::invokeMethod(this, &DocumentBar::setupStyle, Qt::QueuedConnection);
    }
}

void DocumentBar::setupTabs()
{
    _tabs->setObjectName(QStringLiteral("DocumentBarTabs"));
    _tabs->setDocumentMode(true);
    _tabs->setDrawBase(false);
    _tabs->setExpanding(false);
    _tabs->setTabsClosable(true);
    _tabs->setUsesScrollButtons(true);
    _tabs->setElideMode(Qt::ElideRight);
    _tabs->setIconSize(QSize(20, 20));

    // Clicked also fires for the current tab, which is needed to leave the Start page
    connect(_tabs, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index >= 0 && index < int(_tabWindows.size())) {
            activateWindow(_tabWindows[index]);
        }
    });
    connect(_tabs, &QTabBar::tabCloseRequested, this, [this](int index) {
        if (index >= 0 && index < int(_tabWindows.size()) && _tabWindows[index]) {
            _tabWindows[index]->close();
        }
    });

    if (_mdiArea) {
        connect(_mdiArea, &QMdiArea::subWindowActivated, this, &DocumentBar::scheduleRefresh);
        // The sub windows are children of the viewport
        _mdiArea->viewport()->installEventFilter(this);
    }
}

void DocumentBar::setupViews()
{
    _viewsBar->setObjectName(QStringLiteral("DocumentBarViewsBar"));
    _viewsBar->setIconSize(QSize(24, 24));
    addCommands(_viewsBar, viewCommands);
    connect(_viewsBar, &QToolBar::actionTriggered, this, &DocumentBar::onViewTriggered);

    auto menu = new QMenu(_settingsButton);
    addCommands(menu, settingsCommands);
    _settingsButton->setObjectName(QStringLiteral("DocumentBarSettings"));
    _settingsButton->setToolTip(tr("View settings"));
    _settingsButton->setIcon(BitmapFactory().iconFromTheme("preferences-system"));
    _settingsButton->setIconSize(QSize(24, 24));
    _settingsButton->setPopupMode(QToolButton::InstantPopup);
    _settingsButton->setMenu(menu);
}

bool DocumentBar::hostsStatusItem(const QByteArray& id)
{
    return hostedStatusItems.contains(id);
}

void DocumentBar::addStatusItem(QWidget* widget)
{
    if (widget && _statusLayout->indexOf(widget) < 0) {
        _statusLayout->addWidget(widget);
    }
}

void DocumentBar::removeStatusItem(QWidget* widget)
{
    if (widget && _statusLayout->indexOf(widget) >= 0) {
        _statusLayout->removeWidget(widget);
        widget->hide();
    }
}

bool DocumentBar::eventFilter(QObject* source, QEvent* ev)
{
    switch (ev->type()) {
        case QEvent::ChildAdded:
        case QEvent::ChildRemoved:
            // A sub window was added to or removed from the viewport
            if (_mdiArea && source == _mdiArea->viewport()) {
                scheduleRefresh();
            }
            break;
        case QEvent::WindowTitleChange:
        case QEvent::WindowIconChange:
        case QEvent::ModifiedChange:
            if (qobject_cast<QMdiSubWindow*>(source)) {
                scheduleRefresh();
            }
            break;
        default:
            break;
    }
    return QWidget::eventFilter(source, ev);
}

void DocumentBar::scheduleRefresh()
{
    // Several changes usually come together, e.g. when a document is closed
    if (_refreshPending) {
        return;
    }
    _refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        _refreshPending = false;
        refreshTabs();
    });
}

void DocumentBar::refreshTabs()
{
    if (!_mdiArea) {
        return;
    }

    const QSignalBlocker blocker(_tabs);
    while (_tabs->count() > 0) {
        _tabs->removeTab(0);
    }
    _tabWindows.clear();

    QMdiSubWindow* active = _mdiArea->activeSubWindow();
    int current = -1;
    bool startActive = false;

    for (QMdiSubWindow* window : _mdiArea->subWindowList()) {
        window->installEventFilter(this);

        // The Start page has its own button
        if (isStartView(window)) {
            startActive = startActive || window == active;
            continue;
        }

        const int index = _tabs->addTab(window->windowIcon(), tabText(window));
        _tabs->setTabToolTip(index, tabText(window));
        _tabWindows.emplace_back(window);
        if (window == active) {
            current = index;
        }
    }

    if (current >= 0) {
        _tabs->setCurrentIndex(current);
    }
    // An empty tab bar still takes space, e.g. with only the Start page open
    _tabs->setVisible(_tabs->count() > 0);

    // A tab bar always has a current tab, so show it as not selected while another
    // window (e.g. the Start page) is active
    const bool inactive = current < 0;
    if (_tabs->property("inactive").toBool() != inactive) {
        _tabs->setProperty("inactive", inactive);
        _tabs->style()->unpolish(_tabs);
        _tabs->style()->polish(_tabs);
    }

    _startButton->setChecked(startActive);
}

void DocumentBar::activateWindow(QMdiSubWindow* window)
{
    if (!window) {
        return;
    }

    if (auto view = qobject_cast<MDIView*>(window->widget())) {
        getMainWindow()->setActiveWindow(view);
    }
    else if (_mdiArea) {
        _mdiArea->setActiveSubWindow(window);
    }
    scheduleRefresh();
}

void DocumentBar::onStartClicked()
{
    if (_mdiArea) {
        for (QMdiSubWindow* window : _mdiArea->subWindowList()) {
            if (isStartView(window)) {
                activateWindow(window);
                return;
            }
        }
    }

    // Not open, the command of the Start module opens it
    auto& commandManager = Application::Instance->commandManager();
    if (commandManager.getCommandByName("Start_Start")) {
        commandManager.runCommandByName("Start_Start");
    }
    scheduleRefresh();
}

void DocumentBar::onViewTriggered(QAction* action)
{
    // Highlight the last chosen standard view
    for (auto button : _viewsBar->findChildren<QToolButton*>()) {
        const bool current = button->defaultAction() == action;
        if (button->property("current").toBool() != current) {
            button->setProperty("current", current);
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
    }
}

bool DocumentBar::isStartView(const QMdiSubWindow* window)
{
    return window && window->widget()
        && window->widget()->objectName() == QLatin1String("StartView");
}

QString DocumentBar::tabText(const QMdiSubWindow* window)
{
    // Same as the tab text of QMdiArea: the modified marker becomes an asterisk
    QString title = window->windowTitle();
    title.replace(QLatin1String("[*]"), window->isWindowModified() ? QStringLiteral("*") : QString());
    return title;
}

#include "moc_DocumentBar.cpp"
