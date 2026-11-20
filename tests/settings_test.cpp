#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "settings.h"

namespace {

class SettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void dataDirFromEnvVar();
    void pathGettersAppendFilenames();
    void dataDirDefaultsToStandardLocation();
    void dataDirIsCreatedWhenMissing();
    void layoutAndStyleDirsAreCreated();
    void presetDirFromEnvVar();
    void presetDirFromAppDir();
    void presetDirsAreEmptyWithoutCandidates();

private:
    void setEnvDir(const QString &path);
};

void SettingsTest::initTestCase()
{
    QCoreApplication::setOrganizationName("doob");
    QCoreApplication::setApplicationName("doob");
}

void SettingsTest::cleanup()
{
    qunsetenv("DOOB_DATA_DIR");
    qunsetenv("DOOB_PRESETS_DIR");
    Settings::reset();
}

void SettingsTest::setEnvDir(const QString &path)
{
    qputenv("DOOB_DATA_DIR", path.toUtf8());
    Settings::reset();
}

void SettingsTest::dataDirFromEnvVar()
{
    QTemporaryDir tmp;
    setEnvDir(tmp.path());

    QCOMPARE(Settings::instance()->dataDir(), tmp.path());
}

void SettingsTest::pathGettersAppendFilenames()
{
    QTemporaryDir tmp;
    setEnvDir(tmp.path());
    Settings* s = Settings::instance();

    QCOMPARE(s->dbPath(), tmp.path() + "/doob.db");
    QCOMPARE(s->settingsPath(), tmp.path() + "/settings.json");
    QCOMPARE(s->pluginDataPath("tubemusic"), tmp.path() + "/plugins/tubemusic");
}

void SettingsTest::dataDirDefaultsToStandardLocation()
{
    qunsetenv("DOOB_DATA_DIR");
    Settings::reset();

    QCOMPARE(Settings::instance()->dataDir(),
             QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
}

void SettingsTest::dataDirIsCreatedWhenMissing()
{
    QTemporaryDir base;
    const QString subdir = base.path() + "/doobdata";
    setEnvDir(subdir);

    Settings::instance();
    QVERIFY(QDir(subdir).exists());
}

void SettingsTest::layoutAndStyleDirsAreCreated()
{
    QTemporaryDir base;
    setEnvDir(base.path());

    Settings* s = Settings::instance();
    QCOMPARE(s->layoutsDir(), base.path() + "/layouts");
    QCOMPARE(s->stylesDir(), base.path() + "/styles");
    // Both exist after the instance is resolved (created on demand).
    QVERIFY(QDir(s->layoutsDir()).exists());
    QVERIFY(QDir(s->stylesDir()).exists());
}

void SettingsTest::presetDirFromEnvVar()
{
    QTemporaryDir base;
    const QString presets = base.path() + "/presets";
    QVERIFY(QDir().mkpath(presets));
    qputenv("DOOB_PRESETS_DIR", presets.toUtf8());
    setEnvDir(base.path());

    Settings* s = Settings::instance();
    QCOMPARE(s->presetLayoutsDir(), presets + "/layouts");
    QCOMPARE(s->presetStylesDir(), presets + "/styles");
}

void SettingsTest::presetDirFromAppDir()
{
    // The dev build's layout: CMake copies the repo's presets/ tree to
    // <app dir>/share/doob next to the test executable, and no env var
    // is set — the app-dir candidate must win (and the split-layout
    // candidate must not shadow it).
    qunsetenv("DOOB_PRESETS_DIR");
    const QString appDirPresets =
        QCoreApplication::applicationDirPath() + QStringLiteral("/share/doob");
    if (!QDir(appDirPresets).exists())
        QSKIP("the dev build does not copy the presets tree");

    QTemporaryDir base;
    setEnvDir(base.path());

    Settings* s = Settings::instance();
    QCOMPARE(s->presetLayoutsDir(), appDirPresets + "/layouts");
    QCOMPARE(s->presetStylesDir(), appDirPresets + "/styles");
    // The shipped presets are there (the dialog lists them from here).
    QVERIFY(QFileInfo::exists(appDirPresets + "/layouts/Classic.json"));
}

void SettingsTest::presetDirsAreEmptyWithoutCandidates()
{
    // No env var and no other candidate: the preset accessors report no
    // directory. The app-dir and standard-location candidates may
    // legitimately exist (an installed doob, a dev build with the
    // copied presets), in which case there is nothing to assert and the
    // test is skipped.
    qunsetenv("DOOB_PRESETS_DIR");
    QTemporaryDir base;
    QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/share/doob"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/doob"),
    };
    for (const QString &location :
         QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        if (location + QStringLiteral("/doob") != base.path())
            candidates.append(location + QStringLiteral("/doob"));
    for (const QString &candidate : candidates)
        if (QDir(candidate).exists())
            QSKIP("a non-env preset candidate exists");

    setEnvDir(base.path());

    Settings* s = Settings::instance();
    QVERIFY(s->presetLayoutsDir().isEmpty());
    QVERIFY(s->presetStylesDir().isEmpty());
}
}


QTEST_MAIN(SettingsTest)
#include "settings_test.moc"
