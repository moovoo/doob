#include <QtTest/QtTest>
#include <QApplication>
#include <QLabel>
#include <QWidget>

#include "stylemanager.h"

namespace {

// Bare QObjects to match class-name selectors on.
class DummyOsc : public QObject
{
    Q_OBJECT
};

class ChildObj : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
};

class StyleManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testDefaultIsEmpty();
    void testSetAppliesToApplication();
    void testEmptyRestoresPlatformStyle();
    void testResetDropsInstance();
    void testScansCustomProperties();
    void testScanIgnoresPlainDeclarations();
    void testStyleValueMatchesObjectName();
    void testStyleValueMatchesClassName();
    void testStyleValueInheritsFromAncestor();
    void testStyleValueLaterRuleWins();
    void testStyleValueIdBeatsClassName();
    void testStyleValueFallsBack();
    void testRuleReferences();
    void testStylePropertyRegistration();
    void testStylePropertiesSorted();
};

void StyleManagerTest::init()
{
    StyleManager::reset();
    qApp->setStyleSheet(QString());
}

void StyleManagerTest::cleanup()
{
    StyleManager::reset();
    qApp->setStyleSheet(QString());
}

void StyleManagerTest::testDefaultIsEmpty()
{
    QCOMPARE(StyleManager::getInstance()->stylesheet(), QString());
    QCOMPARE(qApp->styleSheet(), QString());
}

void StyleManagerTest::testSetAppliesToApplication()
{
    const QString sheet = "QPushButton { background: green; }";
    StyleManager* sm = StyleManager::getInstance();

    sm->setStyleSheet(sheet);

    QCOMPARE(sm->stylesheet(), sheet);
    QCOMPARE(qApp->styleSheet(), sheet);
}

void StyleManagerTest::testEmptyRestoresPlatformStyle()
{
    StyleManager* sm = StyleManager::getInstance();
    sm->setStyleSheet("QLabel { color: red; }");

    sm->setStyleSheet(QString());

    QCOMPARE(sm->stylesheet(), QString());
    // The empty sheet is applied to the app as a comment-only
    // placeholder: a truly empty sheet would tear down Qt's stylesheet
    // wrapper, whose re-creation loses the widgets' cleanup
    // connections (QTBUG-149945).
    QVERIFY(!qApp->styleSheet().isEmpty());
}

void StyleManagerTest::testResetDropsInstance()
{
    StyleManager::getInstance()->setStyleSheet("QLabel { color: red; }");

    StyleManager::reset();

    QCOMPARE(StyleManager::getInstance()->stylesheet(), QString());
}

// The "x-" declarations are scanned into a lookup (in document order):
// plain declarations are ignored, comments are skipped, "!important"
// is stripped, a missing trailing ";" is fine, and the sheet Qt sees
// stays the raw text.
void StyleManagerTest::testScansCustomProperties()
{
    const QString sheet = QStringLiteral(
        "DummyOsc {\n"
        "    /* x-ghost: 1; { } ; */\n"
        "    background: #101020;\n"
        "    x-trace-style: boxes;\n"
        "    x-box-size: 3px !important\n"
        "}\n"
        "#meter, DummyOsc { x-peak-style: segments; }\n");
    StyleManager::getInstance()->setStyleSheet(sheet);

    // The text is passed to Qt unmodified (Qt warns and ignores the
    // "x-" declarations, applying the rest of each rule).
    QCOMPARE(qApp->styleSheet(), sheet);

    DummyOsc osc;
    QCOMPARE(StyleManager::getInstance()->styleValue(&osc, "x-trace-style", "none"), "boxes");
    QCOMPARE(StyleManager::getInstance()->styleValue(&osc, "x-box-size", "none"), "3px");
    QCOMPARE(StyleManager::getInstance()->styleValue(&osc, "x-peak-style", "none"), "segments");
    // The commented-out declaration never reached the scan.
    QCOMPARE(StyleManager::getInstance()->styleValue(&osc, "x-ghost", "none"), "none");

    // Each part of a comma-separated selector list matches on its own.
    QWidget meter;
    meter.setObjectName("meter");
    QCOMPARE(StyleManager::getInstance()->styleValue(&meter, "x-peak-style", "none"), "segments");
}

