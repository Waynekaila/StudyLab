#include "engines/epub/TextBook.h"

#include "core/domain/Note.h"
#include "engines/document/DocumentMetadata.h"

#include <private/qzipwriter_p.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>

#include <functional>

namespace studylab {

namespace {

constexpr int kTextPartChars = 40000;  // texte brut : taille d'une partie (section du lecteur)

QString esc(QString s)
{
    s.replace(u'&', QStringLiteral("&amp;"));
    s.replace(u'<', QStringLiteral("&lt;"));
    s.replace(u'>', QStringLiteral("&gt;"));
    s.replace(u'"', QStringLiteral("&quot;"));
    return s;
}

/// Caractères interdits en XML 1.0 (contrôles autres que tabulation et fins de ligne).
QString xmlSafe(QString s)
{
    static const QRegularExpression bad(QStringLiteral("[\\x{0}-\\x{8}\\x{B}\\x{C}\\x{E}-\\x{1F}\\x{FFFE}\\x{FFFF}]"));
    return s.remove(bad);
}

int indentOf(const QString& line)
{
    int n = 0;
    for (QChar c : line) {
        if (c == u' ')
            ++n;
        else if (c == u'\t')
            n += 4 - n % 4;
        else
            break;
    }
    return n;
}

/// Retire jusqu'à `n` colonnes d'indentation.
QString dedent(const QString& line, int n)
{
    int col = 0;
    int i = 0;
    while (i < line.size() && col < n) {
        if (line.at(i) == u' ')
            ++col;
        else if (line.at(i) == u'\t')
            col += 4 - col % 4;
        else
            break;
        ++i;
    }
    return line.mid(i);
}

bool isBlank(const QString& line) { return line.trimmed().isEmpty(); }

struct Block {
    QString html;
    int heading = 0;  ///< niveau de titre, 0 sinon
    QString text;     ///< texte du titre
    QString id;
};

Block htmlBlock(const QString& html)
{
    Block b;
    b.html = html;
    return b;
}

const QRegularExpression& fenceRe()
{
    static const QRegularExpression re(QStringLiteral("^ {0,3}(`{3,}|~{3,})\\s*([^`]*)$"));
    return re;
}
const QRegularExpression& atxRe()
{
    static const QRegularExpression re(QStringLiteral("^ {0,3}(#{1,6})(?:[ \\t]+(.*?))?(?:[ \\t]+#+)?[ \\t]*$"));
    return re;
}
const QRegularExpression& hrRe()
{
    static const QRegularExpression re(QStringLiteral("^ {0,3}(?:(?:-[ \\t]*){3,}|(?:\\*[ \\t]*){3,}|(?:_[ \\t]*){3,})$"));
    return re;
}
const QRegularExpression& quoteRe()
{
    static const QRegularExpression re(QStringLiteral("^ {0,3}> ?(.*)$"));
    return re;
}
const QRegularExpression& listRe()
{
    static const QRegularExpression re(QStringLiteral("^([ \\t]*)([-*+]|\\d{1,9}[.)])([ \\t]+)(.*)$"));
    return re;
}
const QRegularExpression& tableSepRe()
{
    static const QRegularExpression re(QStringLiteral("^ {0,3}\\|?[ \\t]*:?-+:?[ \\t]*(?:\\|[ \\t]*:?-+:?[ \\t]*)*\\|?[ \\t]*$"));
    return re;
}

/// Convertisseur Markdown → XHTML (sous-ensemble CommonMark / GFM courant dans les notes et les cours).
class MarkdownConverter {
public:
    using ImageResolver = std::function<QString(const QString& src, const QString& alt)>;
    explicit MarkdownConverter(ImageResolver images) : m_images(std::move(images)) {}

