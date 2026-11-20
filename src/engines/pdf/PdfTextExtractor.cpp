#include "engines/pdf/PdfTextExtractor.h"

#include "engines/document/TextUnits.h"

#include <QPdfDocument>
#include <QPdfSelection>

namespace studylab {

Result<QStringList> PdfTextExtractor::extract(const QString& path, const std::atomic_bool* cancel)
{
    QPdfDocument doc;
    const QPdfDocument::Error err = doc.load(path);
    if (err == QPdfDocument::Error::IncorrectPassword)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("PDF protégé par mot de passe"));
    if (err != QPdfDocument::Error::None)
        return Error::make(Error::Code::Io, QStringLiteral("PDF illisible : %1").arg(path));
    QStringList pages;
    pages.reserve(doc.pageCount());
    for (int i = 0; i < doc.pageCount(); ++i) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return Error::make(Error::Code::Generic, QStringLiteral("Extraction interrompue"));
        pages << normalizeExtractedText(doc.getAllText(i).text());
    }
    return pages;
}

}  // namespace studylab
