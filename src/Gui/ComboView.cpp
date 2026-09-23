/***************************************************************************
 *   Copyright (c) 2009 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include <cstring>

#include <QBoxLayout>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QSplitter>
#include <QTabBar>
#include <QToolButton>

#include <App/Application.h>

#include "ComboView.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "PropertyView.h"
#include "Tree.h"


using namespace Gui;
using namespace Gui::DockWnd;

namespace
{

ParameterGrp::handle comboViewParams()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/DockWindows/ComboView"
    );
}

// Rounded cards on the window background, see the Model panel mockup
const char* cardStyleSheet = R"(
QFrame#ModelCard, QFrame#PropertyCard {
    background-color: palette(base);
    border: 1px solid palette(midlight);
    border-radius: 8px;
}
QFrame#ModelCard QTreeView, QFrame#PropertyCard QTreeView {
    border: none;
}
QFrame#PropertyHeader {
    border: none;
    border-bottom: 1px solid palette(midlight);
    border-radius: 0px;
}
QTabBar#ModelTabBar::tab {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 4px 6px;
    margin-right: 6px;
}
QTabBar#ModelTabBar::tab:selected {
    border-bottom: 2px solid palette(highlight);
    font-weight: bold;
}
QToolButton#ModelCreateButton::menu-indicator {
    image: none;
}
)";

// Height of a card that only shows its header
int collapsedHeight(QFrame* card, QWidget* header)
{
    QMargins m = card->layout()->contentsMargins();
    return header->sizeHint().height() + m.top() + m.bottom() + 2 * card->frameWidth() + 2;
}

}  // namespace

/* TRANSLATOR Gui::DockWnd::ComboView */

ComboView::ComboView(Gui::Document* pcDocument, QWidget* parent)
    : DockWindow(pcDocument, parent)
{
    // The tab bar below takes over the dock title, see OverlayManager::setupTitleBar()
    setProperty("fcOwnTitleBar", true);
    setStyleSheet(QString::fromLatin1(cardStyleSheet));

    auto pLayout = new QGridLayout(this);
    pLayout->setSpacing(0);
    pLayout->setContentsMargins(6, 6, 6, 6);

    // splitter between the model card and the properties card
    splitter = new QSplitter();
    splitter->setOrientation(Qt::Vertical);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);
    pLayout->addWidget(splitter, 0, 0);

    // Model card: tabs, create button and the tree
    modelCard = new QFrame(this);
    modelCard->setObjectName(QStringLiteral("ModelCard"));
    auto modelLayout = new QVBoxLayout(modelCard);
    modelLayout->setSpacing(2);
    modelLayout->setContentsMargins(6, 4, 4, 6);

    modelHeader = new QWidget(modelCard);
    auto headerLayout = new QHBoxLayout(modelHeader);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    tabBar = new QTabBar(modelHeader);
    tabBar->setObjectName(QStringLiteral("ModelTabBar"));
    tabBar->setDrawBase(false);
    tabBar->setExpanding(false);
    tabBar->addTab(QString());
    tabBar->addTab(QString());
    tabBar->setCurrentIndex(comboViewParams()->GetInt("CurrentTab", 0) == 1 ? 1 : 0);
    headerLayout->addWidget(tabBar);
    headerLayout->addStretch();

    createMenu = new QMenu(this);
    connect(createMenu, &QMenu::aboutToShow, this, &ComboView::populateCreateMenu);
    createButton = new QToolButton(modelHeader);
    createButton->setObjectName(QStringLiteral("ModelCreateButton"));
    createButton->setIcon(BitmapFactory().iconFromTheme("list-add"));
    createButton->setAutoRaise(true);
    createButton->setPopupMode(QToolButton::InstantPopup);
    createButton->setMenu(createMenu);
    headerLayout->addWidget(createButton);
    modelLayout->addWidget(modelHeader);

    tree = new TreePanel("ComboView", modelCard);
    modelLayout->addWidget(tree);
    splitter->addWidget(modelCard);

    // Properties card: collapsible header and the property view
    propCard = new QFrame(this);
    propCard->setObjectName(QStringLiteral("PropertyCard"));
    auto propLayout = new QVBoxLayout(propCard);
    propLayout->setSpacing(0);
    propLayout->setContentsMargins(4, 2, 4, 6);

    propHeader = new QFrame(propCard);
    propHeader->setObjectName(QStringLiteral("PropertyHeader"));
    propHeader->setCursor(Qt::PointingHandCursor);
    propHeader->installEventFilter(this);
    auto propHeaderLayout = new QHBoxLayout(propHeader);
    propHeaderLayout->setContentsMargins(4, 2, 0, 2);
    propTitle = new QLabel(propHeader);
    QFont font = propTitle->font();
    font.setBold(true);
    propTitle->setFont(font);
    propHeaderLayout->addWidget(propTitle);
    propHeaderLayout->addStretch();
    collapseButton = new QToolButton(propHeader);
    collapseButton->setAutoRaise(true);
    connect(collapseButton, &QToolButton::clicked, this, [this]() {
        setPropertyCollapsed(!propCollapsed);
    });
    propHeaderLayout->addWidget(collapseButton);
    propLayout->addWidget(propHeader);

    prop = new PropertyView(propCard);
    propLayout->addWidget(prop);
    splitter->addWidget(propCard);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    connect(tabBar, &QTabBar::currentChanged, this, [this](int index) {
        comboViewParams()->SetInt("CurrentTab", index);
        updateSections();
    });

    retranslateUi();
    propCollapsed = comboViewParams()->GetBool("PropertyCollapsed", false);
    updateSections();
}

