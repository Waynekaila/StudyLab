#include "engines/document/TextUnits.h"

#include <QRegularExpression>

namespace studylab {

QString normalizeExtractedText(const QString& text)
{
    static const QRegularExpression hyphen(QStringLiteral("(\\w)[-­]\\s*\\n\\s*(\\p{Ll})"),
                                           QRegularExpression::UseUnicodePropertiesOption);
    // Trait d'union d'un mot composé coupé en fin de ligne (« Jean-\nPaul ») : on garde le trait.
    static const QRegularExpression compound(QStringLiteral("(\\w-)\\s*\\n\\s*(\\p{Lu})"),
                                             QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression spaces(QStringLiteral("[ \\t\\f\\x0B  ]+"));
    static const QRegularExpression blank(QStringLiteral("\\s*\\n\\s*\\n\\s*"));
    static const QRegularExpression line(QStringLiteral(" *\\n *"));
    QString s = text;
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    s.remove(QChar(0));
    s.replace(hyphen, QStringLiteral("\\1\\2"));
    s.replace(compound, QStringLiteral("\\1\\2"));
    s.replace(spaces, QStringLiteral(" "));
    s.replace(blank, QStringLiteral("\n\n"));
    s.replace(line, QStringLiteral("\n"));
    return s.trimmed();
}

}  // namespace studylab
