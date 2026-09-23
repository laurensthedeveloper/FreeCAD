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

#include <algorithm>
#include <vector>

#include <QActionEvent>
#include <QApplication>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QLineEdit>
#include <QMenuBar>
#include <QShortcut>
#include <QStyleOption>
#include <QWidgetAction>

#include <App/Application.h>

#include "RibbonBar.h"
#include "RibbonTitleBar.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "CommandCompleter.h"
#include "MainWindow.h"
#include "WorkbenchManager.h"

using namespace Gui;

namespace
{
// Moves a toolbar out of the main window layout while keeping its explicit visibility,
// as QMainWindow::removeToolBar() always hides the toolbar.
bool takeFromMainWindow(QToolBar* toolbar)
{
    bool shown = !toolbar->isHidden();
    if (auto mw = qobject_cast<QMainWindow*>(toolbar->parentWidget())) {
        if (mw->toolBarArea(toolbar) != Qt::NoToolBarArea) {
            mw->removeToolBar(toolbar);
        }
    }
    return shown;
}

// Removes the name of the active workbench from the start of a toolbar title, e.g.
// "Part Design Helper Features" becomes "Helper Features" in the PartDesign workbench.
// The whole title is kept if nothing would be left.
QString withoutWorkbenchPrefix(const QString& title)
{
    QString key = QString::fromStdString(WorkbenchManager::instance()->activeName());
    if (key.endsWith(QLatin1String("Workbench"))) {
        key.chop(9);
    }
    if (key.isEmpty()) {
        return title;
    }

    const QStringList words = title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString joined;
    for (int i = 0; i + 1 < words.size() && joined.size() < key.size(); ++i) {
        joined += words[i];
        if (joined.compare(key, Qt::CaseInsensitive) == 0) {
            QString rest = words.mid(i + 1).join(QLatin1Char(' '));
            rest[0] = rest[0].toUpper();
            return rest;
        }
    }
    return title;
}

struct HomeSection
{
    const char* title;
    std::vector<const char*> commands;
};

// Content of the Home page. Commands that are not available (e.g. from a module that is
// not installed) are skipped.
const std::vector<HomeSection>& homeSections()
{
    // The most used file and edit commands are in the quick access bar above the tabs,
    // see RibbonTitleBar
    static const std::vector<HomeSection> sections {
        {QT_TRANSLATE_NOOP("Workbench", "Document"),
         {"Std_Import",
          "Std_SaveAs",
          "Std_SaveAll",
          "Separator",
          "Std_Print",
          "Std_PrintPdf",
          "Separator",
          "Std_SelectAll"}},
        {QT_TRANSLATE_NOOP("Workbench", "Structure"),
         {"Std_Part", "Std_Group", "Std_LinkActions", "Std_VarSet"}},
        {QT_TRANSLATE_NOOP("Workbench", "Macro"),
         {"Std_DlgMacroRecord", "Std_DlgMacroExecute", "Std_DlgMacroExecuteDirect"}},
        {QT_TRANSLATE_NOOP("Workbench", "Tools"),
         {"Std_DlgPreferences", "Std_DlgCustomize", "Std_DlgParameter", "Std_AddonMgr"}},
        {QT_TRANSLATE_NOOP("Workbench", "Help"),
         {"Std_OnlineHelp", "Std_WhatsThis", "Std_FreeCADForum", "Std_About"}},
    };
    return sections;
}

// Builds the button label shown under the icon from the command's menu text. The text is
// broken into two lines at the space closest to its middle, and single line labels get an
// empty second line so that all labels in the ribbon start at the same height.
QString ribbonLabel(const QString& menuText)
{
    QString text = menuText.trimmed();
    if (text.endsWith(QLatin1String("..."))) {
        text.chop(3);
    }
    else if (text.endsWith(QChar(0x2026))) {
        text.chop(1);
    }

    // Drop mnemonic markers but keep escaped ampersands, as the tool button shows mnemonics
    QString label;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == QLatin1Char('&')) {
            if (i + 1 < text.size() && text[i + 1] == QLatin1Char('&')) {
                label += QLatin1String("&&");
                ++i;
            }
            continue;
        }
        label += text[i];
    }
    label = label.trimmed();

    const int middle = label.size() / 2;
    int split = -1;
    for (int i = 0; i < label.size(); ++i) {
        if (label[i] == QLatin1Char(' ') && (split < 0 || qAbs(i - middle) < qAbs(split - middle))) {
            split = i;
        }
    }

    if (split < 0) {
        return label + QLatin1String("\n ");
    }
    return label.left(split) + QLatin1Char('\n') + label.mid(split + 1);
}
}  // namespace

