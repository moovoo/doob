#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "settings.h"
#include "settingsmanager.h"
#include "stylemanager.h"

namespace {

// The style store: the user's styles directory, the read-only presets
// (used as-is — the copy is taken on write, when the live sheet
// diverges from the preset's), the active style (the "style.current"
// setting) and the save of the current style.

class StyleStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testListStyles();
    void testUserFileShadowsPreset();
    void testCurrentStylePersistence();
    void testSwitchStyle();
    void testSwitchToPresetUsesPresetItself();
    void testNewStyleSavesCurrentFirst();
    void testSwitchUnknownStyleRefused();
    void testCowForksEditedPreset();
    void testCowLeavesUneditedPresetAlone();
    void testSwitchAwayFromEditedPresetForks();
    void testSaveStyleAs();
    void testDeleteStyle();
    void testLoadCurrentStyle();
    void testStaleCurrentFallsBackToDefault();

private:
    void writePreset(const QString &name, const QString &text);
    void writeUser(const QString &name, const QString &text);

    QTemporaryDir m_data;
    QTemporaryDir m_presets;
};

void StyleStoreTest::init()
{
    m_data.remove();
    m_data = QTemporaryDir();
    m_presets.remove();
    m_presets = QTemporaryDir();
    qputenv("DOOB_DATA_DIR", m_data.path().toUtf8());
    qputenv("DOOB_PRESETS_DIR", m_presets.path().toUtf8());
    Settings::reset();
    StyleManager::reset();
    SettingsManager::getInstance()->resetToDefault("style.current");
}

void StyleStoreTest::cleanup()
{
    SettingsManager::getInstance()->resetToDefault("style.current");
    StyleManager::reset();
    qApp->setStyleSheet(QString());
    qunsetenv("DOOB_DATA_DIR");
    qunsetenv("DOOB_PRESETS_DIR");
    Settings::reset();
}

void StyleStoreTest::writePreset(const QString &name, const QString &text)
{
    QDir().mkpath(m_presets.path() + "/styles");
    QFile f(m_presets.path() + "/styles/" + name + ".qss");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text.toUtf8());
}

void StyleStoreTest::writeUser(const QString &name, const QString &text)
{
    QDir().mkpath(m_data.path() + "/styles");
    QFile f(m_data.path() + "/styles/" + name + ".qss");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text.toUtf8());
}

void StyleStoreTest::testListStyles()
{
    writePreset("dark", "QLabel { color: black; }");
    writePreset("light", "QLabel { color: white; }");
    writeUser("mine", "QLabel { color: red; }");

    const QList<StyleManager::Info> styles = StyleManager::getInstance()->styles();
    // The user's files and the presets, sorted by title
    // (case-insensitive: "dark" < "light" < "mine").
    QCOMPARE(styles.size(), 3);
    QStringList names;
    for (const StyleManager::Info &info : styles)
        names.append(info.name);
    const QStringList expected = {QString("dark"),
                                  QString("light"), QString("mine"),};
    QCOMPARE(names, expected);
    for (const StyleManager::Info &info : styles)
        QCOMPARE(info.readOnly, info.name == "dark" || info.name == "light");
}

void StyleStoreTest::testUserFileShadowsPreset()
{
    writePreset("dark", "preset text");
    writeUser("dark", "user text");

    // The listing has one "dark" (the user's), and the user's text
    // wins on read.
    const QList<StyleManager::Info> styles = StyleManager::getInstance()->styles();
    int darks = 0;
    for (const StyleManager::Info &info : styles)
        if (info.name == "dark") {
            ++darks;
            QCOMPARE(info.readOnly, false);
        }
    QCOMPARE(darks, 1);
    QCOMPARE(StyleManager::getInstance()->styleText("dark"), QString("user text"));
    QVERIFY(!StyleManager::getInstance()->isPreset("dark"));
}

