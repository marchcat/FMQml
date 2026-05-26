#pragma once

class QApplication;
class AppSettingsController;
class QQuickWindow;
class ThemeController;

namespace MainWindowSetup {
void configureProcessIdentity();
void configureApplication(QApplication &app);
void configureMainWindow(QQuickWindow *window, ThemeController *theme, AppSettingsController *settings);
void showMainWindow(QQuickWindow *window, AppSettingsController *settings);
}