// -----------------------------------------------------------

RibbonGroup::RibbonGroup(QToolBar* toolbar, QWidget* parent)
    : QWidget(parent)
    , _toolbar(toolbar)
    , _caption(new QLabel(this))
{
    setObjectName(QStringLiteral("RibbonGroup"));
    _caption->setObjectName(QStringLiteral("RibbonGroupCaption"));
    // The caption must not widen the group, long captions are elided instead
    _caption->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);
    layout->addWidget(_caption);

    bool shown = takeFromMainWindow(toolbar);
    toolbar->setOrientation(Qt::Horizontal);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolbar->setMovable(false);
    // Never shrink below the size hint, which would move buttons into the extension menu.
    // The ribbon scrolls instead.
    toolbar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    for (QAction* action : toolbar->actions()) {
        updateIconText(action);
    }
    layout->addWidget(toolbar);
    layout->addStretch();
    toolbar->setVisible(shown);

    updateCaption();
    updateVisibility();

    toolbar->installEventFilter(this);
    connect(toolbar, &QObject::destroyed, this, &QObject::deleteLater);
}

void RibbonGroup::updateCaption()
{
    if (!_toolbar) {
        return;
    }

    const QString title = _toolbar->windowTitle();
    const QString caption = withoutWorkbenchPrefix(title);
    const QString elided = _caption->fontMetrics().elidedText(caption, Qt::ElideRight, _caption->width());
    _caption->setText(elided);
    _caption->setToolTip(elided == title ? QString() : title);
}

void RibbonGroup::updateIconText(QAction* action)
{
    if (!action || action->isSeparator() || qobject_cast<QWidgetAction*>(action)) {
        return;
    }

    // Derived from the text on every change, so that commands which change their text
    // (e.g. drop-down groups showing the last used command) keep a matching label
    const QString label = ribbonLabel(action->text());
    if (action->iconText() != label) {
        action->setIconText(label);
    }
}

void RibbonGroup::setOnActivePage(bool onActivePage)
{
    _onActivePage = onActivePage;
    updateVisibility();
    // The caption depends on the active workbench, which may have changed
    updateCaption();
}

void RibbonGroup::updateVisibility()
{
    setVisible(_toolbar && !_toolbar->isHidden() && _onActivePage);
}

void RibbonGroup::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    updateCaption();
}

bool RibbonGroup::eventFilter(QObject* source, QEvent* ev)
{
    if (source == _toolbar) {
        switch (ev->type()) {
            // Sent on explicit show/hide of the toolbar, even while the group itself is hidden
            case QEvent::ShowToParent:
            case QEvent::HideToParent:
                updateVisibility();
                break;
            case QEvent::WindowTitleChange:
                updateCaption();
                break;
            case QEvent::ActionAdded:
            case QEvent::ActionChanged:
                updateIconText(static_cast<QActionEvent*>(ev)->action());
                break;
            case QEvent::ParentChange:
                // The toolbar was moved elsewhere (e.g. into the status bar)
                if (_toolbar->parentWidget() != this) {
                    _toolbar->removeEventFilter(this);
                    deleteLater();
                }
                break;
            default:
                break;
        }
    }
    return QWidget::eventFilter(source, ev);
}