void StyleStoreTest::testCurrentStylePersistence()
{
    StyleManager* sm = StyleManager::getInstance();
    QCOMPARE(sm->currentStyle(), QString());

    sm->setCurrentStyle("dark");
    QCOMPARE(sm->currentStyle(), QString("dark"));
    // The name rides in the SettingsManager (it survives a restart,
    // which is what loadCurrentStyle reads).
    QCOMPARE(SettingsManager::getInstance()->value("style.current").toString(),
             QString("dark"));

    sm->setCurrentStyle(QString());
    QCOMPARE(sm->currentStyle(), QString());
}

void StyleStoreTest::testSwitchStyle()
{
    writePreset("dark", "QLabel { color: black; }");
    writeUser("mine", "QLabel { color: red; }");
    StyleManager* sm = StyleManager::getInstance();
    sm->setCurrentStyle(QString());

    // A preset is used as-is: the preset's text is applied, the
    // preset itself becomes current, no copy is taken.
    QVERIFY(sm->switchStyle("dark"));
    QCOMPARE(sm->currentStyle(), QString("dark"));
    QCOMPARE(sm->stylesheet(), QString("QLabel { color: black; }"));
    QCOMPARE(qApp->styleSheet(), QString("QLabel { color: black; }"));
    QVERIFY(!QFile::exists(m_data.path() + "/styles/dark.modified.qss"));
    QVERIFY(!QFile::exists(m_data.path() + "/styles/dark.qss"));

    QVERIFY(sm->switchStyle("mine"));
    QCOMPARE(sm->currentStyle(), QString("mine"));
    QCOMPARE(sm->stylesheet(), QString("QLabel { color: red; }"));

    // "default" is a plain name: without a file there is nothing to
    // switch to (it is not the platform style's special entry).
    QVERIFY(!sm->switchStyle("default"));
    QCOMPARE(sm->currentStyle(), QString("mine"));
    QCOMPARE(sm->stylesheet(), QString("QLabel { color: red; }"));

    // With a user file named "default", it switches like any other
    // file.
    writeUser("default", "QLabel { color: green; }");
    QVERIFY(sm->switchStyle("default"));
    QCOMPARE(sm->currentStyle(), QString("default"));
    QCOMPARE(sm->stylesheet(), QString("QLabel { color: green; }"));

    // Switching to it while already on it is a no-op.
    QVERIFY(sm->switchStyle("default"));
    QCOMPARE(sm->currentStyle(), QString("default"));
    QCOMPARE(sm->stylesheet(), QString("QLabel { color: green; }"));
}

// Selecting a preset uses the preset itself — even when a user copy
// of it exists: the preset's text is applied (not the copy's), the
// preset becomes current, the copy stays untouched, no second copy is
// created, and the previous style is saved with its diverged sheet.
void StyleStoreTest::testSwitchToPresetUsesPresetItself()
{
    writePreset("dark", "QLabel { color: black; }");
    // The user's copy of the preset, diverged from it.
    writeUser("dark.modified",
              "/* doob-origin: preset:dark */\nQLabel { color: blue; }");
    // A user style as the current, with a diverged live sheet.
    writeUser("mine", "QLabel { color: red; }");
    StyleManager* sm = StyleManager::getInstance();
    sm->setCurrentStyle("mine");
    sm->setStyleSheet("QLabel { color: green; }");

    QVERIFY(sm->switchStyle("dark"));
    QCOMPARE(sm->currentStyle(), QString("dark"));
    // The preset's own text is applied, not the copy's.
    QCOMPARE(sm->stylesheet(), QString("QLabel { color: black; }"));
    // No new copy was created; the existing one is untouched.
    QVERIFY(!QFile::exists(m_data.path() + "/styles/dark.modified-2.qss"));
    QFile copy(m_data.path() + "/styles/dark.modified.qss");
    QVERIFY(copy.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(copy.readAll()),
             QString("/* doob-origin: preset:dark */\nQLabel { color: blue; }"));
    copy.close();
    // The previous style was saved with its diverged sheet.
    QFile f(m_data.path() + "/styles/mine.qss");
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(f.readAll()), QString("QLabel { color: green; }"));
    f.close();
}

