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

#include <vector>

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPainter>
#include <QShortcut>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWindow>

#include <App/Application.h>

#include "RibbonTitleBar.h"
#include "Action.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "CommandCompleter.h"

#if defined(Q_OS_WIN)
# include <qpa/qplatformwindow_p.h>
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
# include <windowsx.h>
#endif

using namespace Gui;

namespace
{
// File and edit commands available on every tab
const std::vector<const char*> quickAccessCommands {
    "Std_New",
    "Std_Open",
    "Std_Save",
    "Std_Export",
    "Separator",
    "Std_Undo",
    "Std_Redo",
    "Std_Cut",
    "Std_Copy",
    "Std_Paste",
    "Std_Delete",
    "Separator",
    "Std_Refresh",
};

// Line icons of the Windows icon font for the quick access commands. They are clearly
// distinguishable at a small size, unlike the filled shapes of the command icons.
const std::map<std::string, char16_t> quickAccessGlyphs {
    {"Std_New", 0xE8A5},      // Document
    {"Std_Open", 0xE8E5},     // OpenFile
    {"Std_Save", 0xE74E},     // Save
    {"Std_Export", 0xE898},   // Upload
    {"Std_Undo", 0xE7A7},     // Undo
    {"Std_Redo", 0xE7A6},     // Redo
    {"Std_Cut", 0xE8C6},      // Cut
    {"Std_Copy", 0xE8C8},     // Copy
    {"Std_Paste", 0xE77F},    // Paste
    {"Std_Delete", 0xE74D},   // Delete
    {"Std_Refresh", 0xE72C},  // Refresh
};

// The icon font of Windows 11, or of Windows 10, or an empty string if there is none
QString iconFontFamily()
{
    const QStringList families = QFontDatabase::families();
    for (const QString& family : {QStringLiteral("Segoe Fluent Icons"), QStringLiteral("Segoe MDL2 Assets")}) {
        if (families.contains(family)) {
            return family;
        }
    }
    return {};
}

bool customFrameEnabled()
{
#if defined(Q_OS_WIN)
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/MainWindow"
    );
    return hGrp->GetBool("RibbonCustomTitleBar", true);
#else
    return false;
#endif
}
}  // namespace

// -----------------------------------------------------------

#if defined(Q_OS_WIN)

