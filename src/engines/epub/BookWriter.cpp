#include "engines/epub/BookWriter.h"

#include <private/qzipwriter_p.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace studylab::book {

namespace {

const char* kStyle =
    "pre{white-space:pre-wrap;font-family:monospace;font-size:.9em}"
    "code{font-family:monospace;font-size:.9em}"
    "blockquote{margin-left:0;padding-left:1em;border-left:3px solid rgba(128,128,128,.5)}"
    "table{border-collapse:collapse;margin:1em 0}"
    "th,td{border:1px solid rgba(128,128,128,.5);padding:.2em .5em;vertical-align:top}"
    "img{max-width:100%}"
    "mark{background:rgba(255,214,0,.4);color:inherit}"
    ".wikilink,.link{text-decoration:underline dotted}"
    ".txt p{margin:0 0 .8em}";

QString xhtmlDocument(const QString& title, const QString& body, const QString& lang, bool plainText)
{
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html>\n"
               "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\"%1>\n"
               "<head><meta charset=\"UTF-8\"/><title>%2</title><style>%3</style></head>\n"
               "<body%4>\n%5\n</body>\n</html>\n")
        .arg(lang.isEmpty() ? QString() : QStringLiteral(" lang=\"%1\" xml:lang=\"%1\"").arg(esc(lang)), esc(title),
             QString::fromLatin1(kStyle), plainText ? QStringLiteral(" class=\"txt\"") : QString(), body);
}

QString navDocument(const QString& title, const QList<TocItem>& toc)
{
    QString list;
    int depth = -1;
    for (const TocItem& t : toc) {
        const int d = std::min(t.depth, depth + 1);
        if (d > depth) {
            list += QStringLiteral("<ol>");
        } else {
            list += QStringLiteral("</li>");
            for (int k = depth; k > d; --k)
                list += QStringLiteral("</ol></li>");
        }
        depth = d;
        list += QStringLiteral("<li><a href=\"%1\">%2</a>").arg(esc(t.href), esc(t.label));
    }
    for (int k = depth; k >= 0; --k)
        list += QStringLiteral("</li></ol>");
    return xhtmlDocument(title, QStringLiteral("<nav epub:type=\"toc\" id=\"toc\"><h1>%1</h1>%2</nav>").arg(esc(title), list),
                         {}, false);
}

}  // namespace

QString esc(QString s)
{
    s.replace(u'&', QStringLiteral("&amp;"));
    s.replace(u'<', QStringLiteral("&lt;"));
    s.replace(u'>', QStringLiteral("&gt;"));
    s.replace(u'"', QStringLiteral("&quot;"));
    return s;
}

QString xmlSafe(QString s)
{
    static const QRegularExpression bad(QStringLiteral("[\\x{0}-\\x{8}\\x{B}\\x{C}\\x{E}-\\x{1F}\\x{FFFE}\\x{FFFF}]"));
    return s.remove(bad);
}

QString imageMime(const QString& name)
{
    const QString ext = QFileInfo(name).suffix().toLower();
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg"))
        return QStringLiteral("image/jpeg");
    if (ext == QLatin1String("svg"))
        return QStringLiteral("image/svg+xml");
    return QStringLiteral("image/") + ext;
}

Result<QByteArray> write(const Spec& spec)
{
    QList<TocItem> toc = spec.toc;
    if (toc.isEmpty())
        toc.append({spec.title, QStringLiteral("s1.xhtml"), 0});
    QList<Section> sections = spec.sections;
    if (sections.isEmpty())
        sections.append(Section{});

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QZipWriter zip(&buffer);
    zip.setCompressionPolicy(QZipWriter::NeverCompress);
    zip.addFile(QStringLiteral("mimetype"), QByteArrayLiteral("application/epub+zip"));
    zip.addFile(QStringLiteral("META-INF/container.xml"),
                QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                  "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
                                  "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
                                  "media-type=\"application/oebps-package+xml\"/></rootfiles></container>\n"));
    QString manifest = QStringLiteral("<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n");
    QString spine;
    for (int k = 0; k < sections.size(); ++k) {
        const QString name = QStringLiteral("s%1.xhtml").arg(k + 1);
        const QString sectionTitle = sections.at(k).title.isEmpty() ? spec.title : sections.at(k).title;
        zip.addFile(QStringLiteral("OEBPS/") + name,
                    xhtmlDocument(sectionTitle, sections.at(k).html, spec.lang, spec.plainText).toUtf8());
        manifest += QStringLiteral("<item id=\"s%1\" href=\"%2\" media-type=\"application/xhtml+xml\"/>\n").arg(k + 1).arg(name);
        spine += QStringLiteral("<itemref idref=\"s%1\"/>").arg(k + 1);
    }
    for (int k = 0; k < spec.images.size(); ++k) {
        const Image& img = spec.images.at(k);
        zip.addFile(QStringLiteral("OEBPS/") + img.name, img.data);
        manifest += QStringLiteral("<item id=\"img%1\" href=\"%2\" media-type=\"%3\"/>\n")
                        .arg(k + 1)
                        .arg(esc(img.name), imageMime(img.name));
    }
    zip.addFile(QStringLiteral("OEBPS/nav.xhtml"), navDocument(spec.title, toc).toUtf8());

    QString meta = QStringLiteral("<dc:identifier id=\"uid\">urn:sha1:%1</dc:identifier>\n<dc:title>%2</dc:title>\n")
                       .arg(QString::fromLatin1(QCryptographicHash::hash(spec.identifierSeed, QCryptographicHash::Sha1).toHex()),
                            esc(spec.title));
    for (const QString& a : spec.authors)
        meta += QStringLiteral("<dc:creator>%1</dc:creator>\n").arg(esc(a));
    if (!spec.lang.isEmpty())
        meta += QStringLiteral("<dc:language>%1</dc:language>\n").arg(esc(spec.lang));
    meta += QStringLiteral("<meta property=\"dcterms:modified\">2000-01-01T00:00:00Z</meta>\n");
    const QString opf = QStringLiteral(
                            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                            "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"uid\">\n"
                            "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n%1</metadata>\n"
                            "<manifest>\n%2</manifest>\n<spine>%3</spine>\n</package>\n")
                            .arg(meta, manifest, spine);
    zip.addFile(QStringLiteral("OEBPS/content.opf"), opf.toUtf8());
    zip.close();
    if (zip.status() != QZipWriter::NoError)
        return Error::make(Error::Code::Generic, QStringLiteral("Mise en forme du livre impossible."));
    return buffer.data();
}

}  // namespace studylab::book
