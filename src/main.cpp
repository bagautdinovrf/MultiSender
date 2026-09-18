#include "mainwindow.h"
#include "appversion.h"
#include "apptheme.h"
#include "settingsstore.h"
#include "uilanguage.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MultiSender"));
    QCoreApplication::setApplicationName(QStringLiteral("MultiSender"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MULTISENDER_VERSION));

    QApplication::setWindowIcon(QIcon(QStringLiteral(":/Installer/MultiSender.ico")));
    application.setQuitOnLastWindowClosed(false);
    int result = 0;
    do {
        const ApplicationSettings settings = SettingsStore().load();
        if (!UiLanguage::apply(settings.language)) UiLanguage::apply(QStringLiteral("ru"));
        QApplication::setLayoutDirection(UiLanguage::current() == QStringLiteral("ar")
                                            ? Qt::RightToLeft : Qt::LeftToRight);
        AppTheme::apply(AppTheme::fromName(settings.theme));
        MainWindow window;
        window.show();
        result = application.exec();
    } while (result == UiLanguage::RestartExitCode);
    return result;
}
