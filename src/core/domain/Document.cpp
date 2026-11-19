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

QString documentFormatKey(DocumentFormat f) { return f == DocumentFormat::Epub ? QStringLiteral("epub") : QStringLiteral("pdf"); }

std::optional<DocumentFormat> documentFormatFromKey(const QString& key)
{
    if (key == QLatin1String("pdf"))
        return DocumentFormat::Pdf;
    if (key == QLatin1String("epub"))
        return DocumentFormat::Epub;
    return std::nullopt;
}

QString documentFormatLabel(DocumentFormat f) { return f == DocumentFormat::Epub ? QStringLiteral("EPUB") : QStringLiteral("PDF"); }

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
