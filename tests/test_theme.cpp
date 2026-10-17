#include "ui/theme.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QtTest>

using rats::ui::Theme;

/**
 * The two themes share one stylesheet, so nothing here checks how anything
 * looks — it checks the two halves still fit together: every placeholder the
 * sheet spells has a value, both token tables carry the same keys, and the
 * expansion actually happens.
 *
 * The failure these guard against is silent: Qt drops a declaration containing
 * an unresolved `@name` without a word, so a missing token shows up as a widget
 * that quietly lost its colour in one theme only.
 */
class TestTheme : public QObject {
    Q_OBJECT

private slots:
    void bothThemesDefineTheSameTokens();
    void everyPlaceholderResolves_data();
    void everyPlaceholderResolves();
    void styleSheetIsExpanded_data();
    void styleSheetIsExpanded();
    void tokensReadAsColours_data();
    void tokensReadAsColours();
    void switchingThemeChangesTheSheet();
    void fontFamilyGoesInFrontOfTheStack();
    void fontScaleMultipliesFontSizesOnly();
    void fontSurvivesThemeSwitch();
    void fontFamilyCannotBreakOutOfTheDeclaration();

private:
    static QSet<QString> tokenKeys(const QString& path);
    static QString resource(const QString& path);
};

QString TestTheme::resource(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QSet<QString> TestTheme::tokenKeys(const QString& path)
{
    const QJsonObject object = QJsonDocument::fromJson(resource(path).toUtf8()).object();
    QSet<QString> keys;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!it.key().startsWith(QLatin1Char('_'))) {
            keys.insert(it.key());
        }
    }
    return keys;
}

void TestTheme::bothThemesDefineTheSameTokens()
{
    const QSet<QString> light = tokenKeys(QStringLiteral(":/styles/styles/light.json"));
    const QSet<QString> dark = tokenKeys(QStringLiteral(":/styles/styles/dark.json"));

    QVERIFY(!light.isEmpty());

    QStringList onlyLight = QStringList(QList<QString>((light - dark).values()));
    QStringList onlyDark = QStringList(QList<QString>((dark - light).values()));
    onlyLight.sort();
    onlyDark.sort();
    QVERIFY2(onlyLight.isEmpty(), qPrintable(QStringLiteral("only in light.json: ") + onlyLight.join(", ")));
    QVERIFY2(onlyDark.isEmpty(), qPrintable(QStringLiteral("only in dark.json: ") + onlyDark.join(", ")));
}

void TestTheme::everyPlaceholderResolves_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

void TestTheme::everyPlaceholderResolves()
{
    QFETCH(bool, dark);

    const QString tmpl = resource(QStringLiteral(":/styles/styles/theme.qss"));
    QVERIFY(!tmpl.isEmpty());

    const QSet<QString> keys
        = tokenKeys(dark ? QStringLiteral(":/styles/styles/dark.json") : QStringLiteral(":/styles/styles/light.json"));

    static const QRegularExpression placeholder(QStringLiteral("@([A-Za-z][A-Za-z0-9_]*)"));
    QStringList missing;
    auto it = placeholder.globalMatch(tmpl);
    while (it.hasNext()) {
        const QString name = it.next().captured(1);
        if (!keys.contains(name) && !missing.contains(name)) {
            missing << name;
        }
    }
    QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("theme.qss uses undefined tokens: ") + missing.join(", ")));
}

void TestTheme::styleSheetIsExpanded_data()
{
    everyPlaceholderResolves_data();
}

