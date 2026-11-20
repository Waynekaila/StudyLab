#include "engines/epub/EpubTextExtractor.h"

#include "engines/document/TextUnits.h"
#include "engines/epub/EpubPackage.h"

#include <QSet>
#include <QTextDocumentFragment>
#include <QXmlStreamReader>

namespace studylab {

QString EpubTextExtractor::xhtmlText(const QByteArray& xhtml)
{
    static const QSet<QString> blocks{
        QStringLiteral("p"), QStringLiteral("div"), QStringLiteral("br"), QStringLiteral("li"), QStringLiteral("h1"),
        QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"),
        QStringLiteral("tr"), QStringLiteral("blockquote"), QStringLiteral("section"), QStringLiteral("pre"),
        QStringLiteral("dt"), QStringLiteral("dd"), QStringLiteral("figcaption"), QStringLiteral("td"),
        QStringLiteral("th"), QStringLiteral("hr"), QStringLiteral("article"), QStringLiteral("aside")};
    static const QSet<QString> skipped{QStringLiteral("script"), QStringLiteral("style"), QStringLiteral("head")};

    QString out;
    QXmlStreamReader x(xhtml);
    int skip = 0;
    while (!x.atEnd()) {
        switch (x.readNext()) {
        case QXmlStreamReader::StartElement: {
            const QString name = x.name().toString().toLower();
            if (skipped.contains(name))
                ++skip;
            else if (blocks.contains(name))
                out += QLatin1Char('\n');
            break;
        }
        case QXmlStreamReader::EndElement: {
            const QString name = x.name().toString().toLower();
            if (skipped.contains(name))
                skip = std::max(0, skip - 1);
            else if (blocks.contains(name))
                out += QLatin1Char('\n');
            break;
        }
        case QXmlStreamReader::Characters:
            if (skip == 0)
                out += x.text();
            break;
        default:
            break;
        }
    }
    if (x.hasError()) {
        // XHTML mal formé ou tronqué, entité HTML non déclarée (&nbsp;…) : lecture tolérante de l'analyseur HTML.
        return normalizeExtractedText(QTextDocumentFragment::fromHtml(QString::fromUtf8(xhtml)).toPlainText());
    }
    out.replace(QLatin1Char('\n'), QStringLiteral("\n\n"));
    return normalizeExtractedText(out);
}

Result<QStringList> EpubTextExtractor::extract(const QString& path, const std::atomic_bool* cancel)
{
    auto pkg = EpubPackage::open(path);
    if (!pkg)
        return pkg.error();
    QStringList sections;
    for (const EpubManifestItem& item : pkg.value()->spine()) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return Error::make(Error::Code::Generic, QStringLiteral("Extraction interrompue"));
        sections << xhtmlText(pkg.value()->fileData(item.href));
    }
    return sections;
}

}  // namespace studylab