    QList<Block> parse(const QStringList& lines, bool top)
    {
        QList<Block> out;
        int i = 0;
        const int n = int(lines.size());
        while (i < n) {
            const QString& line = lines.at(i);
            if (isBlank(line)) {
                ++i;
                continue;
            }
            // Bloc de code délimité
            if (const auto m = fenceRe().match(line); m.hasMatch()) {
                const QString fence = m.captured(1);
                const int indent = indentOf(line);
                QStringList code;
                ++i;
                while (i < n) {
                    const QString t = lines.at(i).trimmed();
                    if (t.startsWith(fence) && t.count(fence.at(0)) == t.size() && indentOf(lines.at(i)) < 4) {
                        ++i;
                        break;
                    }
                    code << dedent(lines.at(i), indent);
                    ++i;
                }
                out.append(htmlBlock(QStringLiteral("<pre><code>%1</code></pre>").arg(esc(code.join(u'\n')))));
                continue;
            }
            // Bloc de formules $$ … $$
            if (line.trimmed().startsWith(QLatin1String("$$"))) {
                QStringList math{line.trimmed()};
                const bool closed = line.trimmed().size() > 2 && line.trimmed().endsWith(QLatin1String("$$"));
                ++i;
                while (!closed && i < n) {
                    math << lines.at(i);
                    if (lines.at(i).trimmed().endsWith(QLatin1String("$$"))) {
                        ++i;
                        break;
                    }
                    ++i;
                }
                out.append(htmlBlock(QStringLiteral("<pre class=\"math\">%1</pre>").arg(esc(math.join(u'\n')))));
                continue;
            }
            if (const auto m = atxRe().match(line); m.hasMatch()) {
                out.append(heading(int(m.captured(1).size()), m.captured(2), top));
                ++i;
                continue;
            }
            if (hrRe().match(line).hasMatch()) {
                out.append(htmlBlock(QStringLiteral("<hr/>")));
                ++i;
                continue;
            }
            if (quoteRe().match(line).hasMatch()) {
                QStringList inner;
                while (i < n && !isBlank(lines.at(i))) {
                    const auto q = quoteRe().match(lines.at(i));
                    inner << (q.hasMatch() ? q.captured(1) : lines.at(i));  // continuation paresseuse
                    ++i;
                }
                out.append(htmlBlock(QStringLiteral("<blockquote>%1</blockquote>").arg(render(inner))));
                continue;
            }
            if (const auto m = listRe().match(line); m.hasMatch() && indentOf(line) < 4) {
                out.append(htmlBlock(list(lines, i)));
                continue;
            }
            if (line.contains(u'|') && i + 1 < n && tableSepRe().match(lines.at(i + 1)).hasMatch()
                && lines.at(i + 1).contains(u'-')) {
                out.append(htmlBlock(table(lines, i)));
                continue;
            }
            // Paragraphe (avec titre souligné « === » / « --- »)
            QStringList para;
            while (i < n && !isBlank(lines.at(i))) {
                const QString& l = lines.at(i);
                if (!para.isEmpty()) {
                    static const QRegularExpression setext1(QStringLiteral("^ {0,3}=+[ \\t]*$"));
                    static const QRegularExpression setext2(QStringLiteral("^ {0,3}-+[ \\t]*$"));
                    if (setext1.match(l).hasMatch() || setext2.match(l).hasMatch()) {
                        out.append(heading(setext1.match(l).hasMatch() ? 1 : 2, para.join(u' ').trimmed(), top));
                        para.clear();
                        ++i;
                        break;
                    }
                    if (startsBlock(l))
                        break;
                }
                para << l;
                ++i;
            }
            if (!para.isEmpty())
                out.append(htmlBlock(QStringLiteral("<p>%1</p>").arg(paragraph(para))));
        }
        return out;
    }

    QString render(const QStringList& lines)
    {
        QString html;
        for (const Block& b : parse(lines, false))
            html += b.html;
        return html;
    }