// New style: the current style is saved as-is first (a no-op for an
// unset current — there is no file to write to) — then the fresh empty
// style is created and made current.
void StyleStoreTest::testNewStyleSavesCurrentFirst()
{
    StyleManager* sm = StyleManager::getInstance();
    sm->setCurrentStyle(QString());
    sm->setStyleSheet("QLabel { color: red; }"); // the unsaved sheet

    QVERIFY(sm->newStyle("fresh"));
    QCOMPARE(sm->currentStyle(), QString("fresh"));
    // Nothing was materialized for the unset current.
    QVERIFY(!QFile::exists(m_data.path() + "/styles/default.qss"));
    // The new style is empty and current.
    QVERIFY(QFile::exists(m_data.path() + "/styles/fresh.qss"));
    QCOMPARE(sm->styleText("fresh"), QString());
    QCOMPARE(sm->stylesheet(), QString());
}

void StyleStoreTest::testSwitchUnknownStyleRefused()
{
    StyleManager* sm = StyleManager::getInstance();
    sm->setCurrentStyle(QString());
    sm->setStyleSheet("QLabel { color: red; }");

    QVERIFY(!sm->switchStyle("nosuchstyle"));
    // The current is left untouched (no file was written for it).
    QCOMPARE(sm->currentStyle(), QString());
    QCOMPARE(sm->stylesheet(), QString("QLabel { color: red; }"));
    QVERIFY(!QFile::exists(m_data.path() + "/styles/nosuchstyle.qss"));
}

// The copy-on-write of an edited preset current: the save forks the
// live sheet to "<name>.modified.qss" (with the origin comment) and
// makes the fork current; the read-only preset is untouched. An
// existing copy is written over with the live sheet (the sheet the
// user just approved is the state) — no second copy is created.
void StyleStoreTest::testCowForksEditedPreset()
{
    writePreset("dark", "QLabel { color: black; }");
    StyleManager* sm = StyleManager::getInstance();
    sm->setCurrentStyle("dark");
    sm->setStyleSheet("QLabel { color: blue; }"); // the edit

    QCOMPARE(sm->saveCurrentStyle(), QString("dark.modified"));
    QCOMPARE(sm->currentStyle(), QString("dark.modified"));

    const QString fork = m_data.path() + "/styles/dark.modified.qss";
    QVERIFY(QFile::exists(fork));
    QFile f(fork);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(content.startsWith("/* doob-origin: preset:dark */"));
    QVERIFY(content.contains("QLabel { color: blue; }"));

    // The preset itself was not rewritten.
    QCOMPARE(sm->styleText("dark"), QString("QLabel { color: black; }"));

    // Back on the preset with a new edit: the existing copy is
    // written over with the live sheet, not a second copy is made.
    sm->setCurrentStyle("dark");
    sm->setStyleSheet("QLabel { color: green; }");
    QCOMPARE(sm->saveCurrentStyle(), QString("dark.modified"));
    QCOMPARE(sm->currentStyle(), QString("dark.modified"));
    f.open(QIODevice::ReadOnly);
    const QString content2 = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(content2.startsWith("/* doob-origin: preset:dark */"));
    QVERIFY(content2.contains("QLabel { color: green; }"));
    QVERIFY(!QFile::exists(m_data.path() + "/styles/dark.modified-2.qss"));
}

// Switching away from an edited preset current saves it first — the
// copy-on-write fork (the same path the style editor's OK takes).
void StyleStoreTest::testSwitchAwayFromEditedPresetForks()
{
    writePreset("dark", "QLabel { color: black; }");
    writeUser("mine", "QLabel { color: red; }");
    StyleManager* sm = StyleManager::getInstance();
    sm->setCurrentStyle("dark");
    sm->setStyleSheet("QLabel { color: blue; }"); // the unsaved edit

    QVERIFY(sm->switchStyle("mine"));

    QCOMPARE(sm->currentStyle(), QString("mine"));
    QCOMPARE(sm->stylesheet(), QString("QLabel { color: red; }"));
    const QString fork = m_data.path() + "/styles/dark.modified.qss";
    QVERIFY(QFile::exists(fork));
    QFile f(fork);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(f.readAll());
    f.close();
    QVERIFY(content.startsWith("/* doob-origin: preset:dark */"));
    QVERIFY(content.contains("QLabel { color: blue; }"));
}

