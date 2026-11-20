#include "support/Fixtures.h"

#include <private/qzipwriter_p.h>

#include <QBuffer>
#include <QFont>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QImageWriter>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

#include <algorithm>

namespace studylab::test {

namespace {

/// Police du texte des PDF de test, chargée depuis les sources : sans elle, un PDF écrit sous
/// Windows en mode « offscreen » ne contient aucun glyphe (ni texte sélectionnable, ni recherche).
QFont fixtureFont()
{
    static const QString family = [] {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(STUDYLAB_TEST_FONT));
        const QStringList families = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
        return families.isEmpty() ? QString() : families.first();
    }();
    QFont font(family.isEmpty() ? QStringLiteral("Sans Serif") : family);
    font.setPointSizeF(11);
    return font;
}

}  // namespace

bool writePdf(const QString& path, const PdfSpec& spec)
{
    QPdfWriter w(path);
    w.setTitle(spec.title);
    w.setCreator(QStringLiteral("StudyLab tests"));
    w.setPageSize(QPageSize(QPageSize::A5));
    w.setResolution(72);
    QPainter p;
    if (!p.begin(&w))
        return false;
    p.setFont(fixtureFont());
    for (int i = 0; i < spec.pages; ++i) {
        if (i > 0)
            w.newPage();
        if (spec.scanned) {
            // Scan à 288 dpi (4 pixels par point) : une image de la page, sans couche de texte.
            constexpr int k = 4;
            QImage img(w.width() * k, w.height() * k, QImage::Format_RGB32);
            img.fill(Qt::white);
            QPainter ip(&img);
            ip.scale(k, k);
            QFont big = fixtureFont();
            big.setPointSizeF(14);
            ip.setFont(big);
            ip.setPen(Qt::black);
            const QRectF box(40, 50, w.width() - 80, w.height() - 100);
            ip.drawText(box, Qt::TextWordWrap, QStringLiteral("%1 page %2\n\n%3").arg(spec.title).arg(i + 1).arg(spec.bodyText));
            ip.end();
            p.drawImage(QRectF(0, 0, w.width(), w.height()), img);
            continue;
        }
        p.drawText(QPointF(40, 60), QStringLiteral("%1 — page %2").arg(spec.title).arg(i + 1));
        if (!spec.bodyText.contains(QLatin1Char('\n'))) {
            p.drawText(QPointF(40, 90), spec.bodyText);
            continue;
        }
        // Texte long : paragraphes séparés par une ligne vide, repliés dans la largeur de la page.
        const qreal width = w.width() - 80;
        qreal y = 78;
        for (const QString& para : spec.bodyText.split(QStringLiteral("\n\n"))) {
            const QRectF box = p.boundingRect(QRectF(40, y, width, 10000), Qt::TextWordWrap, para);
            p.drawText(QRectF(40, y, width, box.height()), Qt::TextWordWrap, para);
            y += box.height() + 10;
        }
    }
    return p.end();
}