    QString inlineHtml(const QString& text, bool links = true)
    {
        static const QRegularExpression code(QStringLiteral("(`+)(.+?)(?<!`)\\1(?!`)"));
        static const QRegularExpression wiki(QStringLiteral("(!?)\\[\\[([^\\]|]+?)(?:\\|([^\\]]+))?\\]\\]"));
        static const QRegularExpression image(
            QStringLiteral("!\\[([^\\]]*)\\]\\(\\s*<?([^)\\s>]+)>?(?:\\s+\"[^\"]*\")?\\s*\\)"));
        static const QRegularExpression link(
            QStringLiteral("\\[((?:[^\\[\\]]|\\[[^\\]]*\\])+)\\]\\(\\s*<?([^)\\s>]+)>?(?:\\s+\"[^\"]*\")?\\s*\\)"));
        static const QRegularExpression autolink(QStringLiteral("<((?:https?|mailto):[^>\\s]+)>"));
        static const QRegularExpression math(QStringLiteral("\\$(?=\\S)([^$\\n]+?)(?<=\\S)\\$"));
        static const QRegularExpression escaped(QStringLiteral("\\\\([\\\\`*_{}\\[\\]()#+\\-.!|~=$<>])"));
        const QList<const QRegularExpression*> patterns{&escaped, &code, &wiki, &image, &link, &autolink, &math};

        QString out;
        qsizetype pos = 0;
        while (pos < text.size()) {
            QRegularExpressionMatch best;
            int which = -1;
            for (int k = 0; k < patterns.size(); ++k) {
                if (!links && (patterns.at(k) == &link || patterns.at(k) == &autolink))
                    continue;
                const auto m = patterns.at(k)->match(text, pos);
                if (m.hasMatch() && (which < 0 || m.capturedStart() < best.capturedStart())) {
                    best = m;
                    which = k;
                }
            }
            if (which < 0)
                break;
            out += emphasis(text.mid(pos, best.capturedStart() - pos));
            const QRegularExpression* p = patterns.at(which);
            if (p == &escaped) {
                out += esc(best.captured(1));
            } else if (p == &code) {
                out += QStringLiteral("<code>%1</code>").arg(esc(best.captured(2).trimmed()));
            } else if (p == &wiki) {
                const QString target = best.captured(2).trimmed();
                if (!best.captured(1).isEmpty() && isImagePath(target))
                    out += m_images(target, QFileInfo(target).completeBaseName());
                else
                    out += QStringLiteral("<span class=\"wikilink\">%1</span>")
                               .arg(esc(best.captured(3).isEmpty() ? target : best.captured(3).trimmed()));
            } else if (p == &image) {
                out += m_images(best.captured(2), best.captured(1));
            } else if (p == &link) {
                const QString href = best.captured(2);
                const QString label = inlineHtml(best.captured(1), false);
                if (isExternal(href))
                    out += QStringLiteral("<a href=\"%1\">%2</a>").arg(esc(href), label);
                else
                    out += QStringLiteral("<span class=\"link\">%1</span>").arg(label);
            } else if (p == &autolink) {
                out += QStringLiteral("<a href=\"%1\">%1</a>").arg(esc(best.captured(1)));
            } else if (p == &math) {
                out += QStringLiteral("<span class=\"math\">%1</span>").arg(esc(best.captured(0)));
            }
            pos = best.capturedEnd();
        }
        out += emphasis(text.mid(pos));
        return out;
    }

    /// Texte sans balisage (titres du sommaire).
    static QString plain(const QString& html)
    {
        static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
        QString t = html;
        t.remove(tag);
        t.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        t.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        t.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        t.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        return t.simplified();
    }

    static bool isImagePath(const QString& path)
    {
        static const QStringList exts{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                      QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("svg")};
        return exts.contains(QFileInfo(path).suffix().toLower());
    }

private:
    static bool isExternal(const QString& href)
    {
        return href.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || href.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
            || href.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive);
    }

    static QString emphasis(const QString& raw)
    {
        static const QList<QPair<QRegularExpression, QString>> rules{
            {QRegularExpression(QStringLiteral("\\*\\*(?=\\S)(.+?)(?<=\\S)\\*\\*")), QStringLiteral("<strong>\\1</strong>")},
            {QRegularExpression(QStringLiteral("(?<!\\w)__(?=\\S)(.+?)(?<=\\S)__(?!\\w)")), QStringLiteral("<strong>\\1</strong>")},
            {QRegularExpression(QStringLiteral("\\*(?=\\S)(.+?)(?<=\\S)\\*")), QStringLiteral("<em>\\1</em>")},
            {QRegularExpression(QStringLiteral("(?<!\\w)_(?=\\S)(.+?)(?<=\\S)_(?!\\w)")), QStringLiteral("<em>\\1</em>")},
            {QRegularExpression(QStringLiteral("~~(?=\\S)(.+?)(?<=\\S)~~")), QStringLiteral("<del>\\1</del>")},
            {QRegularExpression(QStringLiteral("==(?=\\S)(.+?)(?<=\\S)==")), QStringLiteral("<mark>\\1</mark>")},
        };
        QString s = esc(raw);
        for (const auto& [re, repl] : rules)
            s.replace(re, repl);
        return s;
    }

