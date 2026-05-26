#include "MainWindowSetup.h"

#include "../controllers/AppSettingsController.h"
#include "../controllers/ThemeController.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#endif

namespace {
constexpr const char *AppIconPath = ":/qt/qml/FM/qml/assets/icons/app_icon.png";
QString appIconPath()
{
    return QString::fromLatin1(AppIconPath);
}

bool shouldStartMaximized(AppSettingsController *settings)
{
    return settings && settings->workspaceState().value(QStringLiteral("windowMaximized"), false).toBool();
}
}

void MainWindowSetup::configureProcessIdentity()
{
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"FM.FileManager.1.0");
#endif
}

void MainWindowSetup::configureApplication(QApplication &app)
{
    Q_UNUSED(app);
    QApplication::setApplicationName(QStringLiteral("FM"));
    QApplication::setOrganizationName(QStringLiteral("FM"));
    QGuiApplication::setWindowIcon(QIcon(appIconPath()));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
}

void MainWindowSetup::configureMainWindow(QQuickWindow *window, ThemeController *theme, AppSettingsController *settings)
{
    if (!window || !theme) {
        return;
    }

    window->setIcon(QIcon(appIconPath()));
    window->setColor(theme->bg());
    window->setOpacity(0.0);

    QObject::connect(theme, &ThemeController::themeChanged, window, [theme, window]() {
        if (window) {
            window->setColor(theme->bg());
        }
    });

    const QVariantMap state = settings ? settings->workspaceState() : QVariantMap();
    const QVariantMap geometry = settings
        ? settings->sanitizedWindowGeometry(state, 1120, 720)
        : QVariantMap();

    if (geometry.value(QStringLiteral("valid")).toBool()) {
        window->setGeometry(QRect(
            geometry.value(QStringLiteral("x")).toInt(),
            geometry.value(QStringLiteral("y")).toInt(),
            geometry.value(QStringLiteral("width")).toInt(),
            geometry.value(QStringLiteral("height")).toInt()));
    } else {
        const QSize targetSize(1120, 720);
        const QRect screenRect = QGuiApplication::primaryScreen()
            ? QGuiApplication::primaryScreen()->availableGeometry()
            : QRect(0, 0, targetSize.width(), targetSize.height());
        const QPoint targetTopLeft = screenRect.center() - QPoint(targetSize.width() / 2, targetSize.height() / 2);
        window->setGeometry(QRect(targetTopLeft, targetSize));
    }

    if (state.value(QStringLiteral("windowMaximized"), false).toBool()) {
        window->setWindowStates(window->windowStates() | Qt::WindowMaximized);
    }
}

void MainWindowSetup::showMainWindow(QQuickWindow *window, AppSettingsController *settings)
{
    if (!window) {
        return;
    }

    if (shouldStartMaximized(settings)) {
        window->showMaximized();
        return;
    }

    window->show();
}
