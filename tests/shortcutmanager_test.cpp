#include <QtTest>
#include <QKeySequence>
#include <QShortcut>
#include <QTemporaryDir>
#include <QWidget>

#include <memory>

#include "playbackmanager.h"
#include "shortcutmanager.h"
#include "settingsmanager.h"

namespace {

class ShortcutManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // First in the file on purpose: it constructs the PlaybackManager
    // singleton (once per process), and the playback actions register
    // on the ShortcutManager that is alive at that first construction —
    // the one this test's init() created.
    void playbackActionsDefaultToMediaKeys();
    void registerActionRegistersSettingsAndDefaults();
    void duplicateIdRefused();
    void emptyIdRefused();
    void storedValueOverridesDefault();
    void attachCreatesShortcuts();
    void settingsChangeRebindsTheShortcut();
    void globalFlagSwitchesTheContext();
    void emptySequenceDisablesTheShortcut();
    void resetToDefaultRestoresTheDefault();
    void shortcutActivationInvokesTheAction();
    void registerAfterAttachCreatesTheShortcut();
    void unregisterDropsActionAndShortcut();

private:
    SettingsManager* m_settings;
    ShortcutManager* m_manager;
    QWidget m_window;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void ShortcutManagerTest::init()
{
    // The SettingsManager first: the manager connects to it in its ctor.
    SettingsManager::reset();
    m_settings = SettingsManager::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    m_settings->setSettingsPath(m_dir->path() + "/settings.json");
    m_settings->load();

    ShortcutManager::reset();
    m_manager = ShortcutManager::getInstance();
}

void ShortcutManagerTest::cleanup()
{
    m_manager->attach(nullptr);
    ShortcutManager::reset();
    SettingsManager::reset();
}

void ShortcutManagerTest::playbackActionsDefaultToMediaKeys()
{
    // The PlaybackManager's ctor registers the playback actions (the
    // singleton, constructed here for the first time in this process).
    PlaybackManager::getInstance();

    // The hardware media key is the primary sequence, the former
    // default its fallback.
    const QKeySequence playPause = m_manager->sequence("playback.play_pause");
    QCOMPARE(playPause.count(), 2);
    QCOMPARE(playPause[0].toCombined(), Qt::Key_MediaTogglePlayPause);
    QCOMPARE(playPause[1].toCombined(), static_cast<int>(Qt::CTRL | Qt::Key_P));
    const QKeySequence next = m_manager->sequence("playback.next");
    QCOMPARE(next.count(), 2);
    QCOMPARE(next[0].toCombined(), Qt::Key_MediaNext);
    QCOMPARE(next[1].toCombined(), static_cast<int>(Qt::CTRL | Qt::Key_Right));
    const QKeySequence previous = m_manager->sequence("playback.previous");
    QCOMPARE(previous.count(), 2);
    QCOMPARE(previous[0].toCombined(), Qt::Key_MediaPrevious);
    QCOMPARE(previous[1].toCombined(), static_cast<int>(Qt::CTRL | Qt::Key_Left));
    // The stop keeps its plain default.
    QCOMPARE(m_manager->sequence("playback.stop").toString(),
             QString("Ctrl+Shift+P"));
}

void ShortcutManagerTest::registerActionRegistersSettingsAndDefaults()
{
    int calls = 0;
    QVERIFY(m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("Test action"),
          QStringLiteral("Test"), QKeySequence(QStringLiteral("Ctrl+T")),
          true, [&calls] { ++calls; } }));

    // The two SettingsManager keys, defaults = the action's defaults.
    const Setting seqSetting =
        m_settings->setting(ShortcutManager::sequenceKey("test.act"));
    QVERIFY(seqSetting.isValid());
    QCOMPARE(seqSetting.defaultValue.toString(), QString("Ctrl+T"));
    const Setting globalSetting =
        m_settings->setting(ShortcutManager::globalKey("test.act"));
    QVERIFY(globalSetting.isValid());
    QCOMPARE(globalSetting.defaultValue.toBool(), true);

    QCOMPARE(m_manager->sequence("test.act").toString(), QString("Ctrl+T"));
    QCOMPARE(m_manager->isGlobal("test.act"), true);
    QCOMPARE(m_manager->actions().size(), 1);
    QCOMPARE(m_manager->actions().first().title, QString("Test action"));

    m_manager->trigger("test.act");
    QCOMPARE(calls, 1);
}

void ShortcutManagerTest::duplicateIdRefused()
{
    QVERIFY(m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("A"), QStringLiteral("Test"),
          QKeySequence(QStringLiteral("Ctrl+T")), true, [] {} }));
    QVERIFY(!m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("B"), QStringLiteral("Test"),
          QKeySequence(QStringLiteral("Ctrl+U")), true, [] {} }));
    QCOMPARE(m_manager->actions().size(), 1);
}

void ShortcutManagerTest::emptyIdRefused()
{
    QVERIFY(!m_manager->registerAction(
        { QString(), QStringLiteral("A"), QStringLiteral("Test"),
          QKeySequence(QStringLiteral("Ctrl+T")), true, [] {} }));
}

void ShortcutManagerTest::storedValueOverridesDefault()
{
    m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("Test action"),
          QStringLiteral("Test"), QKeySequence(QStringLiteral("Ctrl+T")),
          true, [] {}, });

    m_settings->setValue(ShortcutManager::sequenceKey("test.act"), "Ctrl+K");
    m_settings->setValue(ShortcutManager::globalKey("test.act"), false);

    QCOMPARE(m_manager->sequence("test.act").toString(), QString("Ctrl+K"));
    QCOMPARE(m_manager->isGlobal("test.act"), false);
}

