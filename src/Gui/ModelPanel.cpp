// SPDX-License-Identifier: LGPL-2.1-or-later

#include <algorithm>
#include <cstring>

#include <QAbstractItemModel>
#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QRegion>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeView>

#include <App/Application.h>

#include "ModelPanel.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "MainWindow.h"
#include "MDIView.h"
#include "PropertyView.h"
#include "Tree.h"
#include "propertyeditor/PropertyEditor.h"


using namespace Gui;

namespace
{

// Distance of the panel to the edges of the 3D view
constexpr int Margin = 10;
// Gap between the model card and the properties card
constexpr int CardSpacing = 8;
// Border width of the cards, see setupStyle()
constexpr int CardBorder = 1;
// The width follows the 3D view within these limits
constexpr int MinWidth = 240;
constexpr int MaxWidth = 340;
constexpr int WidthPercent = 22;
// Content height a card keeps when both do not fit
constexpr int MinContent = 60;
// Share of the height the properties get when both cards do not fit
constexpr int PropertyPercent = 45;

QPointer<ModelPanel> panelInstance;

ParameterGrp::handle panelParams()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/DockWindows/ModelPanel"
    );
}

// Height needed to show all expanded rows of the view without scrolling. Stops counting
// at \a limit, as the result never needs to exceed the height of the 3D view.
int contentHeight(const QTreeView* view, int limit)
{
    int height = 2 * view->frameWidth();
    if (!view->isHeaderHidden()) {
        height += view->header()->sizeHint().height();
    }
    if (view->horizontalScrollBar()->isVisible()) {
        height += view->horizontalScrollBar()->sizeHint().height();
    }
    const QAbstractItemModel* model = view->model();
    if (!model) {
        return height;
    }
    for (QModelIndex index = model->index(0, 0, view->rootIndex());
         index.isValid() && height < limit;
         index = view->indexBelow(index)) {
        height += view->visualRect(index).height();
    }
    return height;
}

}  // namespace

/* TRANSLATOR Gui::ModelPanel */

ModelPanel::ModelPanel(QMdiArea* mdiArea)
    : QWidget(mdiArea)
    , _mdiArea(mdiArea)
{
    setObjectName(QStringLiteral("ModelPanel"));
    panelInstance = this;

    auto hGrp = panelParams();
    _minimized = hGrp->GetBool("Minimized", false);
    _propertyCollapsed = hGrp->GetBool("PropertyCollapsed", false);

    setupModelCard();
    setupPropertyCard();
    retranslateUi();
    setupStyle();
    connectContentSignals();

    // The viewport is the area of the 3D view, without the document bar
    mdiArea->viewport()->installEventFilter(this);
    connect(mdiArea, &QMdiArea::subWindowActivated, this, &ModelPanel::updateVisibility);
    updateVisibility();
}

ModelPanel::~ModelPanel() = default;

ModelPanel* ModelPanel::instance()
{
    return panelInstance;
}

void ModelPanel::setupModelCard()
{
    _modelCard = new QFrame(this);
    _modelCard->setObjectName(QStringLiteral("ModelPanelCard"));
    auto layout = new QVBoxLayout(_modelCard);
    layout->setContentsMargins(10, 4, 6, 8);
    layout->setSpacing(2);

    _modelHeader = new QWidget(_modelCard);
    auto header = new QHBoxLayout(_modelHeader);
    header->setContentsMargins(0, 0, 0, 0);

    _tabs = new QTabBar(_modelHeader);
    _tabs->setObjectName(QStringLiteral("ModelPanelTabs"));
    _tabs->setDocumentMode(true);
    _tabs->setDrawBase(false);
    _tabs->setExpanding(false);
    _tabs->addTab(QString());
    _tabs->addTab(QString());
    _tabs->setCurrentIndex(panelParams()->GetInt("CurrentTab", 0) == 1 ? 1 : 0);
    header->addWidget(_tabs);
    header->addStretch();

    _createMenu = new QMenu(this);
    connect(_createMenu, &QMenu::aboutToShow, this, &ModelPanel::populateCreateMenu);
    _createButton = new QToolButton(_modelHeader);
    _createButton->setObjectName(QStringLiteral("ModelPanelButton"));
    _createButton->setIcon(BitmapFactory().iconFromTheme("list-add"));
    _createButton->setAutoRaise(true);
    _createButton->setPopupMode(QToolButton::InstantPopup);
    _createButton->setMenu(_createMenu);
    header->addWidget(_createButton);
    layout->addWidget(_modelHeader);

    _tree = new TreePanel("ComboView", _modelCard);
    layout->addWidget(_tree);

    // A click on the current tab minimizes the panel to its header, which leaves the
    // 3D view almost free. A click on any tab brings it back.
    connect(_tabs, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index >= 0 && index == _tabs->currentIndex()) {
            setMinimized(!_minimized);
        }
        else if (_minimized) {
            setMinimized(false);
        }
    });
    connect(_tabs, &QTabBar::currentChanged, this, [this](int index) {
        panelParams()->SetInt("CurrentTab", index);
        scheduleLayout();
    });
}