// Declarations without the "x-" prefix, rules without any "x-"
// declaration, and malformed fragments are not recorded.
void StyleManagerTest::testScanIgnoresPlainDeclarations()
{
    const QString sheet = QStringLiteral(
        "QLabel { color: red; }\n"
        "QWidget { background: blue; x-kept: yes; }\n"
        "Bogus selector without braces and a ; stray ;\n");
    StyleManager::getInstance()->setStyleSheet(sheet);

    // A QLabel matches only the first rule (no "x-" declarations there).
    QLabel label;
    QCOMPARE(StyleManager::getInstance()->styleValue(&label, "x-kept", "none"), "none");
    // A plain QWidget matches the second rule by class name.
    QWidget widget;
    QCOMPARE(StyleManager::getInstance()->styleValue(&widget, "x-kept", "none"), "yes");
}

// A "#name" selector matches the widget's objectName.
void StyleManagerTest::testStyleValueMatchesObjectName()
{
    StyleManager::getInstance()->setStyleSheet(QStringLiteral("#theone { x-a: hit; }"));

    QWidget hit;
    hit.setObjectName("theone");
    QCOMPARE(StyleManager::getInstance()->styleValue(&hit, "x-a", "none"), "hit");

    QWidget miss;
    miss.setObjectName("other");
    QCOMPARE(StyleManager::getInstance()->styleValue(&miss, "x-a", "none"), "none");
}

// A bare selector matches the widget's class name.
void StyleManagerTest::testStyleValueMatchesClassName()
{
    StyleManager::getInstance()->setStyleSheet(QStringLiteral("DummyOsc { x-a: hit; }"));

    DummyOsc osc;
    QCOMPARE(StyleManager::getInstance()->styleValue(&osc, "x-a", "none"), "hit");

    QWidget plain;
    QCOMPARE(StyleManager::getInstance()->styleValue(&plain, "x-a", "none"), "none");
}

// A value set for an ancestor cascades down (CSS custom-property
// style); a value set for the widget itself beats the inherited one.
void StyleManagerTest::testStyleValueInheritsFromAncestor()
{
    StyleManager::getInstance()->setStyleSheet(QStringLiteral(
        "#box { x-inherited: from-ancestor; }\n"
        "ChildObj { x-inherited: from-self; x-own: yes; }\n"));

    QWidget box;
    box.setObjectName("box");
    ChildObj child(&box);
    QCOMPARE(StyleManager::getInstance()->styleValue(&child, "x-inherited", "none"), "from-self");
    QCOMPARE(StyleManager::getInstance()->styleValue(&child, "x-own", "none"), "yes");

    // A child with no rule of its own picks up the ancestor's value.
    QWidget bare;
    bare.setParent(&box);
    QCOMPARE(StyleManager::getInstance()->styleValue(&bare, "x-inherited", "none"), "from-ancestor");

    // Outside the box there is no inherited value.
    QWidget outside;
    QCOMPARE(StyleManager::getInstance()->styleValue(&outside, "x-inherited", "none"), "none");
}

// Among matching rules of equal specificity the last one in the
// sheet wins.
void StyleManagerTest::testStyleValueLaterRuleWins()
{
    StyleManager::getInstance()->setStyleSheet(
        QStringLiteral("DummyOsc { x-a: one; }\nDummyOsc { x-a: two; }"));

    DummyOsc osc;
    QCOMPARE(StyleManager::getInstance()->styleValue(&osc, "x-a", "none"), "two");
}

// A "#name" selector beats a bare class name regardless of document
// order (CSS specificity), and each part of a comma list scores on
// its own.
void StyleManagerTest::testStyleValueIdBeatsClassName()
{
    // The id rule first: it still wins over the later class rule.
    StyleManager::getInstance()->setStyleSheet(
        QStringLiteral("#theone { x-a: id; }\nDummyOsc { x-a: class; }"));
    DummyOsc hit;
    hit.setObjectName("theone");
    QCOMPARE(StyleManager::getInstance()->styleValue(&hit, "x-a", "none"), "id");

    // The class rule first: the id rule still wins (document order
    // does not matter between different specificities).
    StyleManager::getInstance()->setStyleSheet(
        QStringLiteral("DummyOsc { x-a: class; }\n#theone { x-a: id; }"));
    QCOMPARE(StyleManager::getInstance()->styleValue(&hit, "x-a", "none"), "id");

    // A node without the matching name takes the class rule.
    DummyOsc plain;
    QCOMPARE(StyleManager::getInstance()->styleValue(&plain, "x-a", "none"), "class");

    // A rule that matches the node by its class part carries class
    // specificity: the separate id rule still beats it.
    StyleManager::getInstance()->setStyleSheet(
        QStringLiteral("#other, DummyOsc { x-a: via-class-part; }\n"
                       "#theone { x-a: id; }"));
    QCOMPARE(StyleManager::getInstance()->styleValue(&hit, "x-a", "none"), "id");
}

