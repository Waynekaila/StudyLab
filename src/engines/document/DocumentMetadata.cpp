#include "engines/document/DocumentMetadata.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace studylab {

namespace {

bool validIsbn13(const QString& d)
{
    int sum = 0;
    for (int i = 0; i < 13; ++i)
        sum += d.at(i).digitValue() * (i % 2 ? 3 : 1);
    return sum % 10 == 0;
}

bool validIsbn10(const QString& d)
{
    int sum = 0;
    for (int i = 0; i < 10; ++i) {
        const QChar c = d.at(i);
        const int v = (i == 9 && c.toUpper() == QLatin1Char('X')) ? 10 : c.digitValue();
        if (v < 0)
            return false;
        sum += v * (10 - i);
    }
    return sum % 11 == 0;
}

}  // namespace

QString extractIsbn(const QString& text)
{
    static const QRegularExpression candidate(QStringLiteral("(97[89][\\d\\- ]{10,16}|[\\d][\\d\\- ]{8,12}[\\dXx])"));
    auto it = candidate.globalMatch(text);
    while (it.hasNext()) {
        QString d = it.next().captured(1);
        d.remove(QLatin1Char('-')).remove(QLatin1Char(' '));
        if (d.size() == 13 && validIsbn13(d))
            return d;
        if (d.size() == 10 && validIsbn10(d))
            return d.toUpper();
    }
    return {};
}

QString extractDoi(const QString& text)
{
    static const QRegularExpression doi(QStringLiteral("(10\\.\\d{4,9}/[^\\s\"<>]+)"));
    const auto m = doi.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString titleFromFileName(const QString& path)
{
    QString t = QFileInfo(path).completeBaseName();
    const bool hadSpaces = t.contains(QLatin1Char(' '));
    t.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!hadSpaces)  // « cengel-thermo » : les tirets séparent les mots ; « Jean-Paul Sartre » est conservé
        t.replace(QLatin1Char('-'), QLatin1Char(' '));
    t = t.simplified();
    return t.isEmpty() ? QStringLiteral("Sans titre") : t;
}

}  // namespace studylab