    static bool startsBlock(const QString& l)
    {
        if (fenceRe().match(l).hasMatch() || atxRe().match(l).hasMatch() || hrRe().match(l).hasMatch()
            || quoteRe().match(l).hasMatch())
            return true;
        // Une liste à puces interrompt un paragraphe ; une liste numérotée seulement si elle commence à 1.
        const auto m = listRe().match(l);
        if (m.hasMatch() && indentOf(l) < 4 && !m.captured(4).trimmed().isEmpty()) {
            const QString marker = m.captured(2);
            return !marker.at(0).isDigit() || marker.chopped(1) == QLatin1String("1");
        }
        return false;
    }

    QString paragraph(const QStringList& lines)
    {
        QString html;
        for (int k = 0; k < lines.size(); ++k) {
            QString l = lines.at(k);
            const bool hardBreak = l.endsWith(QLatin1String("  ")) || (l.endsWith(u'\\') && !l.endsWith(QLatin1String("\\\\")));
            l = l.trimmed();
            if (hardBreak && l.endsWith(u'\\'))
                l.chop(1);
            html += inlineHtml(l);
            if (k + 1 < lines.size())
                html += hardBreak ? QStringLiteral("<br/>\n") : QStringLiteral("\n");
        }
        return html;
    }

    Block heading(int level, const QString& text, bool top)
    {
        Block b;
        b.heading = level;
        const QString inner = inlineHtml(text.trimmed());
        b.text = plain(inner);
        if (top)
            b.id = QStringLiteral("t%1").arg(++m_headingCount);
        b.html = b.id.isEmpty() ? QStringLiteral("<h%1>%2</h%1>").arg(level).arg(inner)
                                : QStringLiteral("<h%1 id=\"%3\">%2</h%1>").arg(level).arg(inner, b.id);
        return b;
    }

    QString list(const QStringList& lines, int& i)
    {
        const int n = int(lines.size());
        const auto first = listRe().match(lines.at(i));
        const bool ordered = first.captured(2).at(0).isDigit();
        const int base = indentOf(lines.at(i));
        QList<QStringList> items;
        int contentIndent = 0;
        while (i < n) {
            const QString& l = lines.at(i);
            const auto m = listRe().match(l);
            if (!m.hasMatch() || indentOf(l) >= base + 4 || m.captured(2).at(0).isDigit() != ordered
                || (items.size() > 0 && indentOf(l) >= contentIndent))
                break;
            const int spaces = int(m.captured(3).size());
            contentIndent = indentOf(l) + int(m.captured(2).size()) + (spaces > 4 ? 1 : spaces);
            QStringList item{m.captured(4)};
            ++i;
            while (i < n) {
                const QString& c = lines.at(i);
                if (isBlank(c)) {
                    int j = i + 1;
                    while (j < n && isBlank(lines.at(j)))
                        ++j;
                    if (j < n && indentOf(lines.at(j)) >= contentIndent) {
                        item << QString();
                        i = j;
                        continue;
                    }
                    break;  // fin de l'élément ; la boucle extérieure voit si la liste continue
                }
                if (indentOf(c) >= contentIndent) {
                    item << dedent(c, contentIndent);
                    ++i;
                    continue;
                }
                const auto next = listRe().match(c);
                if (next.hasMatch() || startsBlock(c))
                    break;
                item << c.trimmed();  // continuation paresseuse
                ++i;
            }
            items << item;
            // Ligne vide entre deux éléments : la liste continue si le suivant est du même type.
            int j = i;
            while (j < n && isBlank(lines.at(j)))
                ++j;
            if (j < n && j > i) {
                const auto m2 = listRe().match(lines.at(j));
                if (m2.hasMatch() && m2.captured(2).at(0).isDigit() == ordered && indentOf(lines.at(j)) < base + 4)
                    i = j;
                else
                    break;
            }
        }
        QString html;
        const int start = ordered ? first.captured(2).chopped(1).toInt() : 1;
        html += ordered ? (start != 1 ? QStringLiteral("<ol start=\"%1\">").arg(start) : QStringLiteral("<ol>"))
                        : QStringLiteral("<ul>");
        for (QStringList item : items) {
            QString prefix;
            static const QRegularExpression task(QStringLiteral("^\\[([ xX])\\]\\s+"));
            if (const auto t = task.match(item.first()); t.hasMatch()) {
                prefix = t.captured(1) == QLatin1String(" ") ? QStringLiteral("☐ ") : QStringLiteral("☑ ");
                item.first().remove(0, t.capturedLength());
            }
            QString inner = render(item);
            // Liste serrée (sans ligne vide dans l'élément) : pas de <p> autour du premier paragraphe.
            if (!item.contains(QString()) && inner.startsWith(QLatin1String("<p>"))) {
                const qsizetype end = inner.indexOf(QLatin1String("</p>"));
                inner = esc(prefix) + inner.mid(3, end - 3) + inner.mid(end + 4);
                prefix.clear();
            } else if (inner.startsWith(QLatin1String("<p>")) && !prefix.isEmpty()) {
                inner.insert(3, esc(prefix));
                prefix.clear();
            }
            html += QStringLiteral("<li>%1%2</li>").arg(esc(prefix), inner);
        }
        html += ordered ? QStringLiteral("</ol>") : QStringLiteral("</ul>");
        return html;
    }