ComboView::~ComboView() = default;

void ComboView::setPropertyCollapsed(bool collapsed)
{
    if (propCollapsed == collapsed) {
        return;
    }
    propCollapsed = collapsed;
    comboViewParams()->SetBool("PropertyCollapsed", collapsed);
    updateSections();
}

bool ComboView::isPropertyCollapsed() const
{
    return propCollapsed;
}

void ComboView::updateSections()
{
    // The Model tab shows the tree with the properties card below it, the
    // Property tab gives the whole panel to the properties
    bool propertyTab = tabBar->currentIndex() == 1;
    bool showTree = !propertyTab;
    bool showProp = propertyTab || !propCollapsed;

    // remember the split to restore it when both cards are shown again
    if (tree->isVisible() && prop->isVisible()) {
        expandedSizes = splitter->sizes();
    }

    tree->setVisible(showTree);
    modelCard->setMaximumHeight(showTree ? QWIDGETSIZE_MAX : collapsedHeight(modelCard, modelHeader));

    prop->setVisible(showProp);
    propHeader->setVisible(!propertyTab);
    propCard->setMaximumHeight(showProp ? QWIDGETSIZE_MAX : collapsedHeight(propCard, propHeader));
    collapseButton->setArrowType(propCollapsed ? Qt::UpArrow : Qt::DownArrow);

    if (showTree && showProp && expandedSizes.size() == 2) {
        splitter->setSizes(expandedSizes);
    }
}

void ComboView::populateCreateMenu()
{
    // Refilled each time, as commands of workbenches loaded later become available
    createMenu->clear();
    auto& manager = Application::Instance->commandManager();
    bool pendingSeparator = false;
    for (const char* name :
         {"PartDesign_Body", "Sketcher_NewSketch", "Separator", "Std_Part", "Std_Group"}) {
        if (strcmp(name, "Separator") == 0) {
            pendingSeparator = !createMenu->isEmpty();
            continue;
        }
        if (Command* cmd = manager.getCommandByName(name)) {
            if (pendingSeparator) {
                createMenu->addSeparator();
                pendingSeparator = false;
            }
            cmd->addTo(createMenu);
        }
    }
}

void ComboView::retranslateUi()
{
    tabBar->setTabText(0, tr("Model"));
    tabBar->setTabText(1, tr("Property"));
    createButton->setToolTip(tr("Add to the model"));
    propTitle->setText(tr("Properties"));
    collapseButton->setToolTip(tr("Show or hide the properties"));
}

void ComboView::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    DockWindow::changeEvent(e);
}

bool ComboView::eventFilter(QObject* o, QEvent* e)
{
    // clicking anywhere on the properties header toggles it, like the chevron
    if (o == propHeader && e->type() == QEvent::MouseButtonRelease) {
        setPropertyCollapsed(!propCollapsed);
        return true;
    }
    return DockWindow::eventFilter(o, e);
}

#include "moc_ComboView.cpp"
