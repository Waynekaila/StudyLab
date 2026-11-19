#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace studylab::ui {

class ThemeManager;

/// Coloration légère du Markdown dans l'éditeur : titres, gras, italique, code, formules, liens [[…]],
/// listes et citations. Les couleurs suivent le thème.
class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    MarkdownHighlighter(ThemeManager& theme, QTextDocument* document);

protected:
    void highlightBlock(const QString& text) override;

private:
    void updateFormats();

    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
        int group = 0;
    };
    ThemeManager& m_theme;
    QList<Rule> m_rules;
    QTextCharFormat m_heading;
    QTextCharFormat m_math;
};

}  // namespace studylab::ui