void ModelPanel::setupPropertyCard()
{
    _propertyCard = new QFrame(this);
    _propertyCard->setObjectName(QStringLiteral("ModelPanelCard"));
    auto layout = new QVBoxLayout(_propertyCard);
    layout->setContentsMargins(8, 2, 6, 6);
    layout->setSpacing(0);

    _propertyHeader = new QFrame(_propertyCard);
    _propertyHeader->setObjectName(QStringLiteral("ModelPanelSectionHeader"));
    _propertyHeader->setCursor(Qt::PointingHandCursor);
    _propertyHeader->installEventFilter(this);
    auto header = new QHBoxLayout(_propertyHeader);
    header->setContentsMargins(2, 4, 0, 4);
    _propertyTitle = new QLabel(_propertyHeader);
    QFont font = _propertyTitle->font();
    font.setBold(true);
    _propertyTitle->setFont(font);
    header->addWidget(_propertyTitle);
    header->addStretch();
    _collapseButton = new QToolButton(_propertyHeader);
    _collapseButton->setObjectName(QStringLiteral("ModelPanelButton"));
    _collapseButton->setAutoRaise(true);
    connect(_collapseButton, &QToolButton::clicked, this, [this]() {
        setPropertyCollapsed(!_propertyCollapsed);
    });
    header->addWidget(_collapseButton);
    layout->addWidget(_propertyHeader);

    _properties = new PropertyView(_propertyCard);
    // the narrow card has no room for the column titles
    _properties->propertyEditorView->setHeaderHidden(true);
    _properties->propertyEditorData->setHeaderHidden(true);
    layout->addWidget(_properties);
}

void ModelPanel::connectContentSignals()
{
    // Everything that changes the number of visible rows changes the size of the panel
    auto watchModel = [this](QAbstractItemModel* model) {
        if (!model) {
            return;
        }
        connect(model, &QAbstractItemModel::rowsInserted, this, &ModelPanel::scheduleLayout);
        connect(model, &QAbstractItemModel::rowsRemoved, this, &ModelPanel::scheduleLayout);
        connect(model, &QAbstractItemModel::modelReset, this, &ModelPanel::scheduleLayout);
        connect(model, &QAbstractItemModel::layoutChanged, this, &ModelPanel::scheduleLayout);
    };

    if (auto treeWidget = _tree->findChild<TreeWidget*>()) {
        connect(treeWidget, &QTreeWidget::itemExpanded, this, &ModelPanel::scheduleLayout);
        connect(treeWidget, &QTreeWidget::itemCollapsed, this, &ModelPanel::scheduleLayout);
        watchModel(treeWidget->model());
    }
    // e.g. the search box of the tree is shown
    _tree->installEventFilter(this);

    for (QTreeView* editor : {static_cast<QTreeView*>(_properties->propertyEditorView),
                              static_cast<QTreeView*>(_properties->propertyEditorData)}) {
        connect(editor, &QTreeView::expanded, this, &ModelPanel::scheduleLayout);
        connect(editor, &QTreeView::collapsed, this, &ModelPanel::scheduleLayout);
        watchModel(editor->model());
    }
    if (auto tabs = _properties->findChild<QTabWidget*>(QStringLiteral("propertyTab"))) {
        connect(tabs, &QTabWidget::currentChanged, this, &ModelPanel::scheduleLayout);
    }
}

void ModelPanel::setupStyle()
{
    // Same colors as the document bar. Themes do not always set an application palette
    // matching their style sheet, so the menu bar, which every theme styles, decides
    // between light and dark.
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
        const char* card;
        const char* border;
        const char* hover;
        const char* text;
        const char* muted;
        const char* accent;
    };
    const Colors light {"#ffffff", "#d5d9df", "#e2e6eb", "#1f2328", "#6b7280", "#2f6fdf"};
    const Colors darkColors {"#3a3d42", "#4a4e55", "#45494f", "#e6e8eb", "#a0a4ab", "#5b9bff"};
    const Colors& c = dark ? darkColors : light;

    // The panel itself stays transparent, so the rounded corners and the gap between
    // the cards show the 3D view
    setStyleSheet(
        QStringLiteral(
            "#ModelPanelCard { background: %1; border: 1px solid %2; border-radius: 10px; }"
            "#ModelPanelCard QTreeView { background: transparent; border: none; }"
            "#ModelPanelCard QLabel { color: %4; }"
            "#ModelPanelTabs::tab { color: %5; background: transparent; border: none;"
            "  border-bottom: 2px solid transparent; padding: 5px 2px; margin-right: 12px; }"
            "#ModelPanelTabs::tab:selected { color: %4; border-bottom: 2px solid %6; }"
            "#ModelPanelTabs::tab:hover:!selected { color: %4; }"
            "QToolButton#ModelPanelButton { border: none; border-radius: 6px;"
            "  background: transparent; padding: 3px; }"
            "QToolButton#ModelPanelButton:hover { background: %3; }"
            "QToolButton#ModelPanelButton::menu-indicator { image: none; }"
            "#ModelPanelSectionHeader { background: transparent; border: none;"
            "  border-bottom: 1px solid %2; }"
        )
            .arg(QLatin1String(c.card))
            .arg(QLatin1String(c.border))
            .arg(QLatin1String(c.hover))
            .arg(QLatin1String(c.text))
            .arg(QLatin1String(c.muted))
            .arg(QLatin1String(c.accent))
    );
}

