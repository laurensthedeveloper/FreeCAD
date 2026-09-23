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

#include <QActionEvent>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

#include "RibbonBar.h"

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
    setVisible(shown);

    toolbar->installEventFilter(this);
    connect(toolbar, &QObject::destroyed, this, &QObject::deleteLater);
}

void RibbonGroup::updateCaption()
{
    if (!_toolbar) {
        return;
    }

    const QString title = _toolbar->windowTitle();
    const QString elided = _caption->fontMetrics().elidedText(title, Qt::ElideRight, _caption->width());
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
                setVisible(!_toolbar->isHidden());
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
    _layout->setContentsMargins(4, 2, 4, 2);
    _layout->setSpacing(12);
    _layout->addStretch();
}

void RibbonPanel::paintEvent(QPaintEvent* ev)
{
    QWidget::paintEvent(ev);

    // Derived from the text color so that the separator is visible in light and dark themes
    QColor color = palette().color(QPalette::WindowText);
    color.setAlpha(60);

    QPainter painter(this);
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
    , _tabRow(new QHBoxLayout())
    , _panel(new RibbonPanel(this))
    , _scrollArea(new QScrollArea(this))
{
    setObjectName(QStringLiteral("RibbonBar"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    _tabRow->setContentsMargins(0, 0, 0, 0);
    _tabRow->addStretch();
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
    layout->addWidget(_scrollArea);

    // Scroll horizontally with the mouse wheel and follow size changes of the groups
    _scrollArea->viewport()->installEventFilter(this);
    _scrollArea->horizontalScrollBar()->installEventFilter(this);
    _panel->installEventFilter(this);
    updateScrollAreaHeight();
}

bool RibbonBar::eventFilter(QObject* source, QEvent* ev)
{
    auto bar = _scrollArea->horizontalScrollBar();

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
        _tabRow->insertWidget(0, toolbar);
        toolbar->setVisible(shown);
        _workbenchToolBar = toolbar;
        return;
    }

    auto group = new RibbonGroup(toolbar, _panel);
    auto layout = _panel->groupLayout();
    layout->insertWidget(layout->count() - 1, group);  // keep the trailing stretch last
    _groups[toolbar] = group;

    connect(group, &QObject::destroyed, this, &RibbonBar::onGroupDestroyed);
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
    auto layout = _panel->groupLayout();
    int index = 0;
    for (const QString& name : names) {
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
        if (auto group = qobject_cast<RibbonGroup*>(layout->itemAt(i)->widget())) {
            if (group->toolBar()) {
                addToggle(group->toolBar());
            }
        }
    }

    if (!menu.isEmpty()) {
        menu.exec(ev->globalPos());
    }
}

#include "moc_RibbonBar.cpp"
