#include "mainwindow.h"

#include <QApplication>
#include <QFontDatabase>

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    a.setApplicationName(QStringLiteral("Env Vision Monitor"));
    a.setOrganizationName(QStringLiteral("EnvMonitor"));
    QFont font(QStringLiteral("Inter"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const bool hasInter = QFontDatabase::families().contains(QStringLiteral("Inter"));
#else
    const QFontDatabase fontDatabase;
    const bool hasInter = fontDatabase.families().contains(QStringLiteral("Inter"));
#endif
    if (!hasInter)
        font.setFamily(QStringLiteral("PingFang SC"));
    font.setPointSize(13);
    a.setFont(font);
    MainWindow w;
    w.show();
    return QApplication::exec();
}