// -----------------------------------------------------------

RibbonPanel::RibbonPanel(QWidget* parent)
    : QWidget(parent)
    , _layout(new QHBoxLayout(this))
{
    setObjectName(QStringLiteral("RibbonPanel"));
    setAttribute(Qt::WA_StyledBackground);
    _layout->setContentsMargins(8, 4, 8, 4);
    _layout->setSpacing(12);
    _layout->addStretch();
}

void RibbonPanel::paintEvent(QPaintEvent* ev)
{
    Q_UNUSED(ev)
    QPainter painter(this);

    // The rounded card background from the style sheet of the ribbon
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

    // Derived from the text color so that the separator is visible in light and dark themes
    QColor color = palette().color(QPalette::WindowText);
    color.setAlpha(60);
    painter.setPen(color);

    bool first = true;
    for (int i = 0; i < _layout->count(); ++i) {
        auto widget = _layout->itemAt(i)->widget();
        if (!widget || widget->isHidden()) {
            continue;
        }
        if (!first) {
            const QRect rect = widget->geometry();
            const int x = rect.left() - _layout->spacing() / 2;
            painter.drawLine(x, rect.top() + 4, x, rect.bottom() - 4);
        }
        first = false;
    }
}

// -----------------------------------------------------------

RibbonBar::RibbonBar(QWidget* parent)
    : QWidget(parent)
    , _homeButton(new QToolButton(this))
    , _tabRow(new QHBoxLayout())
    , _panel(new RibbonPanel(this))
    , _scrollArea(new QScrollArea(this))
{
    setObjectName(QStringLiteral("RibbonBar"));
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // No outer margins, so that the title row reaches the edges of the window for its
    // window buttons. The rows below have their own margins.
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 8);
    layout->setSpacing(0);

    _homeButton->setObjectName(QStringLiteral("RibbonHomeButton"));
    _homeButton->setText(tr("Home"));
    _homeButton->setToolTip(tr("Shows the general commands, such as file and edit commands"));
    _homeButton->setIcon(BitmapFactory().iconFromTheme("Std_ViewHome"));
    _homeButton->setIconSize(QSize(tabIconSize, tabIconSize));  // same as the workbench tabs
    _homeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _homeButton->setCheckable(true);
    connect(_homeButton, &QToolButton::clicked, this, &RibbonBar::setHomeActive);

    // Title row in the menu bar: quick access to file and edit commands, the menus,
    // command search and help. On Windows it also replaces the title bar of the window.
    new RibbonTitleBar(this);

    _tabRow->setContentsMargins(8, 0, 8, 0);
    _tabRow->setSpacing(6);
    _tabRow->addWidget(_homeButton, 0, Qt::AlignBottom);
    _tabRow->addStretch();
    setupSettingsButton();
    layout->addLayout(_tabRow);

    _scrollArea->setObjectName(QStringLiteral("RibbonScrollArea"));
    _scrollArea->setFrameShape(QFrame::NoFrame);
    _scrollArea->setWidgetResizable(true);
    _scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    _scrollArea->setWidget(_panel);
    // Let the toolbar background show through, setWidget() enables filling the panel
    _panel->setAutoFillBackground(false);
    _scrollArea->viewport()->setAutoFillBackground(false);
    _scrollArea->setStyleSheet(QStringLiteral(
        "#RibbonScrollArea, #RibbonScrollArea > #qt_scrollarea_viewport { background: transparent; }"
    ));
    auto panelRow = new QHBoxLayout();
    panelRow->setContentsMargins(8, 0, 8, 0);
    panelRow->addWidget(_scrollArea);
    layout->addLayout(panelRow);

    // Scroll horizontally with the mouse wheel and follow size changes of the groups
    _scrollArea->viewport()->installEventFilter(this);
    _scrollArea->horizontalScrollBar()->installEventFilter(this);
    _panel->installEventFilter(this);
    updateScrollAreaHeight();

    setupStyle();
    // Check again once the main window is shown, as the theme is fully applied only then
    QMetaObject::invokeMethod(this, &RibbonBar::setupStyle, Qt::QueuedConnection);
}

