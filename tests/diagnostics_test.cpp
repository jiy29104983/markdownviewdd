#include "diagnostics.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

class DiagnosticsTest final : public QObject
{
    Q_OBJECT

private slots:
    void initializationIsIdempotentAndWindowsAreDistinct();
    void rotatesAtConfiguredCapacity();
    void unwritableLocationDoesNotCrash();
    void pathIdentityDoesNotExposeParentDirectory();
};

void DiagnosticsTest::initializationIsIdempotentAndWindowsAreDistinct()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Diagnostics::configureForTesting(directory.path(), 1024 * 1024);
    Diagnostics::initialize();

    QWidget firstWindow;
    QWidget secondWindow;
    Diagnostics::write(&firstWindow, QStringLiteral("first-window-marker"));
    Diagnostics::initialize();
    Diagnostics::write(&secondWindow, QStringLiteral("second-window-marker"));

    const QString path = Diagnostics::logFilePath();
    QVERIFY(QFileInfo(path).fileName().contains(
        QString::number(QCoreApplication::applicationPid())));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray content = file.readAll();
    QVERIFY(content.contains("first-window-marker"));
    QVERIFY(content.contains("second-window-marker"));
    QVERIFY(content.contains("window-1"));
    QVERIFY(content.contains("window-2"));
}

void DiagnosticsTest::rotatesAtConfiguredCapacity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Diagnostics::configureForTesting(directory.path(), 512);
    Diagnostics::initialize();
    for (int index = 0; index < 40; ++index) {
        Diagnostics::write(nullptr, QStringLiteral("rotation-marker-%1-%2")
                               .arg(index)
                               .arg(QString(48, QLatin1Char('x'))));
    }

    const QString path = Diagnostics::logFilePath();
    QVERIFY(QFileInfo::exists(path));
    for (int generation = 1; generation <= 3; ++generation) {
        QVERIFY(QFileInfo::exists(path + QStringLiteral(".%1").arg(generation)));
    }
    QVERIFY(QFileInfo(path).size() <= 512);
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".4")));
}

void DiagnosticsTest::unwritableLocationDoesNotCrash()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString regularFilePath = directory.filePath(QStringLiteral("not-a-directory"));
    QFile regularFile(regularFilePath);
    QVERIFY(regularFile.open(QIODevice::WriteOnly));
    regularFile.close();

    Diagnostics::configureForTesting(regularFilePath, 512);
    Diagnostics::initialize();
    Diagnostics::write(nullptr, QStringLiteral("silently ignored"), Diagnostics::Level::Error);
    QVERIFY(QFileInfo(regularFilePath).isFile());
}

void DiagnosticsTest::pathIdentityDoesNotExposeParentDirectory()
{
    const QString identity = Diagnostics::pathIdentity(
        QStringLiteral("C:/Users/private/Documents/example.md"));
    QVERIFY(identity.contains(QStringLiteral("example.md")));
    QVERIFY(identity.contains(QStringLiteral("path-id:")));
    QVERIFY(!identity.contains(QStringLiteral("Users")));
    QVERIFY(!identity.contains(QStringLiteral("private")));
}

QTEST_MAIN(DiagnosticsTest)

#include "diagnostics_test.moc"