namespace
{
int frameThickness(HWND hwnd)
{
    const UINT dpi = GetDpiForWindow(hwnd);
    return GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

int captionHeight(HWND hwnd)
{
    return GetSystemMetricsForDpi(SM_CYCAPTION, GetDpiForWindow(hwnd));
}
}  // namespace

/**
 * Tells Windows which parts of the main window act as caption and top resize border,
 * now that the system caption is removed.
 */
class RibbonTitleBar::NativeFilter: public QAbstractNativeEventFilter
{
public:
    NativeFilter(RibbonTitleBar* bar, HWND hwnd)
        : _bar(bar)
        , _hwnd(hwnd)
    {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override
    {
        if (eventType != "windows_generic_MSG") {
            return false;
        }
        auto msg = static_cast<MSG*>(message);
        if (msg->hwnd != _hwnd) {
            return false;
        }

        switch (msg->message) {
            case WM_NCHITTEST:
                return hitTest(msg, result);
            case WM_DPICHANGED:
                // The caption and frame sizes depend on the DPI
                QTimer::singleShot(0, _bar, [bar = _bar] { bar->updateCustomFrame(); });
                break;
            default:
                break;
        }
        return false;
    }

private:
    bool hitTest(const MSG* msg, qintptr* result) const
    {
        POINT point {GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        ScreenToClient(_hwnd, &point);
        RECT client;
        GetClientRect(_hwnd, &client);
        if (!PtInRect(&client, point)) {
            return false;  // the remaining system frame handles itself
        }

        // The top resize border, which is part of the client area now
        const int frame = frameThickness(_hwnd);
        if (!IsZoomed(_hwnd) && point.y < frame) {
            if (point.x < 2 * frame) {
                *result = HTTOPLEFT;
            }
            else if (point.x >= client.right - 2 * frame) {
                *result = HTTOPRIGHT;
            }
            else {
                *result = HTTOP;
            }
            return true;
        }

        QWidget* window = _bar->window();
        const qreal ratio = window->devicePixelRatioF();
        const QPoint pos(qRound(point.x / ratio), qRound(point.y / ratio));
        if (_bar->isCaption(window->childAt(pos), pos)) {
            *result = HTCAPTION;
            return true;
        }
        return false;
    }

    RibbonTitleBar* _bar;
    HWND _hwnd;
};

#else

class RibbonTitleBar::NativeFilter
{
};

#endif

// -----------------------------------------------------------

RibbonTitleBar::RibbonTitleBar(QWidget* parent)
    : QWidget(parent)
    , _logo(new QLabel(this))
{
    setObjectName(QStringLiteral("RibbonTitleBar"));
    // Only the owner of the parts placed into the menu bar, not shown itself
    hide();

    // Left of the menus: logo and small quick access buttons
    _leftPart = new QWidget(this);
    _leftPart->setObjectName(QStringLiteral("RibbonTitleLeft"));
    auto left = new QHBoxLayout(_leftPart);
    left->setContentsMargins(8, 0, 4, 0);
    left->setSpacing(4);

    _logo->setObjectName(QStringLiteral("RibbonLogo"));
    _logo->setPixmap(BitmapFactory().iconFromTheme("freecad").pixmap(QSize(16, 16)));
    left->addWidget(_logo);
    setupQuickAccess(left);

    // Right of the menus: search, help and the window buttons. The window title is not
    // shown, the documents have their tabs in the document bar and the menus need the
    // space.
    _rightPart = new QWidget(this);
    _rightPart->setObjectName(QStringLiteral("RibbonTitleRight"));
    auto right = new QHBoxLayout(_rightPart);
    right->setContentsMargins(8, 0, 0, 0);
    right->setSpacing(6);

    setupSearchAndHelp(right);

    if (customFrameEnabled()) {
        setupWindowButtons(right);
        // The native window only exists once the main window is shown
        QMetaObject::invokeMethod(this, &RibbonTitleBar::enableCustomFrame, Qt::QueuedConnection);
    }

    if (QWidget* window = parent ? parent->window() : nullptr) {
        window->installEventFilter(this);
    }

    placeInMenuBar();
    if (QMenuBar* bar = menuBar()) {
        bar->installEventFilter(this);
    }
    // Once the theme is fully applied, which is only when the main window is shown
    QMetaObject::invokeMethod(this, &RibbonTitleBar::updateQuickAccessIcons, Qt::QueuedConnection);
}

QMenuBar* RibbonTitleBar::menuBar() const
{
    auto mainWindow = qobject_cast<QMainWindow*>(window());
    return mainWindow ? mainWindow->menuBar() : nullptr;
}

void RibbonTitleBar::placeInMenuBar()
{
    // The menu bar of the main window becomes the title row: its menus stay where they
    // are, the parts of the title row go into its corners. Widgets already in a corner
    // (toolbar areas of the menu bar) are kept next to them.
    QMenuBar* bar = menuBar();
    if (!bar) {
        return;
    }

    auto wrap = [bar](Qt::Corner corner, QWidget* part, bool partFirst) {
        auto container = new QWidget(bar);
        container->setObjectName(
            corner == Qt::TopLeftCorner ? QStringLiteral("RibbonTitleLeftCorner")
                                        : QStringLiteral("RibbonTitleRightCorner")
        );
        // Both corners as high as the window buttons, so that they line up with the menus
        container->setFixedHeight(32);
        auto layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        QWidget* existing = bar->cornerWidget(corner);
        if (partFirst) {
            layout->addWidget(part);
        }
        if (existing) {
            layout->addWidget(existing);
        }
        if (!partFirst) {
            layout->addWidget(part);
        }
        bar->setCornerWidget(container, corner);
        container->show();
    };

    wrap(Qt::TopLeftCorner, _leftPart, true);
    wrap(Qt::TopRightCorner, _rightPart, false);
}

RibbonTitleBar::~RibbonTitleBar()
{
#if defined(Q_OS_WIN)
    if (_nativeFilter) {
        qApp->removeNativeEventFilter(_nativeFilter.get());
    }
#endif
}

void RibbonTitleBar::setupQuickAccess(QHBoxLayout* layout)
{
    // Small monochrome buttons for the file and edit commands used on every tab. They
    // are own buttons rather than the actions of the commands, as those carry the
    // colored icons. A click triggers the action of the command, and its enabled state
    // and tool tip are followed.
    auto container = new QWidget(this);
    container->setObjectName(QStringLiteral("RibbonQuickAccess"));
    auto row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    auto& commandManager = Application::Instance->commandManager();
    for (const char* name : quickAccessCommands) {
        if (qstrcmp(name, "Separator") == 0) {
            auto line = new QFrame(container);
            line->setObjectName(QStringLiteral("RibbonQuickAccessSeparator"));
            line->setFixedSize(1, 14);
            row->addSpacing(4);
            row->addWidget(line);
            row->addSpacing(4);
            continue;
        }

        Command* command = commandManager.getCommandByName(name);
        if (!command) {
            continue;
        }
        command->initAction();
        QAction* action = command->getAction() ? command->getAction()->action() : nullptr;
        if (!action) {
            continue;
        }

        auto button = new QToolButton(container);
        button->setObjectName(QStringLiteral("RibbonQuickAccessButton"));
        button->setIconSize(QSize(quickIconSize, quickIconSize));
        button->setFixedSize(22, 22);
        button->setToolTip(action->toolTip());
        button->setEnabled(action->isEnabled());
        connect(button, &QToolButton::clicked, action, &QAction::trigger);
        connect(action, &QAction::changed, button, [button, action] {
            button->setEnabled(action->isEnabled());
            button->setToolTip(action->toolTip());
        });
        row->addWidget(button);
        _quickButtons.push_back({button, action, name});
    }

    layout->addWidget(container);
    updateQuickAccessIcons();
}

void RibbonTitleBar::updateQuickAccessIcons()
{
    // One color for all icons, the text color of the title row: dark on a light theme
    // and light on a dark one. The palette of the menu bar follows the style sheet.
    QColor color(0x1f, 0x23, 0x28);
    if (QMenuBar* bar = menuBar()) {
        bar->ensurePolished();
        if (bar->palette().color(bar->backgroundRole()).lightness() < 128) {
            color = QColor(0xe6, 0xe8, 0xeb);
        }
    }

    const qreal ratio = devicePixelRatioF();
    for (const auto& [button, action] : _quickButtons) {
        if (!button || !action) {
            continue;
        }
        // Keep only the shape of the icon and fill it with the color
        QPixmap shape = action->icon().pixmap(QSize(quickIconSize, quickIconSize), ratio);
        QPixmap pixmap(shape.size());
        pixmap.setDevicePixelRatio(shape.devicePixelRatio());
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.drawPixmap(0, 0, shape);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        painter.end();
        button->setIcon(QIcon(pixmap));
    }
}

void RibbonTitleBar::setupSearchAndHelp(QHBoxLayout* layout)
{
    // Command search, like the command palette of other applications
    auto search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("RibbonSearch"));
    search->setPlaceholderText(tr("Search commands (Ctrl+K)"));
    search->setToolTip(tr("Type at least three characters to find a command, "
                          "press Enter to run it"));
    search->setClearButtonEnabled(true);
    search->setFixedWidth(200);
    search->addAction(searchIcon(), QLineEdit::LeadingPosition);

    auto completer = new CommandCompleter(search, search);
    connect(completer, &CommandCompleter::commandActivated, this, [search](const QByteArray& name) {
        search->clear();
        search->clearFocus();
        Application::Instance->commandManager().runCommandByName(name.constData());
    });

    auto shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, search, [search] {
        search->setFocus(Qt::ShortcutFocusReason);
        search->selectAll();
    });
    layout->addWidget(search);