bool writeEpub(const QString& path, const EpubSpec& spec)
{
    QZipWriter zip(path);
    if (zip.status() != QZipWriter::NoError)
        return false;
    zip.setCompressionPolicy(QZipWriter::NeverCompress);
    zip.addFile(QStringLiteral("mimetype"), QByteArrayLiteral("application/epub+zip"));
    zip.setCompressionPolicy(QZipWriter::AlwaysCompress);
    zip.addFile(QStringLiteral("META-INF/container.xml"),
                QByteArrayLiteral("<?xml version=\"1.0\"?><container version=\"1.0\" "
                                  "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles>"
                                  "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
                                  "</rootfiles></container>"));
    if (spec.drm)
        zip.addFile(QStringLiteral("META-INF/encryption.xml"),
                    QByteArrayLiteral("<encryption xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\" "
                                      "xmlns:enc=\"http://www.w3.org/2001/04/xmlenc#\"><enc:EncryptedData>"
                                      "<enc:EncryptionMethod Algorithm=\"http://www.w3.org/2001/04/xmlenc#aes128-cbc\"/>"
                                      "</enc:EncryptedData></encryption>"));

    QString meta = QStringLiteral("<dc:title>%1</dc:title>").arg(spec.title.toHtmlEscaped());
    int n = 0;
    for (const QString& a : spec.authors) {
        meta += QStringLiteral("<dc:creator id=\"c%1\">%2</dc:creator>").arg(n).arg(a.toHtmlEscaped());
        if (spec.epub3)
            meta += QStringLiteral("<meta refines=\"#c%1\" property=\"role\" scheme=\"marc:relators\">aut</meta>").arg(n);
        ++n;
    }
    meta += QStringLiteral("<dc:creator id=\"ill\" opf:role=\"ill\">Illustrateur Ignoré</dc:creator>");
    meta += QStringLiteral("<dc:language>%1</dc:language><dc:date>%2</dc:date>").arg(spec.language, spec.date);
    meta += QStringLiteral("<dc:identifier id=\"uid\">%1</dc:identifier>")
                .arg(spec.isbn.isEmpty() ? QStringLiteral("urn:uuid:1234") : QStringLiteral("urn:isbn:") + spec.isbn);
    meta += QStringLiteral("<dc:publisher>Éditions Test</dc:publisher>");
    meta += QStringLiteral("<dc:description>&lt;p&gt;Un &lt;b&gt;résumé&lt;/b&gt;.&lt;/p&gt;</dc:description>");
    for (const QString& s : spec.subjects)
        meta += QStringLiteral("<dc:subject>%1</dc:subject>").arg(s.toHtmlEscaped());

    QString manifest, spine;
    for (int i = 0; i < spec.chapters.size(); ++i) {
        const QString file = QStringLiteral("Text/ch%1.xhtml").arg(i + 1);
        manifest += QStringLiteral("<item id=\"ch%1\" href=\"%2\" media-type=\"application/xhtml+xml\"/>").arg(i + 1).arg(file);
        spine += QStringLiteral("<itemref idref=\"ch%1\"/>").arg(i + 1);
        zip.addFile(QStringLiteral("OEBPS/") + file,
                    QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?><html xmlns=\"http://www.w3.org/1999/xhtml\">"
                                   "<head><title>Ch</title></head><body>%1</body></html>")
                        .arg(spec.chapters.at(i))
                        .toUtf8());
    }
    if (!spec.tocTitles.isEmpty()) {
        QString items;
        for (int i = 0; i < spec.tocTitles.size() && i < spec.chapters.size(); ++i)
            items += spec.epub3 ? QStringLiteral("<li><a href=\"Text/ch%1.xhtml\">%2</a></li>").arg(i + 1).arg(spec.tocTitles.at(i).toHtmlEscaped())
                                : QStringLiteral("<navPoint id=\"n%1\" playOrder=\"%1\"><navLabel><text>%2</text></navLabel>"
                                                 "<content src=\"Text/ch%1.xhtml\"/></navPoint>").arg(i + 1).arg(spec.tocTitles.at(i).toHtmlEscaped());
        if (spec.epub3) {
            manifest += QStringLiteral("<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>");
            zip.addFile(QStringLiteral("OEBPS/nav.xhtml"),
                        QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?><html xmlns=\"http://www.w3.org/1999/xhtml\" "
                                       "xmlns:epub=\"http://www.idpf.org/2007/ops\"><head><title>Sommaire</title></head><body>"
                                       "<nav epub:type=\"toc\"><ol>%1</ol></nav></body></html>").arg(items).toUtf8());
        } else {
            manifest += QStringLiteral("<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>");
            zip.addFile(QStringLiteral("OEBPS/toc.ncx"),
                        QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?><ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" "
                                       "version=\"2005-1\"><head/><docTitle><text>T</text></docTitle><navMap>%1</navMap></ncx>").arg(items).toUtf8());
        }
    }
    if (spec.withCover) {
        QImage img(600, 900, QImage::Format_RGB32);
        img.fill(QColor(0x4A, 0x5D, 0x7E));
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        zip.addFile(QStringLiteral("OEBPS/Images/couverture%20finale.png").replace(QStringLiteral("%20"), QStringLiteral(" ")), png);
        if (spec.epub3)
            manifest += QStringLiteral("<item id=\"img\" href=\"Images/couverture%20finale.png\" media-type=\"image/png\" properties=\"cover-image\"/>");
        else {
            manifest += QStringLiteral("<item id=\"img\" href=\"Images/couverture%20finale.png\" media-type=\"image/png\"/>");
            meta += QStringLiteral("<meta name=\"cover\" content=\"img\"/>");
        }
    }
    const QString opf = QStringLiteral(
                            "<?xml version=\"1.0\" encoding=\"utf-8\"?><package xmlns=\"http://www.idpf.org/2007/opf\" "
                            "version=\"%1\" unique-identifier=\"uid\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
                            "xmlns:opf=\"http://www.idpf.org/2007/opf\">%2</metadata><manifest>%3</manifest>"
                            "<spine%5>%4</spine></package>")
                            .arg(spec.epub3 ? QStringLiteral("3.0") : QStringLiteral("2.0"), meta, manifest, spine,
                                 !spec.epub3 && !spec.tocTitles.isEmpty() ? QStringLiteral(" toc=\"ncx\"") : QString());
    zip.addFile(QStringLiteral("OEBPS/content.opf"), opf.toUtf8());
    zip.close();
    return zip.status() == QZipWriter::NoError;
}