    static QStringList cells(QString row)
    {
        row = row.trimmed();
        row.replace(QStringLiteral("\\|"), QStringLiteral("\x01"));
        if (row.startsWith(u'|'))
            row.remove(0, 1);
        if (row.endsWith(u'|'))
            row.chop(1);
        QStringList out;
        for (QString c : row.split(u'|'))
            out << c.replace(u'\x01', u'|').trimmed();
        return out;
    }

    QString table(const QStringList& lines, int& i)
    {
        const QStringList head = cells(lines.at(i));
        QStringList align;
        for (const QString& s : cells(lines.at(i + 1))) {
            const bool l = s.startsWith(u':');
            const bool r = s.endsWith(u':');
            align << (l && r ? QStringLiteral("center") : r ? QStringLiteral("right") : l ? QStringLiteral("left") : QString());
        }
        auto cell = [&](const QString& tag, const QString& text, int col) {
            const QString a = col < align.size() ? align.at(col) : QString();
            return a.isEmpty() ? QStringLiteral("<%1>%2</%1>").arg(tag, inlineHtml(text))
                               : QStringLiteral("<%1 style=\"text-align:%3\">%2</%1>").arg(tag, inlineHtml(text), a);
        };
        QString html = QStringLiteral("<table><thead><tr>");
        for (int c = 0; c < head.size(); ++c)
            html += cell(QStringLiteral("th"), head.at(c), c);
        html += QStringLiteral("</tr></thead><tbody>");
        i += 2;
        while (i < lines.size() && !isBlank(lines.at(i)) && lines.at(i).contains(u'|')) {
            const QStringList row = cells(lines.at(i));
            html += QStringLiteral("<tr>");
            for (int c = 0; c < head.size(); ++c)
                html += cell(QStringLiteral("td"), c < row.size() ? row.at(c) : QString(), c);
            html += QStringLiteral("</tr>");
            ++i;
        }
        return html + QStringLiteral("</tbody></table>");
    }

    ImageResolver m_images;
    int m_headingCount = 0;
};

struct Section {
    QString html;
    QString title;
};

struct TocItem {
    QString label;
    QString href;
    int depth = 0;
};

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

QStringList frontMatterList(const QJsonValue& v)
{
    QStringList out;
    if (v.isArray()) {
        for (const QJsonValue& x : v.toArray())
            if (!x.toString().trimmed().isEmpty())
                out << x.toString().trimmed();
    } else if (!v.toString().trimmed().isEmpty()) {
        for (const QString& s : v.toString().split(QRegularExpression(QStringLiteral("\\s*;\\s*|\\s+(?:and|et)\\s+"))))
            if (!s.trimmed().isEmpty())
                out << s.trimmed();
    }
    return out;
}

}  // namespace

