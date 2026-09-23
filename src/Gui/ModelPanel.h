// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QPointer>
#include <QRect>
#include <QWidget>

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

/** The model tree and the properties as two cards floating over the 3D view.
 *
 * It replaces the Tree view, Property view and Model dock windows, so that the 3D view
 * keeps the full width of the window. The panel sizes itself to its content: the model
 * card grows with the rows of the tree, the properties card with the properties of the
 * selection, both limited to the height of the 3D view. It is placed in the top left
 * corner of the viewport of the MDI area and only shown for views of a document.
 */
class ModelPanel: public QWidget
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
    int propertyHeight(int limit) const;

    void setMinimized(bool minimized);
    void setPropertyCollapsed(bool collapsed);

    QPointer<QMdiArea> _mdiArea;
    QFrame* _modelCard;
    QWidget* _modelHeader;
    QTabBar* _tabs;
    QToolButton* _createButton;
    QMenu* _createMenu;
    TreePanel* _tree;
    QFrame* _propertyCard;
    QFrame* _propertyHeader;
    QLabel* _propertyTitle;
    QToolButton* _collapseButton;
    PropertyView* _properties;

    bool _minimized = false;
    bool _propertyCollapsed = false;
    bool _hasDocumentView = false;
    bool _layoutPending = false;
    int _darkStyle = -1;  // -1: style not set yet
};

}  // namespace Gui
