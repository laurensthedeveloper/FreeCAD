// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QColor>
#include <QPointer>
#include <QRect>
#include <QWidget>

#include "Selection/Selection.h"

class QFrame;
class QLabel;
class QMdiArea;
class QMenu;
class QTabBar;
class QToolButton;
class QTreeView;

namespace Gui
{
class PropertyView;
class TreePanel;

/** The model tree or the properties in a card floating over the 3D view.
 *
 * It replaces the Tree view, Property view and Model dock windows, so that the 3D view
 * keeps the full width of the window. The Model tab shows the tree, the Property tab the
 * properties of the selection. The panel sizes itself to that content, limited to the
 * height of the 3D view. It is placed in the top left corner of the viewport of the MDI
 * area and only shown for views of a document.
 */
class ModelPanel: public QWidget, public SelectionObserver
{
    Q_OBJECT

public:
    explicit ModelPanel(QMdiArea* mdiArea);
    ~ModelPanel() override;

    /// The panel, or nullptr if the dock windows are used instead
    static ModelPanel* instance();

    /// Brings the tree into view, e.g. when the panel was minimized
    void showModel();
    /// Brings the properties into view
    void showProperties();

protected:
    bool eventFilter(QObject* source, QEvent* ev) override;
    void changeEvent(QEvent* ev) override;
    void onSelectionChanged(const SelectionChanges& msg) override;

private:
    void setupModelCard();
    void setupPropertyCard();
    void setupStyle();
    void retranslateUi();
    void connectContentSignals();
    void populateCreateMenu();

    void scheduleLayout();
    void layoutPanel();
    void updateVisibility();
    int treeHeight(int limit) const;
    int propertyHeight(int limit, int width) const;
    bool hasProperties() const;
    void updatePropertyTitle();
    void setCardsJoined(bool joined);

    void setMinimized(bool minimized);

    QPointer<QMdiArea> _mdiArea;
    QFrame* _modelCard;
    QWidget* _modelHeader;
    QTabBar* _tabs;
    QToolButton* _createButton;
    QMenu* _createMenu;
    TreePanel* _tree;
    QFrame* _propertyCard;
    QFrame* _propertyHeader;
    QLabel* _propertyIcon;
    QLabel* _propertyTitle;
    PropertyView* _properties;
    QLabel* _placeholder;

    bool _minimized = false;
    bool _hasDocumentView = false;
    bool _layoutPending = false;
    bool _cardsJoined = false;
    QColor _glyphColor;
    int _darkStyle = -1;  // -1: style not set yet
};

}  // namespace Gui