bool TextBook::isMarkdownFile(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("md") || s == QLatin1String("markdown");
}

bool TextBook::isTextFile(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("txt") || s == QLatin1String("text") || isMarkdownFile(path);
}

Result<QString> TextBook::readText(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return Error::make(Error::Code::Io, QStringLiteral("Lecture impossible : %1").arg(f.errorString()));
    const QByteArray data = f.readAll();
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8.decode(data);
    if (utf8.hasError()) {
        QStringDecoder legacy("windows-1252");
        text = legacy.isValid() ? legacy.decode(data) : QString::fromLatin1(data);
    }
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(u'\r', u'\n');
    return xmlSafe(text);
}

QString TextBook::markdownToXhtml(const QString& markdown)
{
    MarkdownConverter conv([](const QString&, const QString& alt) {
        return QStringLiteral("<span class=\"image\">[%1]</span>").arg(esc(alt));
    });
    QString html;
    for (const Block& b : conv.parse(xmlSafe(markdown).split(u'\n'), true))
        html += b.html + u'\n';
    return html;
}

Result<QByteArray> TextBook::build(const QString& path)
{
    auto read = readText(path);
    if (!read)
        return read.error();
    const bool markdown = isMarkdownFile(path);
    QString text = read.value();

    QString title = titleFromFileName(path);
    QStringList authors;
    QString lang;
    QList<Section> sections;
    QList<TocItem> toc;
    QList<QPair<QString, QString>> images;  // (chemin dans l'archive, fichier)
    QHash<QString, QString> imageByFile;

    if (markdown) {
        const FrontMatter fm = parseFrontMatter(text);
        if (fm.present) {
            text = text.mid(fm.bodyStart);
            if (const QString t = fm.values.value(QStringLiteral("title")).toString().trimmed(); !t.isEmpty())
                title = t;
            authors = frontMatterList(fm.values.value(QStringLiteral("author")));
            if (authors.isEmpty())
                authors = frontMatterList(fm.values.value(QStringLiteral("authors")));
            lang = fm.values.value(QStringLiteral("lang")).toString().trimmed();
            if (lang.isEmpty())
                lang = fm.values.value(QStringLiteral("language")).toString().trimmed();
        }
        const QDir dir = QFileInfo(path).absoluteDir();
        MarkdownConverter conv([&](const QString& src, const QString& alt) -> QString {
            const QString decoded = QUrl::fromPercentEncoding(src.toUtf8());
            const QString file = QFileInfo(dir.absoluteFilePath(decoded)).absoluteFilePath();
            if (decoded.contains(QLatin1String("://")) || !MarkdownConverter::isImagePath(decoded) || !QFileInfo(file).isFile())
                return QStringLiteral("<span class=\"image\">[%1]</span>").arg(esc(alt.isEmpty() ? QFileInfo(decoded).fileName() : alt));
            QString name = imageByFile.value(file);
            if (name.isEmpty()) {
                name = QStringLiteral("images/i%1.%2").arg(images.size() + 1).arg(QFileInfo(file).suffix().toLower());
                imageByFile.insert(file, name);
                images.append({name, file});
            }
            return QStringLiteral("<img src=\"%1\" alt=\"%2\"/>").arg(name, esc(alt));
        });
        const QList<Block> blocks = conv.parse(text.split(u'\n'), true);

        // Découpage : aux titres du plus haut niveau qui se répète (au plus ###).
        QHash<int, int> counts;
        int minLevel = 7;
        QString firstH1;
        for (const Block& b : blocks) {
            if (b.heading > 0) {
                counts[b.heading]++;
                minLevel = std::min(minLevel, b.heading);
                if (b.heading == 1 && firstH1.isEmpty())
                    firstH1 = b.text;
            }
        }
        int split = 0;
        for (int level = 1; level <= 3; ++level)
            if (counts.value(level) >= 2) {
                split = level;
                break;
            }
        if (!fm.present || fm.values.value(QStringLiteral("title")).toString().trimmed().isEmpty())
            if (!firstH1.isEmpty() && counts.value(1) == 1)
                title = firstH1;

        Section current;
        for (const Block& b : blocks) {
            if (split > 0 && b.heading > 0 && b.heading <= split && !current.html.isEmpty()) {
                sections << current;
                current = {};
            }
            if (b.heading > 0 && current.title.isEmpty())
                current.title = b.text;
            if (b.heading > 0 && (split == 0 ? b.heading <= minLevel + 1 : b.heading <= split + 1))
                toc.append({b.text, QStringLiteral("s%1.xhtml#%2").arg(sections.size() + 1).arg(b.id),
                            b.heading - std::min(minLevel, 6)});
            current.html += b.html + u'\n';
        }
        if (!current.html.isEmpty() || sections.isEmpty())
            sections << current;
    } else {
        // Texte brut : paragraphes séparés par une ligne vide, retours à la ligne conservés.
        static const QRegularExpression blank(QStringLiteral("\\n[ \\t]*\\n\\s*"));
        Section current;
        for (const QString& para : text.split(blank, Qt::SkipEmptyParts)) {
            if (para.trimmed().isEmpty())
                continue;
            QStringList lines;
            for (const QString& l : para.trimmed().split(u'\n'))
                lines << esc(l.trimmed());
            if (current.html.size() > kTextPartChars) {
                sections << current;
                current = {};
            }
            current.html += QStringLiteral("<p>%1</p>\n").arg(lines.join(QStringLiteral("<br/>\n")));
        }
        if (!current.html.isEmpty() || sections.isEmpty())
            sections << current;
        for (int k = 0; k < sections.size(); ++k)
            toc.append({sections.size() == 1 ? title : QStringLiteral("Partie %1").arg(k + 1),
                        QStringLiteral("s%1.xhtml").arg(k + 1), 0});
    }
    if (toc.isEmpty())
        toc.append({title, QStringLiteral("s1.xhtml"), 0});

    // Archive EPUB 3
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
        const QString sectionTitle = sections.at(k).title.isEmpty() ? title : sections.at(k).title;
        zip.addFile(QStringLiteral("OEBPS/") + name, xhtmlDocument(sectionTitle, sections.at(k).html, lang, !markdown).toUtf8());
        manifest += QStringLiteral("<item id=\"s%1\" href=\"%2\" media-type=\"application/xhtml+xml\"/>\n").arg(k + 1).arg(name);
        spine += QStringLiteral("<itemref idref=\"s%1\"/>").arg(k + 1);
    }
    for (int k = 0; k < images.size(); ++k) {
        QFile img(images.at(k).second);
        if (!img.open(QIODevice::ReadOnly))
            continue;
        const QString ext = QFileInfo(images.at(k).first).suffix();
        const QString mime = ext == QLatin1String("jpg") ? QStringLiteral("image/jpeg")
                           : ext == QLatin1String("svg") ? QStringLiteral("image/svg+xml")
                                                         : QStringLiteral("image/") + ext;
        zip.addFile(QStringLiteral("OEBPS/") + images.at(k).first, img.readAll());
        manifest += QStringLiteral("<item id=\"img%1\" href=\"%2\" media-type=\"%3\"/>\n").arg(k + 1).arg(images.at(k).first, mime);
    }
    zip.addFile(QStringLiteral("OEBPS/nav.xhtml"), navDocument(title, toc).toUtf8());

    QString meta = QStringLiteral("<dc:identifier id=\"uid\">urn:sha1:%1</dc:identifier>\n<dc:title>%2</dc:title>\n")
                       .arg(QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex()),
                            esc(title));
    for (const QString& a : authors)
        meta += QStringLiteral("<dc:creator>%1</dc:creator>\n").arg(esc(a));
    if (!lang.isEmpty())
        meta += QStringLiteral("<dc:language>%1</dc:language>\n").arg(esc(lang));
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
        return Error::make(Error::Code::Generic, QStringLiteral("Mise en forme du texte impossible."));
    return buffer.data();
}

}  // namespace studylab