// No sheet, no matching selector, or no such property: the fallback
// stands in.
void StyleManagerTest::testStyleValueFallsBack()
{
    DummyOsc osc;
    QCOMPARE(StyleManager::getInstance()->styleValue(&osc, "x-a", "fallback"), "fallback");

    StyleManager::getInstance()->setStyleSheet(QStringLiteral("OtherDummy { x-a: one; }"));
    QCOMPARE(StyleManager::getInstance()->styleValue(&osc, "x-a", "fallback"), "fallback");

    StyleManager::getInstance()->setStyleSheet(QStringLiteral("DummyOsc { x-b: one; }"));
    QCOMPARE(StyleManager::getInstance()->styleValue(&osc, "x-a", "fallback"), "fallback");
}

// ruleReferences: true only when a rule's selector list names the
// objectName — class names are not names, a cleared sheet references
// nothing, and plain (non-"x-") rules are indexed too: the empty
// "#name { }" block the style action starts from, and a native rule
// the user wrote by hand, both pin the name.
void StyleManagerTest::testRuleReferences()
{
    StyleManager* sm = StyleManager::getInstance();
    sm->setStyleSheet(QStringLiteral(
        "DummyOsc { x-a: one; }\n"
        "#meter, DummyOsc { x-a: two; }\n"
        "QWidget { background: red; }\n"));

    QVERIFY(sm->ruleReferences("meter"));
    QVERIFY(!sm->ruleReferences("other"));
    QVERIFY(!sm->ruleReferences("DummyOsc"));
    QVERIFY(!sm->ruleReferences("QWidget"));

    // Plain rules (no "x-" declaration) are indexed as well.
    sm->setStyleSheet(QStringLiteral(
        "#empty { }\n"
        "#plain { color: red; }\n"));
    QVERIFY(sm->ruleReferences("empty"));
    QVERIFY(sm->ruleReferences("plain"));
    QVERIFY(!sm->ruleReferences("meter"));

    sm->setStyleSheet(QString());
    QVERIFY(!sm->ruleReferences("meter"));
}

// The "x-" property docs register under their names: a re-registered
// name replaces its record (no duplicate), an empty name is ignored.
void StyleManagerTest::testStylePropertyRegistration()
{
    StyleManager* sm = StyleManager::getInstance();

    StyleProperty b;
    b.name = QStringLiteral("x-b");
    b.owner = QStringLiteral("Widget B");
    b.defaultValue = QStringLiteral("16");
    StyleProperty a;
    a.name = QStringLiteral("x-a");
    a.owner = QStringLiteral("Widget A");
    sm->registerStyleProperties({a, b});

    QList<StyleProperty> props = sm->styleProperties();
    QCOMPARE(props.size(), 2);

    // A re-registered name replaces its record; an empty name is
    // dropped.
    StyleProperty b2;
    b2.name = QStringLiteral("x-b");
    b2.owner = QStringLiteral("Widget B");
    b2.description = QStringLiteral("the new docs");
    sm->registerStyleProperties({b2, {}});
    props = sm->styleProperties();
    QCOMPARE(props.size(), 2);
    QCOMPARE(props.at(1).description, QStringLiteral("the new docs"));
}

// The help's stable order: sorted by owner, then by name.
void StyleManagerTest::testStylePropertiesSorted()
{
    StyleManager* sm = StyleManager::getInstance();

    StyleProperty z1;
    z1.name = QStringLiteral("x-z");
    z1.owner = QStringLiteral("Widget A");
    StyleProperty a2;
    a2.name = QStringLiteral("x-a");
    a2.owner = QStringLiteral("Widget B");
    StyleProperty z2;
    z2.name = QStringLiteral("x-y");
    z2.owner = QStringLiteral("Widget A");
    sm->registerStyleProperties({z1, a2, z2});

    const QList<StyleProperty> props = sm->styleProperties();
    QCOMPARE(props.size(), 3);
    QCOMPARE(props.at(0).name, QStringLiteral("x-y"));  // Widget A first, by name
    QCOMPARE(props.at(1).name, QStringLiteral("x-z"));
    QCOMPARE(props.at(2).name, QStringLiteral("x-a"));  // Widget B last
}
}

QTEST_MAIN(StyleManagerTest)
#include "stylemanager_test.moc"