bool writeImage(const QString& path, const QString& text, int width, int height, int dpi)
{
    QImage img(width, height, QImage::Format_RGB32);
    img.fill(QColor(250, 248, 240));
    {
        QPainter p(&img);
        QFont font = fixtureFont();
        font.setPixelSize(std::max(12, height / 12));
        p.setFont(font);
        p.setPen(Qt::black);
        p.drawText(img.rect().adjusted(width / 12, height / 12, -width / 12, -height / 12),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
    }
    img.setDotsPerMeterX(qRound(dpi / 0.0254));
    img.setDotsPerMeterY(qRound(dpi / 0.0254));
    QImageWriter writer(path);
    return writer.write(img);
}

bool writeDocx(const QString& path, const QString& bodyText)
{
    const QString ns = QStringLiteral(
        "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"");
    const auto para = [](const QString& style, const QString& runs, const QString& extraPr = {}) {
        QString pr;
        if (!style.isEmpty() || !extraPr.isEmpty())
            pr = QStringLiteral("<w:pPr>%1%2</w:pPr>")
                     .arg(style.isEmpty() ? QString() : QStringLiteral("<w:pStyle w:val=\"%1\"/>").arg(style), extraPr);
        return QStringLiteral("<w:p>%1%2</w:p>").arg(pr, runs);
    };
    const auto run = [](const QString& text, const QString& rpr = {}) {
        return QStringLiteral("<w:r>%1<w:t xml:space=\"preserve\">%2</w:t></w:r>")
            .arg(rpr.isEmpty() ? QString() : QStringLiteral("<w:rPr>%1</w:rPr>").arg(rpr), text);
    };
    const auto item = [&](const QString& text, int num, int lvl) {
        return para(QStringLiteral("ListParagraph"), run(text),
                    QStringLiteral("<w:numPr><w:ilvl w:val=\"%1\"/><w:numId w:val=\"%2\"/></w:numPr>").arg(lvl).arg(num));
    };
    const auto cell = [](const QString& text, const QString& tcPr = {}) {
        return QStringLiteral("<w:tc>%1<w:p><w:r><w:t>%2</w:t></w:r></w:p></w:tc>")
            .arg(tcPr.isEmpty() ? QString() : QStringLiteral("<w:tcPr>%1</w:tcPr>").arg(tcPr), text);
    };

    QString body;
    body += para(QStringLiteral("Title"), run(QStringLiteral("Physique des fluides")));
    body += para({}, run(QStringLiteral("Texte ")) + run(QStringLiteral("gras"), QStringLiteral("<w:b/>")) + run(QStringLiteral(", ")) +
                         run(QStringLiteral("italique"), QStringLiteral("<w:i/>")) + run(QStringLiteral(", ")) +
                         run(QStringLiteral("souligné"), QStringLiteral("<w:u w:val=\"single\"/>")) + run(QStringLiteral(", ")) +
                         run(QStringLiteral("barré"), QStringLiteral("<w:strike/>")) + run(QStringLiteral(", m")) +
                         run(QStringLiteral("2"), QStringLiteral("<w:vertAlign w:val=\"superscript\"/>")) + run(QStringLiteral(", H")) +
                         run(QStringLiteral("2"), QStringLiteral("<w:vertAlign w:val=\"subscript\"/>")) + run(QStringLiteral("O, pas gras"), QStringLiteral("<w:b w:val=\"0\"/>")) +
                         run(QStringLiteral(" et ")) +
                         QStringLiteral("<w:hyperlink r:id=\"rIdLink\"><w:r><w:t>un lien</w:t></w:r></w:hyperlink>") +
                         run(QStringLiteral(" &amp; &lt;fin&gt;.")));
    body += para(QStringLiteral("Heading1"), run(QStringLiteral("Statique")));
    body += para({}, run(bodyText) + QStringLiteral("<w:r><w:footnoteReference w:id=\"1\"/></w:r>") +
                         QStringLiteral("<w:del w:id=\"9\"><w:r><w:delText>supprimé</w:delText></w:r></w:del>") +
                         QStringLiteral("<w:ins w:id=\"10\"><w:r><w:t xml:space=\"preserve\"> Ajouté.</w:t></w:r></w:ins>"));
    body += item(QStringLiteral("Pascal"), 1, 0);
    body += item(QStringLiteral("Premier point"), 1, 1);
    body += item(QStringLiteral("Second point"), 1, 1);
    body += item(QStringLiteral("Archimède"), 1, 0);
    body += para(QStringLiteral("Heading2"), run(QStringLiteral("Unités")));
    body += QStringLiteral("<w:tbl><w:tblPr/><w:tr>%1%2</w:tr><w:tr>%3%4</w:tr><w:tr>%5</w:tr></w:tbl>")
                .arg(cell(QStringLiteral("Grandeur"), QStringLiteral("<w:vMerge w:val=\"restart\"/>")), cell(QStringLiteral("Pa")),
                     cell(QString(), QStringLiteral("<w:vMerge/>")), cell(QStringLiteral("bar")),
                     cell(QStringLiteral("Deux colonnes"), QStringLiteral("<w:gridSpan w:val=\"2\"/>")));
    body += para(QStringLiteral("Heading1"), run(QStringLiteral("Dynamique")));
    body += para(QStringLiteral("Quote"), run(QStringLiteral("Tout corps plongé dans un fluide…")));
    body += para({}, QStringLiteral("<w:r><w:drawing><wp:inline><wp:docPr id=\"1\" name=\"Image 1\" descr=\"Schéma du tube\"/>"
                                    "<a:graphic><a:graphicData><pic:pic><pic:blipFill><a:blip r:embed=\"rIdImg\"/></pic:blipFill>"
                                    "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>"));
    body += item(QStringLiteral("Étape un"), 2, 0);
    body += item(QStringLiteral("Étape deux"), 2, 0);
    body += para({}, run(QStringLiteral("Bernoulli")) + QStringLiteral("<w:r><w:br/></w:r>") + run(QStringLiteral("et Venturi.")));

    const QString document = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                            "<w:document %1><w:body>%2<w:sectPr/></w:body></w:document>")
                                 .arg(ns, body);
    const QString styles = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<w:styles %1>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Title\"><w:name w:val=\"Title\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/><w:pPr><w:outlineLvl w:val=\"0\"/></w:pPr></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Heading2\"><w:name w:val=\"heading 2\"/><w:basedOn w:val=\"Heading1\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"Quote\"><w:name w:val=\"Quote\"/></w:style>"
        "<w:style w:type=\"paragraph\" w:styleId=\"ListParagraph\"><w:name w:val=\"List Paragraph\"/></w:style>"
        "</w:styles>").arg(ns);
    const QString numbering = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<w:numbering %1>"
        "<w:abstractNum w:abstractNumId=\"0\"><w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"bullet\"/></w:lvl>"
        "<w:lvl w:ilvl=\"1\"><w:numFmt w:val=\"decimal\"/></w:lvl></w:abstractNum>"
        "<w:abstractNum w:abstractNumId=\"1\"><w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"decimal\"/></w:lvl></w:abstractNum>"
        "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>"
        "<w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>"
        "</w:numbering>").arg(ns);
    const QString footnotes = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<w:footnotes %1>"
        "<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>"
        "<w:footnote w:id=\"1\"><w:p><w:r><w:t>Blaise Pascal, 1648.</w:t></w:r></w:p></w:footnote>"
        "</w:footnotes>").arg(ns);
    const QByteArray rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rIdLink\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" "
        "Target=\"https://fr.wikipedia.org/wiki/Pression\" TargetMode=\"External\"/>"
        "<Relationship Id=\"rIdImg\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
        "Target=\"media/image1.png\"/>"
        "</Relationships>";
    const QByteArray core =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
        "<dc:title>Physique des fluides</dc:title><dc:creator>Ada Lovelace; Blaise Pascal</dc:creator>"
        "<dc:language>fr-FR</dc:language></cp:coreProperties>";
    const QByteArray contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/><Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>";

    QImage img(80, 40, QImage::Format_RGB32);
    img.fill(QColor(30, 120, 200));
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QZipWriter zip(&file);
    zip.setCompressionPolicy(QZipWriter::AutoCompress);
    zip.addFile(QStringLiteral("[Content_Types].xml"), contentTypes);
    zip.addFile(QStringLiteral("docProps/core.xml"), core);
    zip.addFile(QStringLiteral("word/document.xml"), document.toUtf8());
    zip.addFile(QStringLiteral("word/styles.xml"), styles.toUtf8());
    zip.addFile(QStringLiteral("word/numbering.xml"), numbering.toUtf8());
    zip.addFile(QStringLiteral("word/footnotes.xml"), footnotes.toUtf8());
    zip.addFile(QStringLiteral("word/_rels/document.xml.rels"), rels);
    zip.addFile(QStringLiteral("word/media/image1.png"), png);
    zip.close();
    return zip.status() == QZipWriter::NoError;
}

}  // namespace studylab::test