void ModelPanel::retranslateUi()
{
    _tabs->setTabText(0, tr("Model"));
    _tabs->setTabText(1, tr("Property"));
    _tabs->setToolTip(tr("Click the current tab to minimize or restore the panel"));
    _createButton->setToolTip(tr("Add to the model"));
    _propertyTitle->setText(tr("Properties"));
    _collapseButton->setToolTip(tr("Show or hide the properties"));
}

void ModelPanel::changeEvent(QEvent* ev)
{
    QWidget::changeEvent(ev);
    if (ev->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    // The theme may have switched between light and dark. Queued, as changing the
    // style sheet from within this event causes further palette changes.
    else if (ev->type() == QEvent::PaletteChange) {
        QMetaObject::invokeMethod(this, &ModelPanel::setupStyle, Qt::QueuedConnection);
    }
}

bool ModelPanel::eventFilter(QObject* source, QEvent* ev)
{
    if (_mdiArea && source == _mdiArea->viewport()) {
        if (ev->type() == QEvent::Resize || ev->type() == QEvent::Move) {
            scheduleLayout();
        }
    }
    else if (source == _tree && ev->type() == QEvent::LayoutRequest) {
        scheduleLayout();
    }
    else if (source == _propertyHeader && ev->type() == QEvent::MouseButtonRelease) {
        // a click anywhere on the header does the same as the chevron
        setPropertyCollapsed(!_propertyCollapsed);
        return true;
    }
    return QWidget::eventFilter(source, ev);
}

void ModelPanel::populateCreateMenu()
{
    // Refilled each time, as the commands of workbenches loaded later become available
    _createMenu->clear();
    auto& manager = Application::Instance->commandManager();
    bool pendingSeparator = false;
    for (const char* name :
         {"PartDesign_Body", "Sketcher_NewSketch", "Separator", "Std_Part", "Std_Group"}) {
        if (std::strcmp(name, "Separator") == 0) {
            pendingSeparator = !_createMenu->isEmpty();
            continue;
        }
        if (Command* cmd = manager.getCommandByName(name)) {
            if (pendingSeparator) {
                _createMenu->addSeparator();
                pendingSeparator = false;
            }
            cmd->addTo(_createMenu);
        }
    }
}

void ModelPanel::showModel()
{
    setMinimized(false);
    _tabs->setCurrentIndex(0);
}

void ModelPanel::showProperties()
{
    setMinimized(false);
    if (_tabs->currentIndex() == 0) {
        setPropertyCollapsed(false);
    }
}

void ModelPanel::setMinimized(bool minimized)
{
    if (_minimized == minimized) {
        return;
    }
    _minimized = minimized;
    panelParams()->SetBool("Minimized", minimized);
    scheduleLayout();
}

void ModelPanel::setPropertyCollapsed(bool collapsed)
{
    if (_propertyCollapsed == collapsed) {
        return;
    }
    _propertyCollapsed = collapsed;
    panelParams()->SetBool("PropertyCollapsed", collapsed);
    scheduleLayout();
}

void ModelPanel::updateVisibility()
{
    // Only views of a document get the panel, not e.g. the start page. The current
    // sub-window is used, not the active one, which is null while another application
    // has the focus.
    QMdiSubWindow* window = _mdiArea ? _mdiArea->currentSubWindow() : nullptr;
    auto view = window ? qobject_cast<MDIView*>(window->widget()) : nullptr;
    _hasDocumentView = view && view->getAppDocument();
    setVisible(_hasDocumentView);
    if (_hasDocumentView) {
        scheduleLayout();
    }
}

void ModelPanel::scheduleLayout()
{
    // Several changes in a row, e.g. expanding a whole subtree, result in a single layout
    if (_layoutPending) {
        return;
    }
    _layoutPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            _layoutPending = false;
            layoutPanel();
        },
        Qt::QueuedConnection
    );
}

