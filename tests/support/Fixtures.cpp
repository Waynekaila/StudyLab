#include "support/Fixtures.h"

#include <private/qzipwriter_p.h>

#include <QBuffer>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

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
        p.drawText(QPointF(40, 60), QStringLiteral("%1 — page %2").arg(spec.title).arg(i + 1));
        p.drawText(QPointF(40, 90), spec.bodyText);
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
                            "<spine>%4</spine></package>")
                            .arg(spec.epub3 ? QStringLiteral("3.0") : QStringLiteral("2.0"), meta, manifest, spine);
    zip.addFile(QStringLiteral("OEBPS/content.opf"), opf.toUtf8());
    zip.close();
    return zip.status() == QZipWriter::NoError;
}

}  // namespace studylab::test
