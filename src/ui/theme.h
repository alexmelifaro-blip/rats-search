#ifndef RATS_UI_THEME_H
#define RATS_UI_THEME_H

#include <QColor>
#include <QHash>
#include <QString>

namespace rats::ui {

/**
 * @brief The single source of truth for every colour in the interface.
 *
 * There is one stylesheet template (`:/styles/styles/theme.qss`) written
 * against `@token` names, and one token table per theme
 * (`:/styles/styles/{light,dark}.json`). Theme substitutes the second into the
 * first, which is why a layout tweak can no longer land in one theme and be
 * forgotten in the other.
 *
 * Widgets that paint themselves (TorrentItemDelegate) read the same tokens
 * through color(), so they never drift from the sheet.
 *
 * The user's font choice is layered on top of whichever theme is loaded: the
 * family goes in front of the `@fontUi` stack and every `font-size` in the
 * sheet is multiplied by the scale, so it survives a light/dark switch and the
 * two themes can still only differ in colour.
 *
 * Not thread-safe: it is UI state and is only touched from the GUI thread.
 */
class Theme {
public:
    static Theme& instance();

    /** Loads the token table for @p dark. Cheap to call again with the same value. */
    void setDark(bool dark);
    bool isDark() const { return dark_; }

    /**
     * The user's font: @p family is tried before the theme's own UI font stack
     * (empty = the stack as is), and every font size is scaled to
     * @p scalePercent. Cheap to call again with the same values.
     */
    void setFont(const QString& family, int scalePercent);
    QString fontFamily() const { return fontFamily_; }
    int fontScalePercent() const { return fontScalePercent_; }

    /**
     * @p size multiplied by the font scale — for widgets that paint their own
     * text, and for the row heights sized around it.
     */
    qreal scaled(qreal size) const { return size * fontScalePercent_ / 100.0; }

    /** The expanded stylesheet for the current theme. */
    QString styleSheet() const;

    /**
     * Token value as a colour. An unknown or non-colour token returns @p fallback
     * and logs once — a typo shows up in the log instead of silently painting black.
     */
    QColor color(QLatin1String token, const QColor& fallback = QColor(Qt::magenta)) const;

    /** Raw token value (colours, icon paths, font stacks). Empty when unknown. */
    QString value(QLatin1String token) const;

private:
    Theme() = default;

    void load(bool dark);

    bool loaded_ = false;
    bool dark_ = false;
    QString fontFamily_;
    int fontScalePercent_ = 100;
    QHash<QString, QString> tokens_;
    QString styleSheet_;
};

} // namespace rats::ui

#endif // RATS_UI_THEME_H
