#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTextStream>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include "calcengine.h"
#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95calc [--scientific]
//        win95calc [--scientific] [--base 16] [--angle rad] --keys "1 2 + 3 ="
//            presses the keys (separated by spaces) and prints the display
//            (for tests/calc/run.sh)
int main(int argc, char *argv[])
{
    installCrashHandler("win95calc");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95calc"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95calc"));

    const QStringList args = app.arguments();
    const bool scientific = args.contains(QStringLiteral("--scientific"));
    const int keysAt = args.indexOf(QStringLiteral("--keys"));
    if (keysAt > 0 && keysAt + 1 < args.size()) {
        CalcEngine calc;
        calc.setScientific(scientific);
        if (const int b = args.indexOf(QStringLiteral("--base")); b > 0 && b + 1 < args.size())
            calc.setBase(args.at(b + 1).toInt());
        if (const int a = args.indexOf(QStringLiteral("--angle")); a > 0 && a + 1 < args.size())
            calc.setAngle(args.at(a + 1));
        for (const QString &k : args.at(keysAt + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (k == QLatin1String("Inv"))
                calc.setInv(true);
            else if (k == QLatin1String("Hyp"))
                calc.setHyp(true);
            else if (k.startsWith(QLatin1String("base=")))
                calc.setBase(k.mid(5).toInt());
            else
                calc.press(k);
        }
        QTextStream(stdout) << calc.display() << '\n';
        return 0;
    }

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("startScientific"), scientific);
    // W95_CALC_DEBUG: the display after each key, on stderr (for the tests)
    engine.rootContext()->setContextProperty(QStringLiteral("calcDebug"), qEnvironmentVariableIsSet("W95_CALC_DEBUG"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Calc/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