bool RibbonBar::isDarkTheme()
{
    // Themes do not always set an application palette matching their style sheet. The
    // menu bar is styled by every theme and its polished palette follows the style sheet.
    QWidget* reference = getMainWindow() ? getMainWindow()->menuBar() : nullptr;
    QColor background = QApplication::palette().color(QPalette::Window);
    if (reference) {
        reference->ensurePolished();
        background = reference->palette().color(reference->backgroundRole());
    }
    return background.lightness() < 128;
}

void RibbonBar::setupStyle()
{
    const bool dark = isDarkTheme();
    if (_darkStyle == int(dark)) {
        return;  // also prevents a loop, as the style sheet changes the palette
    }
    _darkStyle = int(dark);

    struct Colors
    {
        const char* bar;       // behind the tabs
        const char* tab;       // unselected tab
        const char* hover;     // tab under the mouse
        const char* selected;  // selected tab
        const char* accent;    // text of the selected tab
        const char* card;      // panel with the commands
        const char* text;
    };
    const Colors light {"#e9ecf0", "#dde1e7", "#d2d8e0", "#d6e4f7", "#1f5fbf", "#f9fafb", "#1f2328"};
    const Colors darkColors {"#232529", "#2f3237", "#3a3e44", "#2f4260", "#8fbaff", "#2c2f34", "#e6e8eb"};
    const Colors& c = dark ? darkColors : light;

    // Large rounded tabs for Home and the workbenches, above a rounded card with the
    // commands. The workbench tabs are styled here as they are part of the ribbon.
    const QString tab = QStringLiteral(
        "background: %2; color: %7; border: none; border-top-left-radius: 10px;"
        " border-top-right-radius: 10px; padding: 7px 14px; font-weight: 500;"
    );
    const QString sheet =
        (QStringLiteral("#RibbonBar { background: %1; }"
                        // The menu bar is the title row above the ribbon
                        "QMenuBar { background: %1; color: %7; border: none; }"
                        "QMenuBar::item { background: transparent; color: %7; padding: 8px 6px;"
                        "  border-radius: 6px; }"
                        "QMenuBar::item:selected, QMenuBar::item:pressed { background: %3; }"
                        "#RibbonPanel { background: %6; border-radius: 10px;"
                        "  border-top-left-radius: 0px; }"
                        "#RibbonGroupCaption { color: %7; font-weight: 600; }"
                        "#RibbonHomeButton { ")
         + tab
         + QStringLiteral(
             " }"
             "#RibbonHomeButton:hover { background: %3; }"
             "#RibbonHomeButton:checked { background: %4; color: %5; }"
             "#RibbonBar QTabBar::tab { "
         )
         + tab
         + QStringLiteral(
             " margin-right: 6px; }"
             "#RibbonBar QTabBar::tab:hover { background: %3; }"
             "#RibbonBar QTabBar::tab:selected { background: %4; color: %5; }"
             "#RibbonBar QTabBar[homeActive=\"true\"]::tab:selected { background: %2; color: %7; }"
             "#RibbonSettingsButton, #RibbonHelpButton { border: none; border-radius: 8px;"
             "  padding: 4px; background: transparent; }"
             "#RibbonSettingsButton:hover, #RibbonHelpButton:hover { background: %3; }"
             "#RibbonSettingsButton::menu-indicator { image: none; }"
             "#RibbonQuickAccess { border: none; background: transparent; spacing: 0px;"
             "  padding: 0px; }"
             "#RibbonQuickAccess QToolButton { border: none; border-radius: 5px; padding: 1px;"
             "  background: transparent; }"
             "#RibbonQuickAccess QToolButton:hover { background: %3; }"
             "#RibbonWindowTitle { color: %7; }"
             "#RibbonSearch { border: 1px solid rgba(128, 128, 128, 110); border-radius: 8px;"
             "  padding: 3px 6px; background: %6; color: %7; }"
             "#RibbonSearch:focus { border: 1px solid %5; }"
             "QToolButton[windowButton=\"true\"] { border: none; border-radius: 0px;"
             "  background: transparent; color: %7; }"
             "QToolButton[windowButton=\"true\"]:hover { background: %3; }"
             "#RibbonCloseButton:hover { background: #c42b1c; color: white; }"
         ))
            .arg(QLatin1String(c.bar),
                 QLatin1String(c.tab),
                 QLatin1String(c.hover),
                 QLatin1String(c.selected),
                 QLatin1String(c.accent),
                 QLatin1String(c.card),
                 QLatin1String(c.text));

    // The parts of the title row are in the menu bar, outside of the ribbon, so the
    // menu bar gets the same style sheet
    setStyleSheet(sheet);
    if (auto mainWindow = getMainWindow()) {
        mainWindow->menuBar()->setStyleSheet(sheet);
    }
}