int ModelPanel::treeHeight(int limit) const
{
    auto treeWidget = _tree->findChild<TreeWidget*>();
    if (!treeWidget) {
        return MinContent;
    }
    int height = contentHeight(treeWidget, limit);
    auto search = _tree->findChild<QLineEdit*>(QString(), Qt::FindDirectChildrenOnly);
    if (search && search->isVisibleTo(_tree)) {
        height += search->sizeHint().height();
    }
    return height;
}

int ModelPanel::propertyHeight(int limit) const
{
    auto tabs = _properties->findChild<QTabWidget*>(QStringLiteral("propertyTab"));
    if (!tabs) {
        return MinContent;
    }
    auto editor = qobject_cast<QTreeView*>(tabs->currentWidget());
    int height = editor ? contentHeight(editor, limit) : 0;
    // The View and Data tabs and the frame around the pages. Measured once laid out,
    // before that estimated.
    if (editor && editor->height() > 0 && tabs->height() > editor->height()) {
        height += tabs->height() - editor->height();
    }
    else {
        height += tabs->tabBar()->sizeHint().height() + 6;
    }
    return height;
}

void ModelPanel::layoutPanel()
{
    if (!_mdiArea || !_hasDocumentView) {
        return;
    }

    const QRect area = _mdiArea->viewport()->geometry().adjusted(Margin, Margin, -Margin, -Margin);
    if (area.width() <= 0 || area.height() <= 0) {
        return;
    }
    const int width = std::min(
        area.width(),
        std::clamp(area.width() * WidthPercent / 100, MinWidth, MaxWidth)
    );

    // The Model tab shows the tree with the properties below, the Property tab gives the
    // whole height to the properties
    const bool propertyTab = _tabs->currentIndex() == 1;
    const bool showTree = !_minimized && !propertyTab;
    const bool showPropertyCard = !_minimized;
    const bool showProperties = propertyTab || !_propertyCollapsed;

    _tree->setVisible(showTree);
    _propertyCard->setVisible(showPropertyCard);
    _propertyHeader->setVisible(!propertyTab);
    _properties->setVisible(showPropertyCard && showProperties);
    _collapseButton->setArrowType(_propertyCollapsed ? Qt::RightArrow : Qt::DownArrow);

    // Heights the cards want to show everything
    const QMargins modelMargins = _modelCard->layout()->contentsMargins();
    const int modelChrome = modelMargins.top() + modelMargins.bottom()
        + _modelHeader->sizeHint().height() + 2 * CardBorder;
    const QMargins propertyMargins = _propertyCard->layout()->contentsMargins();
    const int propertyChrome = propertyMargins.top() + propertyMargins.bottom()
        + (propertyTab ? 0 : _propertyHeader->sizeHint().height()) + 2 * CardBorder;

    int modelWanted = modelChrome;
    if (showTree) {
        modelWanted += _modelCard->layout()->spacing() + treeHeight(area.height());
    }
    int propertyWanted = 0;
    if (showPropertyCard) {
        propertyWanted = propertyChrome + (showProperties ? propertyHeight(area.height()) : 0);
    }

    // Fit the cards into the 3D view. When both do not fit, the properties get part of
    // the height and the tree the rest, and the space one of them does not need goes to
    // the other. Both scroll within their card then.
    int modelCardHeight = modelWanted;
    int propertyCardHeight = propertyWanted;
    const int space = area.height() - (showPropertyCard ? CardSpacing : 0);
    if (modelCardHeight + propertyCardHeight > space) {
        if (!showPropertyCard) {
            modelCardHeight = std::max(modelChrome, space);
        }
        else if (!showTree) {
            propertyCardHeight = std::max(propertyChrome, space - modelCardHeight);
        }
        else {
            const int propertyMin = propertyChrome + (showProperties ? MinContent : 0);
            propertyCardHeight = std::min(
                propertyWanted,
                std::max(propertyMin, space * PropertyPercent / 100)
            );
            modelCardHeight = std::min(
                modelWanted,
                std::max(modelChrome + MinContent, space - propertyCardHeight)
            );
            propertyCardHeight
                = std::max(propertyChrome, std::min(propertyWanted, space - modelCardHeight));
        }
    }

    _modelCard->setGeometry(0, 0, width, modelCardHeight);
    int height = modelCardHeight;
    QRegion mask(_modelCard->geometry());
    if (showPropertyCard) {
        _propertyCard->setGeometry(0, modelCardHeight + CardSpacing, width, propertyCardHeight);
        height += CardSpacing + propertyCardHeight;
        mask += _propertyCard->geometry();
    }

    const QRect geometry(area.topLeft(), QSize(width, height));
    if (this->geometry() != geometry) {
        setGeometry(geometry);
    }
    // Clicks into the gap between the cards reach the 3D view
    setMask(mask);
    raise();
}

#include "moc_ModelPanel.cpp"