void ShortcutManagerTest::attachCreatesShortcuts()
{
    m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("Test action"),
          QStringLiteral("Test"), QKeySequence(QStringLiteral("Ctrl+T")),
          true, [] {}, });

    // Unattached: no live shortcut.
    QVERIFY(m_manager->shortcut("test.act") == nullptr);

    m_manager->attach(&m_window);
    QShortcut *shortcut = m_manager->shortcut("test.act");
    QVERIFY(shortcut != nullptr);
    QCOMPARE(shortcut->key().toString(), QString("Ctrl+T"));
    QCOMPARE(shortcut->context(), Qt::ApplicationShortcut);
}

void ShortcutManagerTest::settingsChangeRebindsTheShortcut()
{
    m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("Test action"),
          QStringLiteral("Test"), QKeySequence(QStringLiteral("Ctrl+T")),
          true, [] {}, });
    m_manager->attach(&m_window);

    m_settings->setValue(ShortcutManager::sequenceKey("test.act"), "Ctrl+K");
    QCOMPARE(m_manager->shortcut("test.act")->key().toString(),
             QString("Ctrl+K"));
}

void ShortcutManagerTest::globalFlagSwitchesTheContext()
{
    m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("Test action"),
          QStringLiteral("Test"), QKeySequence(QStringLiteral("Ctrl+T")),
          true, [] {}, });
    m_manager->attach(&m_window);

    QCOMPARE(m_manager->shortcut("test.act")->context(), Qt::ApplicationShortcut);
    m_settings->setValue(ShortcutManager::globalKey("test.act"), false);
    QCOMPARE(m_manager->shortcut("test.act")->context(), Qt::WindowShortcut);
}

void ShortcutManagerTest::emptySequenceDisablesTheShortcut()
{
    m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("Test action"),
          QStringLiteral("Test"), QKeySequence(QStringLiteral("Ctrl+T")),
          true, [] {}, });
    m_manager->attach(&m_window);

    m_settings->setValue(ShortcutManager::sequenceKey("test.act"), QString());
    QVERIFY(m_manager->shortcut("test.act")->key().isEmpty());
}

void ShortcutManagerTest::resetToDefaultRestoresTheDefault()
{
    m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("Test action"),
          QStringLiteral("Test"), QKeySequence(QStringLiteral("Ctrl+T")),
          true, [] {}, });
    m_manager->attach(&m_window);

    m_settings->setValue(ShortcutManager::sequenceKey("test.act"), "Ctrl+K");
    m_settings->setValue(ShortcutManager::globalKey("test.act"), false);
    m_settings->resetToDefault(ShortcutManager::sequenceKey("test.act"));
    m_settings->resetToDefault(ShortcutManager::globalKey("test.act"));

    QCOMPARE(m_manager->sequence("test.act").toString(), QString("Ctrl+T"));
    QCOMPARE(m_manager->isGlobal("test.act"), true);
    QCOMPARE(m_manager->shortcut("test.act")->key().toString(),
             QString("Ctrl+T"));
    QCOMPARE(m_manager->shortcut("test.act")->context(),
             Qt::ApplicationShortcut);
}

void ShortcutManagerTest::shortcutActivationInvokesTheAction()
{
    int calls = 0;
    m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("Test action"),
          QStringLiteral("Test"), QKeySequence(QStringLiteral("Ctrl+T")),
          true, [&calls] { ++calls; }, });
    m_manager->attach(&m_window);

    // Fire the shortcut's activated() the way a key press would.
    QShortcut *shortcut = m_manager->shortcut("test.act");
    const int index = shortcut->metaObject()->indexOfSignal("activated()");
    QMetaObject::activate(shortcut, index, nullptr);

    QCOMPARE(calls, 1);
}

void ShortcutManagerTest::registerAfterAttachCreatesTheShortcut()
{
    m_manager->attach(&m_window);

    m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("Test action"),
          QStringLiteral("Test"), QKeySequence(QStringLiteral("Ctrl+T")),
          false, [] {}, });

    QShortcut *shortcut = m_manager->shortcut("test.act");
    QVERIFY(shortcut != nullptr);
    QCOMPARE(shortcut->context(), Qt::WindowShortcut);
}

void ShortcutManagerTest::unregisterDropsActionAndShortcut()
{
    m_manager->registerAction(
        { QStringLiteral("test.act"), QStringLiteral("Test action"),
          QStringLiteral("Test"), QKeySequence(QStringLiteral("Ctrl+T")),
          true, [] {}, });
    m_manager->attach(&m_window);
    m_settings->setValue(ShortcutManager::sequenceKey("test.act"), "Ctrl+K");

    m_manager->unregisterAction("test.act");

    QVERIFY(m_manager->shortcut("test.act") == nullptr);
    QCOMPARE(m_manager->actions().size(), 0);
    QVERIFY(!m_settings->setting(ShortcutManager::sequenceKey("test.act")).isValid());
    // The stored value survives the unregistration (a re-added action
    // finds its settings intact).
    QVERIFY(m_settings->isModified(ShortcutManager::sequenceKey("test.act")));
}
}

QTEST_MAIN(ShortcutManagerTest)
#include "shortcutmanager_test.moc"