void RibbonBar::changeEvent(QEvent* ev)
{
    QWidget::changeEvent(ev);
    // The theme may have switched between light and dark. Queued, as changing the
    // style sheet from within this event causes further palette changes.
    if (ev->type() == QEvent::PaletteChange) {
        QMetaObject::invokeMethod(this, &RibbonBar::setupStyle, Qt::QueuedConnection);
    }
}

void RibbonBar::setupSettingsButton()
{
    // Settings at the far right of the tab row. As the classic menu bar is hidden with
    // the ribbon, its menus are offered here as well, so that every command stays
    // reachable.
    auto button = new QToolButton(this);
    button->setObjectName(QStringLiteral("RibbonSettingsButton"));
    button->setToolTip(tr("Settings and all menus"));
    button->setIcon(BitmapFactory().iconFromTheme("preferences-system"));
    button->setIconSize(QSize(20, 20));
    button->setPopupMode(QToolButton::InstantPopup);

    auto menu = new QMenu(button);
    button->setMenu(menu);
    connect(menu, &QMenu::aboutToShow, this, [menu] {
        // Rebuilt each time, as the menus change with the active workbench
        menu->clear();
        auto& commandManager = Application::Instance->commandManager();
        for (const char* command : {"Std_DlgPreferences", "Std_DlgCustomize"}) {
            if (commandManager.getCommandByName(command)) {
                commandManager.addTo(command, menu);
            }
        }
        menu->addSeparator();
        if (auto menuBar = getMainWindow()->menuBar()) {
            for (QAction* action : menuBar->actions()) {
                if (action->menu() && action->isVisible()) {
                    menu->addMenu(action->menu());
                }
            }
        }
    });

    _tabRow->addWidget(button);
}

bool RibbonBar::eventFilter(QObject* source, QEvent* ev)
{
    auto bar = _scrollArea->horizontalScrollBar();

    // The workbench tabs are added to the workbench toolbar after it moved into the ribbon
    if (source == _workbenchToolBar && ev->type() == QEvent::ActionAdded) {
        QMetaObject::invokeMethod(this, &RibbonBar::connectWorkbenchTabs, Qt::QueuedConnection);
    }

    if (source == _scrollArea->viewport() && ev->type() == QEvent::Wheel) {
        auto wheel = static_cast<QWheelEvent*>(ev);
        const QPoint delta = wheel->angleDelta();
        bar->setValue(bar->value() - (delta.x() != 0 ? delta.x() : delta.y()));
        return true;
    }

    if ((source == _panel && ev->type() == QEvent::LayoutRequest)
        || (source == bar && (ev->type() == QEvent::Show || ev->type() == QEvent::Hide))) {
        updateScrollAreaHeight();
    }

    return QWidget::eventFilter(source, ev);
}

