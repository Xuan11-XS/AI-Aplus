#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QPalette>
#include <QStyleFactory>
#include <QTimer>
#include <QTranslator>

#include <cstdio>

namespace {

void applyClassicPalette(QApplication &app)
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0xF0, 0xF0, 0xF0));
    palette.setColor(QPalette::WindowText, Qt::black);
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor(0xF5, 0xF5, 0xF5));
    palette.setColor(QPalette::ToolTipBase, QColor(0xFF, 0xFF, 0xE1));
    palette.setColor(QPalette::ToolTipText, Qt::black);
    palette.setColor(QPalette::Text, Qt::black);
    palette.setColor(QPalette::Button, QColor(0xF0, 0xF0, 0xF0));
    palette.setColor(QPalette::ButtonText, Qt::black);
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, QColor(0x00, 0x00, 0xA0));
    palette.setColor(QPalette::Highlight, QColor(0x31, 0x6A, 0xC5));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x78, 0x78, 0x78));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x78, 0x78, 0x78));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x78, 0x78, 0x78));
    app.setPalette(palette);
}

void takeScreenshot(MainWindow &window, const QString &path)
{
    if (!window.grab().save(path))
        std::fprintf(stderr, "无法保存截图：%s\n", qPrintable(path));
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Aplus"));
    QCoreApplication::setApplicationName(QStringLiteral("Aplus"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));

    QTranslator qtTranslator;
    if (qtTranslator.load(QStringLiteral("qtbase_zh_CN"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtTranslator);
    }

    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        app.setStyle(fusion);
    applyClassicPalette(app);

    QFont appFont = app.font();
    appFont.setPointSize(10);
    app.setFont(appFont);
    app.setWindowIcon(QIcon(QStringLiteral(":/aplus.png")));

    const QStringList arguments = app.arguments();
    const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    const int runScreenshotIndex = arguments.indexOf(QStringLiteral("--screenshot-run"));

    MainWindow window;
    if (screenshotIndex >= 0 || runScreenshotIndex >= 0)
        window.resize(1100, 760);
    window.show();

    if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
        const QString path = arguments.at(screenshotIndex + 1);
        QTimer::singleShot(900, &window, [&window, path]() {
            takeScreenshot(window, path);
            QCoreApplication::quit();
        });
    } else if (runScreenshotIndex >= 0 && runScreenshotIndex + 1 < arguments.size()) {
        const QString path = arguments.at(runScreenshotIndex + 1);
        QTimer::singleShot(250, &window, [&window]() {
            if (QAction *action = window.findChild<QAction *>(QStringLiteral("compileRunAction")))
                action->trigger();
        });
        QTimer::singleShot(5000, &window, [&window, path]() {
            takeScreenshot(window, path);
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
