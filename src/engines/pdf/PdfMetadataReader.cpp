#include "engines/pdf/PdfMetadataReader.h"

#include <QPainter>
#include <QPdfDocument>
#include <QRegularExpression>

namespace studylab {

namespace {

// Titres produits par les logiciels et non par l'auteur : on préfère alors le nom du fichier.
bool isJunkTitle(const QString& t)
{
    static const QRegularExpression junk(
        QStringLiteral("^(untitled|sans titre|document\\d*|titre|title|microsoft (word|powerpoint) - .*|"
                       ".*\\.(docx?|pptx?|odt|tex|dvi|indd|qxd|pdf|ps))$"),
        QRegularExpression::CaseInsensitiveOption);
    return t.trimmed().size() < 2 || junk.match(t.trimmed()).hasMatch();
}

QStringList splitAuthors(const QString& raw)
{
    static const QRegularExpression sep(QStringLiteral("\\s*(;|&|\\bet\\b|\\band\\b)\\s*"),
                                        QRegularExpression::CaseInsensitiveOption);
    QStringList out;
    for (const QString& a : raw.split(sep, Qt::SkipEmptyParts)) {
        const QString s = a.simplified();
        if (!s.isEmpty())
            out << s;
    }
    return out;
}

}  // namespace

Result<DocumentMetadata> PdfMetadataReader::read(const QString& path, int coverWidth)
{
    DocumentMetadata md;
    QPdfDocument doc;
    const auto err = doc.load(path);
    if (err == QPdfDocument::Error::IncorrectPassword) {
        md.passwordProtected = true;
        md.title = titleFromFileName(path);
        return md;
    }
    if (err != QPdfDocument::Error::None || doc.status() != QPdfDocument::Status::Ready)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("PDF illisible ou endommagé."));
    if (doc.pageCount() <= 0)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("PDF sans aucune page."));

    md.pageCount = doc.pageCount();
    const QString title = doc.metaData(QPdfDocument::MetaDataField::Title).toString().simplified();
    md.title = isJunkTitle(title) ? titleFromFileName(path) : title;
    md.authors = splitAuthors(doc.metaData(QPdfDocument::MetaDataField::Author).toString());
    md.description = doc.metaData(QPdfDocument::MetaDataField::Subject).toString().simplified();
    const QString keywords = doc.metaData(QPdfDocument::MetaDataField::Keywords).toString();
    for (const QString& k : keywords.split(QRegularExpression(QStringLiteral("[;,]")), Qt::SkipEmptyParts))
        if (const QString s = k.simplified(); !s.isEmpty() && s.size() <= 40)
            md.keywords << s;
    // L'année n'est pas déduite de CreationDate : c'est la date de production du fichier, pas de publication.
    md.isbn = extractIsbn(keywords + QLatin1Char(' ') + md.description);
    md.doi = extractDoi(keywords + QLatin1Char(' ') + md.description + QLatin1Char(' ') + title);

    const QSizeF pt = doc.pagePointSize(0);
    if (coverWidth > 0 && pt.width() > 0 && pt.height() > 0) {
        const QSize size(coverWidth, qMin(coverWidth * 3, qRound(coverWidth * pt.height() / pt.width())));
        const QImage page = doc.render(0, size);
        if (!page.isNull()) {
            QImage cover(page.size(), QImage::Format_RGB32);
            cover.fill(Qt::white);  // les pages transparentes s'affichent sur fond papier
            QPainter p(&cover);
            p.drawImage(0, 0, page);
            p.end();
            md.cover = cover;
        }
    }
    doc.close();
    return md;
}

}  // namespace studylab