    // Help, the same as Help > Help (F1)
    auto help = new QToolButton(this);
    help->setObjectName(QStringLiteral("RibbonHelpButton"));
    help->setToolTip(tr("Opens the Help documentation"));
    help->setIcon(BitmapFactory().iconFromTheme("help-browser"));
    help->setIconSize(QSize(18, 18));
    connect(help, &QToolButton::clicked, this, [] {
        Application::Instance->commandManager().runCommandByName("Std_OnlineHelp");
    });
    layout->addWidget(help);
}

void RibbonTitleBar::setupWindowButtons(QHBoxLayout* layout)
{
    // The glyphs of the Windows caption buttons, from the icon font of Windows 11 or 10
    QFont font(iconFontFamily());
    font.setPointSizeF(7.5);

    _windowButtons = new QWidget(this);
    auto buttonLayout = new QHBoxLayout(_windowButtons);
    buttonLayout->setContentsMargins(8, 0, 0, 0);
    buttonLayout->setSpacing(0);

    auto addButton = [&](const QString& name, QChar glyph, const QString& tip, auto slot) {
        auto button = new QToolButton(_windowButtons);
        button->setObjectName(name);
        button->setProperty("windowButton", true);
        button->setFont(font);
        button->setText(glyph);
        button->setToolTip(tip);
        button->setFixedSize(46, 32);
        connect(button, &QToolButton::clicked, this, slot);
        buttonLayout->addWidget(button);
        return button;
    };

    addButton(QStringLiteral("RibbonMinimizeButton"), QChar(0xE921), tr("Minimize"), [this] {
        window()->showMinimized();
    });
    _maximizeButton = addButton(QStringLiteral("RibbonMaximizeButton"), QChar(0xE922), tr("Maximize"), [this] {
        window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
    });
    addButton(QStringLiteral("RibbonCloseButton"), QChar(0xE8BB), tr("Close"), [this] {
        window()->close();
    });

    layout->addWidget(_windowButtons, 0, Qt::AlignTop);
    _windowButtons->hide();  // shown once the system caption is removed
}

void RibbonTitleBar::enableCustomFrame()
{
#if defined(Q_OS_WIN)
    QWidget* window = this->window();
    if (_customFrame || !window->isWindow() || !window->windowHandle()) {
        return;
    }

    _customFrame = true;
    auto hwnd = reinterpret_cast<HWND>(window->winId());
    _nativeFilter = std::make_unique<NativeFilter>(this, hwnd);
    qApp->installNativeEventFilter(_nativeFilter.get());

    _windowButtons->show();
    updateCustomFrame();
#endif
}

void RibbonTitleBar::updateCustomFrame()
{
#if defined(Q_OS_WIN)
    QWidget* window = this->window();
    QWindow* handle = window->windowHandle();
    if (!_customFrame || !handle) {
        return;
    }

    using QNativeInterface::Private::QWindowsWindow;
    auto windowsWindow = handle->nativeInterface<QWindowsWindow>();
    if (!windowsWindow) {
        return;
    }

    // Remove the caption, and the top frame too unless maximized: a maximized window
    // extends beyond the screen by the frame, so only the caption must go. Full screen
    // windows have no frame at all.
    auto hwnd = reinterpret_cast<HWND>(window->winId());
    QMargins margins;
    if (!window->isFullScreen()) {
        int top = captionHeight(hwnd);
        if (!window->isMaximized()) {
            top += frameThickness(hwnd);
        }
        margins = QMargins(0, -top, 0, 0);
    }
    if (windowsWindow->customMargins() != margins) {
        windowsWindow->setCustomMargins(margins);
    }
    _windowButtons->setVisible(!window->isFullScreen());
    updateWindowButtons();
#endif
}

void RibbonTitleBar::updateWindowButtons()
{
    if (!_maximizeButton) {
        return;
    }
    const bool maximized = window()->isMaximized();
    _maximizeButton->setText(QChar(maximized ? 0xE923 : 0xE922));
    _maximizeButton->setToolTip(maximized ? tr("Restore") : tr("Maximize"));
}

bool RibbonTitleBar::isCaption(const QWidget* child, const QPoint& posInWindow) const
{
    // Only the height of the title row, which is the menu bar, and there only the parts
    // without controls
    QMenuBar* bar = menuBar();
    if (!bar) {
        return false;
    }
    const QRect rect(bar->mapTo(window(), QPoint(0, 0)), bar->size());
    if (posInWindow.y() > rect.bottom()) {
        return false;
    }

    // The menu bar itself, except on its menus
    if (child == bar) {
        return !bar->actionAt(bar->mapFrom(window(), posInWindow));
    }

    return !child || child == window() || child == _logo || child == _windowButtons || child == _leftPart || child == _rightPart
        || child->objectName().startsWith(QLatin1String("RibbonTitle"))
        || (qobject_cast<const QToolBar*>(child) && !_leftPart->isAncestorOf(child));
}

bool RibbonTitleBar::eventFilter(QObject* source, QEvent* ev)
{
    if (source == window() && ev->type() == QEvent::WindowStateChange) {
        // Queued, as the frame must not change from within this event
        QMetaObject::invokeMethod(this, &RibbonTitleBar::updateCustomFrame, Qt::QueuedConnection);
    }
    // The theme may have changed between light and dark
    if (source == menuBar() && ev->type() == QEvent::PaletteChange) {
        QMetaObject::invokeMethod(this, &RibbonTitleBar::updateQuickAccessIcons, Qt::QueuedConnection);
    }
    return QWidget::eventFilter(source, ev);
}

QIcon RibbonTitleBar::searchIcon() const
{
    // A magnifier drawn in the placeholder text color, so it matches the theme
    const qreal ratio = devicePixelRatioF();
    QPixmap pixmap(QSize(16, 16) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::PlaceholderText), 1.6, Qt::SolidLine, Qt::RoundCap));
    painter.drawEllipse(QRectF(2.0, 2.0, 8.5, 8.5));
    painter.drawLine(QPointF(9.5, 9.5), QPointF(14.0, 14.0));
    painter.end();

    return QIcon(pixmap);
}

#include "moc_RibbonTitleBar.cpp"