void RibbonBar::updateScrollAreaHeight()
{
    // The scroll area has no natural height, so give it the height of the groups plus
    // room for the scroll bar when the groups do not fit horizontally
    auto bar = _scrollArea->horizontalScrollBar();
    int height = _panel->sizeHint().height();
    if (bar->isVisible()) {
        height += bar->sizeHint().height();
    }
    _scrollArea->setFixedHeight(height);
}

void RibbonBar::addToolBar(QToolBar* toolbar)
{
    if (!toolbar || contains(toolbar)) {
        return;
    }

    // The workbench selector goes into the tab row, all other toolbars become groups
    if (toolbar->objectName() == QLatin1String("Workbench")) {
        bool shown = takeFromMainWindow(toolbar);
        toolbar->setOrientation(Qt::Horizontal);
        toolbar->setMovable(false);
        _tabRow->insertWidget(_tabRow->indexOf(_homeButton) + 1, toolbar, 0, Qt::AlignBottom);
        toolbar->setVisible(shown);
        _workbenchToolBar = toolbar;
        toolbar->installEventFilter(this);
        connectWorkbenchTabs();
        return;
    }

    auto group = new RibbonGroup(toolbar, _panel);
    auto layout = _panel->groupLayout();
    layout->insertWidget(layout->count() - 1, group);  // keep the trailing stretch last
    group->setOnActivePage(!_homeActive && !isGeneralToolBar(toolbar));
    _groups[toolbar] = group;

    connect(group, &QObject::destroyed, this, &RibbonBar::onGroupDestroyed);
}

bool RibbonBar::isGeneralToolBar(const QToolBar* toolbar)
{
    // The general toolbars of the standard workbench, matched by their untranslated name.
    // Their commands are on the Home page, so they are not shown on the workbench pages.
    static const QStringList generalToolBars {
        QStringLiteral("File"),
        QStringLiteral("Edit"),
        QStringLiteral("Clipboard"),
        QStringLiteral("Macro"),
        QStringLiteral("Structure"),
        QStringLiteral("Help"),
    };
    return generalToolBars.contains(toolbar->objectName());
}

void RibbonBar::buildHomePage()
{
    if (!_homeGroups.empty()) {
        return;
    }

    // Built on first use, so that commands registered late by modules are available
    auto& commandManager = Application::Instance->commandManager();
    auto layout = _panel->groupLayout();

    for (const HomeSection& section : homeSections()) {
        auto toolbar = new QToolBar(_panel);
        toolbar->setObjectName(QStringLiteral("RibbonHome_") + QLatin1String(section.title));
        toolbar->setWindowTitle(QApplication::translate("Workbench", section.title));
        toolbar->setIconSize(QSize(iconSize(), iconSize()));

        for (const char* command : section.commands) {
            if (qstrcmp(command, "Separator") == 0) {
                toolbar->addSeparator();
            }
            else if (commandManager.getCommandByName(command)) {
                commandManager.addTo(command, toolbar);
            }
        }

        auto group = new RibbonGroup(toolbar, _panel);
        layout->insertWidget(layout->count() - 1, group);  // keep the trailing stretch last
        toolbar->show();
        _homeGroups.emplace_back(group);
    }
}

void RibbonBar::setHomeActive(bool active)
{
    _homeActive = active;
    _homeButton->setChecked(active);

    if (active) {
        buildHomePage();
    }

    for (const auto& group : _homeGroups) {
        if (group) {
            group->setOnActivePage(active);
        }
    }

    for (const auto& [toolbar, group] : _groups) {
        if (group && group->toolBar()) {
            group->setOnActivePage(!active && !isGeneralToolBar(group->toolBar()));
        }
    }

    // The tab of the active workbench stays current in the tab bar, show it as not
    // selected while the Home page is shown
    if (_workbenchToolBar) {
        for (auto tabBar : _workbenchToolBar->findChildren<QTabBar*>()) {
            if (tabBar->property("homeActive").toBool() != active) {
                tabBar->setProperty("homeActive", active);
                tabBar->style()->unpolish(tabBar);
                tabBar->style()->polish(tabBar);
                tabBar->update();
            }
        }
    }

    _scrollArea->horizontalScrollBar()->setValue(0);
    _panel->update();
}

