#include "theme.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>

namespace rats::ui {

namespace {

QString readResource(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Theme: cannot read" << path << file.errorString();
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

/**
 * Replaces every `@token` in @p tmpl with its value. Anything unknown is left
 * verbatim and reported: Qt would otherwise drop the whole declaration without
 * a word, which is a miserable thing to debug from a screenshot.
 */
QString expand(const QString& tmpl, const QHash<QString, QString>& tokens)
{
    static const QRegularExpression placeholder(QStringLiteral("@([A-Za-z][A-Za-z0-9_]*)"));

    QString out;
    out.reserve(tmpl.size() + tmpl.size() / 4);

    qsizetype copied = 0;
    auto it = placeholder.globalMatch(tmpl);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString name = match.captured(1);
        const auto token = tokens.constFind(name);
        if (token == tokens.constEnd()) {
            qWarning() << "Theme: unknown token @" << name << "in theme.qss";
            continue; // leave it in place, copied with the surrounding text
        }
        out += QStringView(tmpl).mid(copied, match.capturedStart() - copied);
        out += *token;
        copied = match.capturedEnd();
    }
    out += QStringView(tmpl).mid(copied);
    return out;
}

/**
 * @p family as a QSS font-family entry. The value comes from the config, which
 * `config.set` can write too, so anything that could close the string or the
 * declaration is dropped rather than escaped — no real family name has it.
 */
QString quoteFamily(QString family)
{
    static const QRegularExpression unsafe(QStringLiteral("[\"'\\\\;{}@\r\n]"));
    family.remove(unsafe);
    family = family.trimmed();
    return family.isEmpty() ? QString() : QLatin1Char('"') + family + QLatin1Char('"');
}

/**
 * Multiplies every `font-size: <n>px` in @p sheet by @p percent. Only font
 * sizes: paddings and radii stay, so a larger font does not turn into a
 * coarser layout — that is what the platform's display scaling is for.
 */
QString scaleFontSizes(const QString& sheet, int percent)
{
    if (percent == 100) {
        return sheet;
    }
    static const QRegularExpression fontSize(QStringLiteral("(font-size\\s*:\\s*)(\\d+(?:\\.\\d+)?)px"));

    QString out;
    out.reserve(sheet.size());
    qsizetype copied = 0;
    auto it = fontSize.globalMatch(sheet);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int px = qMax(1, qRound(match.captured(2).toDouble() * percent / 100.0));
        out += QStringView(sheet).mid(copied, match.capturedStart(2) - copied);
        out += QString::number(px);
        copied = match.capturedEnd(2);
    }
    out += QStringView(sheet).mid(copied);
    return out;
}

} // namespace

Theme& Theme::instance()
{
    static Theme theme;
    return theme;
}

void Theme::setDark(bool dark)
{
    if (loaded_ && dark_ == dark) {
        return;
    }
    load(dark);
}

void Theme::setFont(const QString& family, int scalePercent)
{
    const QString trimmed = family.trimmed();
    if (fontFamily_ == trimmed && fontScalePercent_ == scalePercent) {
        return;
    }
    fontFamily_ = trimmed;
    fontScalePercent_ = qMax(1, scalePercent);
    if (loaded_) {
        load(dark_);
    }
}

void Theme::load(bool dark)
{
    dark_ = dark;
    loaded_ = true;
    tokens_.clear();

    const QString tokenPath
        = dark ? QStringLiteral(":/styles/styles/dark.json") : QStringLiteral(":/styles/styles/light.json");

    QJsonParseError error {};
    const QJsonDocument doc = QJsonDocument::fromJson(readResource(tokenPath).toUtf8(), &error);
    if (!doc.isObject()) {
        qWarning() << "Theme: bad token file" << tokenPath << error.errorString();
    } else {
        const QJsonObject object = doc.object();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.key().startsWith(QLatin1Char('_'))) {
                continue; // documentation keys
            }
            tokens_.insert(it.key(), it.value().toString());
        }
    }

    const QString family = quoteFamily(fontFamily_);
    if (!family.isEmpty()) {
        // In front of the stack, not instead of it: a family that is not
        // installed (a config carried to another machine) falls through to the
        // theme's own choice instead of to Qt's last-resort default.
        const QString stack = tokens_.value(QStringLiteral("fontUi"));
        tokens_.insert(QStringLiteral("fontUi"), stack.isEmpty() ? family : family + QStringLiteral(", ") + stack);
    }

    styleSheet_
        = scaleFontSizes(expand(readResource(QStringLiteral(":/styles/styles/theme.qss")), tokens_), fontScalePercent_);
    qInfo() << (dark ? "Dark" : "Light") << "theme loaded:" << tokens_.size() << "tokens, font"
            << (fontFamily_.isEmpty() ? QStringLiteral("default") : fontFamily_) << fontScalePercent_ << "%";
}

QString Theme::styleSheet() const
{
    if (!loaded_) {
        const_cast<Theme*>(this)->load(dark_);
    }
    return styleSheet_;
}

QString Theme::value(QLatin1String token) const
{
    if (!loaded_) {
        const_cast<Theme*>(this)->load(dark_);
    }
    return tokens_.value(QString(token));
}

QColor Theme::color(QLatin1String token, const QColor& fallback) const
{
    const QString raw = value(token);
    const QColor color(raw);
    if (!color.isValid()) {
        qWarning() << "Theme: token" << token << "is not a colour:" << raw;
        return fallback;
    }
    return color;
}

} // namespace rats::ui
