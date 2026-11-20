#include "core/domain/Document.h"

#include <QObject>
#include <QRegularExpression>

namespace studylab {

QString readingStatusKey(ReadingStatus s)
{
    switch (s) {
    case ReadingStatus::ToRead: return QStringLiteral("to_read");
    case ReadingStatus::Reading: return QStringLiteral("reading");
    case ReadingStatus::Paused: return QStringLiteral("paused");
    case ReadingStatus::Finished: return QStringLiteral("finished");
    case ReadingStatus::ToReread: return QStringLiteral("to_reread");
    case ReadingStatus::Archived: return QStringLiteral("archived");
    }
    return QStringLiteral("to_read");
}

std::optional<ReadingStatus> readingStatusFromKey(const QString& key)
{
    for (ReadingStatus s : allReadingStatuses()) {
        if (readingStatusKey(s) == key)
            return s;
    }
    return std::nullopt;
}

QString readingStatusLabel(ReadingStatus s)
{
    switch (s) {
    case ReadingStatus::ToRead: return QObject::tr("À lire");
    case ReadingStatus::Reading: return QObject::tr("En cours");
    case ReadingStatus::Paused: return QObject::tr("En pause");
    case ReadingStatus::Finished: return QObject::tr("Terminé");
    case ReadingStatus::ToReread: return QObject::tr("À relire");
    case ReadingStatus::Archived: return QObject::tr("Archivé");
    }
    return {};
}

QList<ReadingStatus> allReadingStatuses()
{
    return {ReadingStatus::ToRead,   ReadingStatus::Reading,  ReadingStatus::Paused,
            ReadingStatus::Finished, ReadingStatus::ToReread, ReadingStatus::Archived};
}

QString documentFormatKey(DocumentFormat f)
{
    switch (f) {
    case DocumentFormat::Pdf:
        return QStringLiteral("pdf");
    case DocumentFormat::Epub:
        return QStringLiteral("epub");
    case DocumentFormat::Txt:
        return QStringLiteral("txt");
    case DocumentFormat::Markdown:
        return QStringLiteral("md");
    }
    return QStringLiteral("pdf");
}

QList<DocumentFormat> allDocumentFormats()
{
    return {DocumentFormat::Pdf, DocumentFormat::Epub, DocumentFormat::Txt, DocumentFormat::Markdown};
}

std::optional<DocumentFormat> documentFormatFromKey(const QString& key)
{
    if (key == QLatin1String("pdf"))
        return DocumentFormat::Pdf;
    if (key == QLatin1String("epub"))
        return DocumentFormat::Epub;
    if (key == QLatin1String("txt"))
        return DocumentFormat::Txt;
    if (key == QLatin1String("md"))
        return DocumentFormat::Markdown;
    return std::nullopt;
}

QString documentFormatLabel(DocumentFormat f)
{
    switch (f) {
    case DocumentFormat::Pdf:
        return QStringLiteral("PDF");
    case DocumentFormat::Epub:
        return QStringLiteral("EPUB");
    case DocumentFormat::Txt:
        return QStringLiteral("TXT");
    case DocumentFormat::Markdown:
        return QStringLiteral("Markdown");
    }
    return QStringLiteral("PDF");
}

namespace {
QString stripDiacritics(const QString& s)
{
    const QString decomposed = s.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing)
            out.append(c);
    }
    return out;
}
}  // namespace

QString makeSortTitle(const QString& title)
{
    static const QRegularExpression article(
        QStringLiteral("^(l['’]|le |la |les |un |une |des |the |a |an )"), QRegularExpression::CaseInsensitiveOption);
    QString t = title.trimmed().toLower();
    t.remove(article);
    return stripDiacritics(t).trimmed();
}

QString foldForSort(const QString& text)
{
    return stripDiacritics(text.simplified().toLower());
}

QString makeAuthorSortName(const QString& name)
{
    const QString n = name.simplified();
    if (n.contains(QLatin1Char(',')) || !n.contains(QLatin1Char(' ')))
        return n;
    const qsizetype idx = n.lastIndexOf(QLatin1Char(' '));
    return n.mid(idx + 1) + QStringLiteral(", ") + n.left(idx);
}

}  // namespace studylab