void RibbonBar::connectWorkbenchTabs()
{
    if (!_workbenchToolBar) {
        return;
    }

    // Clicking a workbench tab, also the one of the active workbench, leaves the Home page
    for (auto tabBar : _workbenchToolBar->findChildren<QTabBar*>()) {
        // Large tabs as in the Home button, without the line below the tabs
        tabBar->setDrawBase(false);
        tabBar->setIconSize(QSize(tabIconSize, tabIconSize));
        connect(
            tabBar,
            &QTabBar::tabBarClicked,
            this,
            &RibbonBar::onWorkbenchTabClicked,
            Qt::UniqueConnection
        );
    }
}

void RibbonBar::onWorkbenchTabClicked()
{
    setHomeActive(false);
}

int RibbonBar::iconSize()
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/MainWindow"
    );
    return std::max(int(hGrp->GetInt("RibbonIconSize", 32)), 16);
}

bool RibbonBar::isGroupToolBar(const QWidget* widget) const
{
    auto it = _groups.find(qobject_cast<const QToolBar*>(widget));
    return it != _groups.end() && it->second && widget->parentWidget() == it->second;
}

bool RibbonBar::contains(const QWidget* widget) const
{
    if (!widget) {
        return false;
    }
    if (widget == _workbenchToolBar && widget->parentWidget() == this) {
        return true;
    }

    auto it = _groups.find(qobject_cast<const QToolBar*>(widget));
    return it != _groups.end() && it->second && widget->parentWidget() == it->second;
}

void RibbonBar::setOrder(const QStringList& names)
{
    // The view toolbars go last, after the commands of the workbench
    static const QStringList trailing {QStringLiteral("View"), QStringLiteral("Individual Views")};
    QStringList ordered;
    for (const QString& name : names) {
        if (!trailing.contains(name)) {
            ordered << name;
        }
    }
    for (const QString& name : trailing) {
        if (names.contains(name)) {
            ordered << name;
        }
    }

    auto layout = _panel->groupLayout();
    int index = 0;
    for (const QString& name : std::as_const(ordered)) {
        for (const auto& [toolbar, group] : _groups) {
            if (!group || !group->toolBar() || group->toolBar()->objectName() != name) {
                continue;
            }
            if (layout->indexOf(group) != index) {
                layout->removeWidget(group);
                layout->insertWidget(index, group);
            }
            ++index;
            break;
        }
    }
    _panel->update();
}

void RibbonBar::onGroupDestroyed(QObject* group)
{
    for (auto it = _groups.begin(); it != _groups.end();) {
        if (!it->second || static_cast<QObject*>(it->second.data()) == group) {
            it = _groups.erase(it);
        }
        else {
            ++it;
        }
    }
}

void RibbonBar::contextMenuEvent(QContextMenuEvent* ev)
{
    QMenu menu;

    auto addToggle = [&menu](QToolBar* toolbar) {
        QAction* action = toolbar->toggleViewAction();
        if (action->isVisible() && !action->text().isEmpty()) {
            menu.addAction(action);
        }
    };

    if (_workbenchToolBar) {
        addToggle(_workbenchToolBar);
    }

    auto layout = _panel->groupLayout();
    for (int i = 0; i < layout->count(); ++i) {
        // Only the workbench toolbars that can actually be shown in the ribbon
        auto group = qobject_cast<RibbonGroup*>(layout->itemAt(i)->widget());
        auto toolbar = group ? group->toolBar() : nullptr;
        if (toolbar && _groups.count(toolbar) && !isGeneralToolBar(toolbar)) {
            addToggle(toolbar);
        }
    }

    if (!menu.isEmpty()) {
        menu.exec(ev->globalPos());
    }
}

#include "moc_RibbonBar.cpp"