// An unedited preset is never written: the save is a no-op and the
// current stays the read-only preset.
void StyleStoreTest::testCowLeavesUneditedPresetAlone()
{
    writePreset("dark", "QLabel { color: black; }");
    StyleManager* sm = StyleManager::getInstance();
    sm->setCurrentStyle("dark");
    sm->setStyleSheet("QLabel { color: black; }"); // exactly the preset's text

    QCOMPARE(sm->saveCurrentStyle(), QString("dark"));
    QCOMPARE(sm->currentStyle(), QString("dark"));
    QVERIFY(!QFile::exists(m_data.path() + "/styles/dark.modified.qss"));
    QVERIFY(!QFile::exists(m_data.path() + "/styles/dark.qss"));
}

// Save-as: the current sheet under a new name (an existing file is
// overwritten), the provenance carries over, the copy becomes current.
void StyleStoreTest::testSaveStyleAs()
{
    writePreset("dark", "QLabel { color: black; }");
    StyleManager* sm = StyleManager::getInstance();
    sm->setCurrentStyle("dark");
    sm->setStyleSheet("QLabel { color: blue; }");

    QVERIFY(sm->saveStyleAs("myfork"));
    QCOMPARE(sm->currentStyle(), QString("myfork"));
    const QString fork = m_data.path() + "/styles/myfork.qss";
    QVERIFY(QFile::exists(fork));
    // The provenance names the preset it was forked from.
    QCOMPARE(sm->originOf("myfork"), QString("preset:dark"));

    // A bad name (a path separator) is refused.
    QVERIFY(!sm->saveStyleAs("../escape"));
}

// Delete: user files only; deleting the current falls back to the
// first remaining user file, or to no current (the platform style)
// when there are none.
void StyleStoreTest::testDeleteStyle()
{
    writeUser("mine", "QLabel { color: red; }");
    writeUser("other", "QLabel { color: blue; }");
    writePreset("dark", "QLabel { color: black; }");
    StyleManager* sm = StyleManager::getInstance();
    sm->setCurrentStyle("mine");

    // Presets and names without a file are refused.
    QVERIFY(!sm->deleteStyle("dark"));
    QVERIFY(!sm->deleteStyle("default"));

    QVERIFY(sm->deleteStyle("mine"));
    QVERIFY(!QFile::exists(m_data.path() + "/styles/mine.qss"));
    // The current fell back to the first remaining user file.
    QCOMPARE(sm->currentStyle(), QString("other"));
    QCOMPARE(sm->stylesheet(), QString("QLabel { color: blue; }"));

    // The last one: the current is unset (the platform style).
    QVERIFY(sm->deleteStyle("other"));
    QCOMPARE(sm->currentStyle(), QString());
    QCOMPARE(sm->stylesheet(), QString());
}

// The startup load: the current style's text is applied (a preset's
// text when there is no user file).
void StyleStoreTest::testLoadCurrentStyle()
{
    writePreset("dark", "QLabel { color: black; }");
    StyleManager* sm = StyleManager::getInstance();
    sm->setCurrentStyle("dark");
    sm->setStyleSheet(QString()); // as if freshly started

    sm->loadCurrentStyle();
    QCOMPARE(sm->stylesheet(), QString("QLabel { color: black; }"));
}

// A current that names no file anywhere (deleted behind the app's back)
// falls back to the first user file, or to no current (the platform
// style) when there are none.
void StyleStoreTest::testStaleCurrentFallsBackToDefault()
{
    StyleManager* sm = StyleManager::getInstance();
    sm->setCurrentStyle("vanished");

    sm->loadCurrentStyle();
    QCOMPARE(sm->currentStyle(), QString());
    QCOMPARE(sm->stylesheet(), QString());

    // With a user file around, the fallback is that file.
    writeUser("only", "QLabel { color: green; }");
    sm->setCurrentStyle("vanished");
    sm->loadCurrentStyle();
    QCOMPARE(sm->currentStyle(), QString("only"));
    QCOMPARE(sm->stylesheet(), QString("QLabel { color: green; }"));
}

}

QTEST_MAIN(StyleStoreTest)
#include "stylestore_test.moc"
