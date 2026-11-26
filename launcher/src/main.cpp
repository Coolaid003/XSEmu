#include "app/DevLever.h"
#include "app/Services.h"
#include "shell/MainWindow.h"
#include "theme/Theme.h"
#include "widgets/Icon.h"

#include <QApplication>
#include <QTextStream>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("XSEmuLauncher"));
    QApplication::setOrganizationName(QStringLiteral("XSEmu"));
    QApplication::setApplicationDisplayName(QStringLiteral("XSEmu"));
    QApplication::setApplicationVersion(QStringLiteral(XSEMU_VERSION));

#ifdef XSEMU_DEV_TOOLS
    DevLever::Options lever;
    QString leverError;
    const bool useLever = DevLever::parse(QApplication::arguments(), &lever, &leverError);
    if (!leverError.isEmpty()) {
        QTextStream(stderr) << leverError << Qt::endl;
        return 2;
    }
    if (lever.listStates) {
        QTextStream(stdout) << DevLever::states().join(QLatin1Char('\n')) << Qt::endl;
        return 0;
    }
    if (useLever && !DevLever::prepare(&leverError)) {
        QTextStream(stderr) << leverError << Qt::endl;
        return 2;
    }
#endif

    Theme::instance().install(app);
    QApplication::setWindowIcon(Icon::appIcon());
    Services services;
    MainWindow window(services);

#ifdef XSEMU_DEV_TOOLS
    if (useLever) {
        DevLever::apply(lever, services, window);
        return QApplication::exec();
    }
#endif

    window.resize(Metrics::WindowWidth, 800);
    window.show();
    return QApplication::exec();
}