void TestTheme::styleSheetIsExpanded()
{
    QFETCH(bool, dark);

    Theme& theme = Theme::instance();
    theme.setDark(dark);

    QString sheet = theme.styleSheet();
    QVERIFY(sheet.contains(QStringLiteral("QPushButton")));

    // Comments are the only place an at-name may survive expansion.
    static const QRegularExpression comment(
        QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
    sheet.remove(comment);
    QVERIFY2(!sheet.contains(QLatin1Char('@')), "stylesheet still carries an unresolved placeholder");
}

void TestTheme::tokensReadAsColours_data()
{
    everyPlaceholderResolves_data();
}

void TestTheme::tokensReadAsColours()
{
    QFETCH(bool, dark);

    Theme& theme = Theme::instance();
    theme.setDark(dark);
    QCOMPARE(theme.isDark(), dark);

    // The delegate paints straight from these; a typo would show up as magenta.
    for (const char* token :
        { "text", "textMuted", "textFaint", "textOnAccent", "surface", "surfaceAlt", "accent", "rowHover", "rowBorder",
            "remoteRow", "remoteRowAlt", "remoteStripe", "pathText", "matchHighlight", "matchHighlightSelected",
            "seedersHigh", "seedersMid", "seedersLow", "leechersHigh", "leechersMid", "leechersLow", "peersNone" }) {
        const QColor color = theme.color(QLatin1String(token), QColor());
        QVERIFY2(color.isValid(), token);
    }
}

void TestTheme::switchingThemeChangesTheSheet()
{
    Theme& theme = Theme::instance();

    theme.setDark(false);
    const QString light = theme.styleSheet();
    const QColor lightSurface = theme.color(QLatin1String("surface"));

    theme.setDark(true);
    const QString dark = theme.styleSheet();
    const QColor darkSurface = theme.color(QLatin1String("surface"));

    QVERIFY(light != dark);
    QVERIFY(lightSurface != darkSurface);
    QVERIFY(lightSurface.lightness() > darkSurface.lightness());
}

// The base rule in theme.qss: `* { font-family: ...; font-size: 13px; }`.
static QString baseFontRule(const QString& sheet)
{
    static const QRegularExpression base(
        QStringLiteral("\\*\\s*\\{\\s*font-family:([^;]*);\\s*font-size:\\s*(\\d+)px"));
    const QRegularExpressionMatch match = base.match(sheet);
    return match.hasMatch() ? match.captured(1).trimmed() + QLatin1Char('|') + match.captured(2) : QString();
}

void TestTheme::fontFamilyGoesInFrontOfTheStack()
{
    Theme& theme = Theme::instance();
    theme.setDark(false);
    theme.setFont(QString(), 100);
    const QString stack = theme.value(QLatin1String("fontUi"));
    QVERIFY(!stack.isEmpty());

    theme.setFont(QStringLiteral("Comic Neue"), 100);
    // Kept as a fallback: a family missing on this machine must land on the
    // theme's choice, not on Qt's last resort.
    QCOMPARE(theme.value(QLatin1String("fontUi")), QStringLiteral("\"Comic Neue\", ") + stack);
    QVERIFY(baseFontRule(theme.styleSheet()).startsWith(QStringLiteral("\"Comic Neue\", ")));

    theme.setFont(QString(), 100);
    QCOMPARE(theme.value(QLatin1String("fontUi")), stack);
}

void TestTheme::fontScaleMultipliesFontSizesOnly()
{
    Theme& theme = Theme::instance();
    theme.setDark(false);
    theme.setFont(QString(), 100);
    const QString normal = theme.styleSheet();
    QVERIFY(baseFontRule(normal).endsWith(QStringLiteral("|13")));

    theme.setFont(QString(), 200);
    const QString doubled = theme.styleSheet();
    QVERIFY(baseFontRule(doubled).endsWith(QStringLiteral("|26")));
    QCOMPARE(theme.scaled(10), 20.0);

    // Paddings, radii and borders keep their size: with the font sizes put
    // back, the two sheets are the same text.
    static const QRegularExpression fontSize(QStringLiteral("font-size\\s*:\\s*\\d+px"));
    QString a = normal, b = doubled;
    a.replace(fontSize, QStringLiteral("font-size"));
    b.replace(fontSize, QStringLiteral("font-size"));
    QCOMPARE(a, b);

    theme.setFont(QString(), 100);
    QCOMPARE(theme.styleSheet(), normal);
}

void TestTheme::fontSurvivesThemeSwitch()
{
    Theme& theme = Theme::instance();
    theme.setDark(false);
    theme.setFont(QStringLiteral("Comic Neue"), 150);

    theme.setDark(true);
    QVERIFY(theme.isDark());
    const QString rule = baseFontRule(theme.styleSheet());
    QVERIFY2(rule.startsWith(QStringLiteral("\"Comic Neue\"")), qPrintable(rule));
    QVERIFY2(rule.endsWith(QStringLiteral("|20")), qPrintable(rule)); // 13px * 1.5, rounded

    theme.setFont(QString(), 100);
    theme.setDark(false);
}

void TestTheme::fontFamilyCannotBreakOutOfTheDeclaration()
{
    // The family reaches the config through `config.set` as well as through the
    // dialog, so it is sheet input from the outside.
    Theme& theme = Theme::instance();
    theme.setDark(false);
    theme.setFont(QStringLiteral("Evil\"; } QWidget { background: @bg; color: red"), 100);

    const QString family = theme.value(QLatin1String("fontUi")).section(QLatin1Char(','), 0, 0);
    QVERIFY2(!family.mid(1, family.size() - 2).contains(QLatin1Char('"')), qPrintable(family));
    QVERIFY(!family.contains(QLatin1Char(';')));
    QVERIFY(!family.contains(QLatin1Char('}')));
    QVERIFY(!family.contains(QLatin1Char('@')));

    theme.setFont(QString(), 100);
}

QTEST_MAIN(TestTheme)
#include "test_theme.moc"
