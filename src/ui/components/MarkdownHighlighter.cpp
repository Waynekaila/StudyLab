#include "ui/components/MarkdownHighlighter.h"

#include "ui/theme/ThemeManager.h"

namespace studylab::ui {

MarkdownHighlighter::MarkdownHighlighter(ThemeManager& theme, QTextDocument* document)
    : QSyntaxHighlighter(document), m_theme(theme)
{
    updateFormats();
    connect(&theme, &ThemeManager::themeChanged, this, [this] {
        updateFormats();
        rehighlight();
    });
}

void MarkdownHighlighter::updateFormats()
{
    const Palette& p = m_theme.palette();
    m_rules.clear();
    auto rule = [this](const QString& re, const QTextCharFormat& f, int group = 0) {
        m_rules.append({QRegularExpression(re), f, group});
    };
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    bold.setForeground(p.primaryStrong);
    QTextCharFormat italic;
    italic.setFontItalic(true);
    QTextCharFormat code;
    code.setForeground(p.text2);
    code.setBackground(p.sunken);
    QTextCharFormat link;
    link.setForeground(p.primary);
    link.setFontUnderline(true);
    QTextCharFormat marker;
    marker.setForeground(p.text3);
    m_math = QTextCharFormat();
    m_math.setForeground(p.sage.darker(p.dark ? 80 : 130));
    m_heading = QTextCharFormat();
    m_heading.setFontWeight(QFont::Bold);
    m_heading.setForeground(p.text);

    rule(QStringLiteral("^\\s*([-*+]|\\d+[.)])\\s"), marker);
    rule(QStringLiteral("^\\s*>"), marker);
    rule(QStringLiteral("\\*\\*[^*\\n]+\\*\\*|__[^_\\n]+__"), bold);
    rule(QStringLiteral("(?<![*\\w])\\*(?!\\s)[^*\\n]+(?<!\\s)\\*(?!\\*)|(?<![_\\w])_(?!\\s)[^_\\n]+(?<!\\s)_(?!\\w)"), italic);
    rule(QStringLiteral("\\[\\[[^\\[\\]\\n]+\\]\\]"), link);
    rule(QStringLiteral("\\[[^\\]\\n]+\\]\\([^)\\n]+\\)"), link);
    rule(QStringLiteral("\\$\\$[^$]+\\$\\$|(?<![\\\\$])\\$(?!\\s)[^$\\n]+(?<!\\s)\\$"), m_math);
    rule(QStringLiteral("`[^`\\n]+`"), code);
}

void MarkdownHighlighter::highlightBlock(const QString& text)
{
    // Bloc de formule $$ … $$ sur plusieurs lignes : état 1 à l'intérieur.
    const bool inMath = previousBlockState() == 1;
    const int fences = int(text.count(QStringLiteral("$$")));
    if (inMath || (fences % 2 == 1 && text.trimmed().startsWith(QStringLiteral("$$")))) {
        setFormat(0, int(text.size()), m_math);
        const bool closes = inMath ? fences % 2 == 1 : false;
        setCurrentBlockState(inMath ? (closes ? 0 : 1) : 1);
        return;
    }
    setCurrentBlockState(0);
    static const QRegularExpression heading(QStringLiteral("^#{1,6}\\s"));
    if (heading.match(text).hasMatch()) {
        setFormat(0, int(text.size()), m_heading);
        return;
    }
    for (const Rule& r : std::as_const(m_rules)) {
        auto it = r.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(int(m.capturedStart(r.group)), int(m.capturedLength(r.group)), r.format);
        }
    }
}

}  // namespace studylab::ui
