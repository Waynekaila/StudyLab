#include "engines/epub/DocxBook.h"

#include "engines/epub/BookWriter.h"
#include "engines/document/DocumentMetadata.h"

#include <private/qzipreader_p.h>

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <algorithm>
#include <memory>

namespace studylab {

namespace {

using book::esc;

const QString kW = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
const QString kR = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
const QString kA = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
const QString kV = QStringLiteral("urn:schemas-microsoft-com:vml");
const QString kWp = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing");
const QString kDc = QStringLiteral("http://purl.org/dc/elements/1.1/");

/// Arbre XML minimal (les parties d'un .docx tiennent en mémoire).
struct Node {
    QString ns;
    QString name;
    QXmlStreamAttributes attrs;
    QString text;  ///< texte direct (w:t, dc:title…)
    std::vector<Node> children;

    bool is(const QString& n, const QString& space) const { return name == n && ns == space; }
    const Node* child(const QString& n, const QString& space) const
    {
        for (const Node& c : children)
            if (c.is(n, space))
                return &c;
        return nullptr;
    }
    QString attr(const QString& space, const QString& n) const { return attrs.value(space, n).toString(); }
    /// Premier descendant de ce nom (parcours en profondeur).
    const Node* find(const QString& n, const QString& space) const
    {
        for (const Node& c : children) {
            if (c.is(n, space))
                return &c;
            if (const Node* f = c.find(n, space))
                return f;
        }
        return nullptr;
    }
};

std::unique_ptr<Node> parseXml(const QByteArray& data)
{
    if (data.isEmpty())
        return nullptr;
    QXmlStreamReader x(data);
    auto root = std::make_unique<Node>();
    std::vector<Node*> stack{root.get()};
    while (!x.atEnd()) {
        switch (x.readNext()) {
        case QXmlStreamReader::StartElement: {
            Node n;
            n.ns = x.namespaceUri().toString();
            n.name = x.name().toString();
            n.attrs = x.attributes();
            stack.back()->children.push_back(std::move(n));
            stack.push_back(&stack.back()->children.back());
            break;
        }
        case QXmlStreamReader::EndElement:
            stack.pop_back();
            break;
        case QXmlStreamReader::Characters:
            stack.back()->text += x.text();
            break;
        default:
            break;
        }
        if (stack.empty())
            break;
    }
    if (x.hasError() && root->children.empty())
        return nullptr;
    return root;
}

bool onOff(const Node* n)
{
    if (!n)
        return false;
    const QString v = n->attr(kW, QStringLiteral("val"));
    return v.isEmpty() || (v != QLatin1String("0") && v != QLatin1String("false") && v != QLatin1String("none"));
}

struct Style {
    QString name;
    QString basedOn;
    int outline = -1;  ///< niveau hiérarchique 0..8, -1 si aucun
};

struct Converter {
    explicit Converter(QZipReader& z) : zip(z) {}

    QZipReader& zip;
    QHash<QString, Style> styles;
    QHash<QString, QString> rels;          ///< rId → cible (chemin dans l'archive ou URL)
    QHash<QString, bool> external;         ///< rId → lien externe
    QHash<QString, bool> ordered;          ///< « numId/ilvl » → liste numérotée
    QHash<QString, QString> footnotes;     ///< id → HTML
    QList<QPair<QString, QString>> usedNotes;  ///< (numéro, HTML), dans l'ordre d'appel
    QList<book::Image> images;
    QHash<QString, QString> imageByPart;
    QString titleFromStyle;
    int headingCount = 0;

    struct Block {
        QString html;
        int heading = 0;
        QString text;
        QString id;
    };
    QList<Block> blocks;

    // ----- Styles, numérotation, relations -------------------------------------------------------------------

    void loadStyles(const Node* root)
    {
        if (!root)
            return;
        const Node* doc = root->child(QStringLiteral("styles"), kW);
        if (!doc)
            return;
        for (const Node& s : doc->children) {
            if (!s.is(QStringLiteral("style"), kW))
                continue;
            Style st;
            if (const Node* n = s.child(QStringLiteral("name"), kW))
                st.name = n->attr(kW, QStringLiteral("val"));
            if (const Node* b = s.child(QStringLiteral("basedOn"), kW))
                st.basedOn = b->attr(kW, QStringLiteral("val"));
            if (const Node* p = s.child(QStringLiteral("pPr"), kW))
                if (const Node* o = p->child(QStringLiteral("outlineLvl"), kW))
                    st.outline = o->attr(kW, QStringLiteral("val")).toInt();
            styles.insert(s.attr(kW, QStringLiteral("styleId")), st);
        }
    }

    /// Niveau de titre (1..6) d'un style, 0 sinon ; -1 pour le style « Title ».
    int headingLevel(const QString& styleId) const
    {
        static const QRegularExpression re(QStringLiteral("^(?:heading|titre|überschrift|título|titolo)\\s*(\\d)$"),
                                           QRegularExpression::CaseInsensitiveOption);
        QString id = styleId;
        for (int guard = 0; guard < 8 && !id.isEmpty(); ++guard) {
            const Style st = styles.value(id);
            const QString name = st.name.isEmpty() ? id : st.name;
            if (name.compare(QLatin1String("Title"), Qt::CaseInsensitive) == 0 ||
                name.compare(QLatin1String("Titre"), Qt::CaseInsensitive) == 0)
                return -1;
            if (const auto m = re.match(name.trimmed()); m.hasMatch())
                return std::clamp(m.captured(1).toInt(), 1, 6);
            if (st.outline >= 0 && st.outline < 9)
                return std::min(st.outline + 1, 6);
            id = st.basedOn;
        }
        return 0;
    }

    bool isQuoteStyle(const QString& styleId) const
    {
        const QString name = styles.value(styleId).name.isEmpty() ? styleId : styles.value(styleId).name;
        return name.contains(QLatin1String("quote"), Qt::CaseInsensitive) ||
               name.contains(QLatin1String("citation"), Qt::CaseInsensitive);
    }

    void loadNumbering(const Node* root)
    {
        if (!root)
            return;
        const Node* doc = root->child(QStringLiteral("numbering"), kW);
        if (!doc)
            return;
        QHash<QString, QHash<int, bool>> abstract;
        for (const Node& a : doc->children) {
            if (!a.is(QStringLiteral("abstractNum"), kW))
                continue;
            QHash<int, bool> levels;
            for (const Node& l : a.children) {
                if (!l.is(QStringLiteral("lvl"), kW))
                    continue;
                const Node* fmt = l.child(QStringLiteral("numFmt"), kW);
                const QString v = fmt ? fmt->attr(kW, QStringLiteral("val")) : QStringLiteral("bullet");
                levels.insert(l.attr(kW, QStringLiteral("ilvl")).toInt(), v != QLatin1String("bullet") && v != QLatin1String("none"));
            }
            abstract.insert(a.attr(kW, QStringLiteral("abstractNumId")), levels);
        }
        for (const Node& n : doc->children) {
            if (!n.is(QStringLiteral("num"), kW))
                continue;
            const Node* a = n.child(QStringLiteral("abstractNumId"), kW);
            if (!a)
                continue;
            const auto levels = abstract.value(a->attr(kW, QStringLiteral("val")));
            for (auto it = levels.cbegin(); it != levels.cend(); ++it)
                ordered.insert(n.attr(kW, QStringLiteral("numId")) + u'/' + QString::number(it.key()), it.value());
        }
    }

    void loadRels(const QByteArray& data)
    {
        QXmlStreamReader x(data);
        while (!x.atEnd()) {
            if (x.readNext() != QXmlStreamReader::StartElement || x.name() != QLatin1String("Relationship"))
                continue;
            const QString id = x.attributes().value(QStringLiteral("Id")).toString();
            const QString target = x.attributes().value(QStringLiteral("Target")).toString();
            const bool ext = x.attributes().value(QStringLiteral("TargetMode")) == QLatin1String("External");
            external.insert(id, ext);
            if (ext)
                rels.insert(id, target);
            else if (target.startsWith(u'/'))
                rels.insert(id, target.mid(1));
            else
                rels.insert(id, QStringLiteral("word/") + target);
        }
    }

    void loadFootnotes(const Node* root)
    {
        if (!root)
            return;
        const Node* doc = root->child(QStringLiteral("footnotes"), kW);
        if (!doc)
            return;
        for (const Node& f : doc->children) {
            if (!f.is(QStringLiteral("footnote"), kW) || !f.attr(kW, QStringLiteral("type")).isEmpty())
                continue;  // séparateurs
            QStringList parts;
            for (const Node& p : f.children)
                if (p.is(QStringLiteral("p"), kW)) {
                    const QString html = inlineHtml(p).trimmed();
                    if (!html.isEmpty())
                        parts << html;
                }
            footnotes.insert(f.attr(kW, QStringLiteral("id")), parts.join(QStringLiteral(" ")));
        }
    }

    // ----- Contenu en ligne ----------------------------------------------------------------------------------

    QString image(const QString& rid, const QString& alt)
    {
        const QString part = rels.value(rid);
        const QString ext = QFileInfo(part).suffix().toLower();
        static const QStringList ok = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                       QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("webp"),
                                       QStringLiteral("bmp")};
        if (part.isEmpty() || external.value(rid) || !ok.contains(ext))
            return alt.isEmpty() ? QString() : QStringLiteral("<span class=\"image\">[%1]</span>").arg(esc(alt));
        QString name = imageByPart.value(part);
        if (name.isEmpty()) {
            const QByteArray data = zip.fileData(part);
            if (data.isEmpty())
                return QString();
            name = QStringLiteral("images/i%1.%2").arg(images.size() + 1).arg(ext);
            images.append({name, data});
            imageByPart.insert(part, name);
        }
        return QStringLiteral("<img src=\"%1\" alt=\"%2\"/>").arg(name, esc(alt));
    }

    QString run(const Node& r)
    {
        QString text;
        for (const Node& c : r.children) {
            if (c.is(QStringLiteral("t"), kW) || c.is(QStringLiteral("delText"), kW)) {
                if (c.is(QStringLiteral("t"), kW))
                    text += esc(book::xmlSafe(c.text));
            } else if (c.is(QStringLiteral("tab"), kW)) {
                text += QStringLiteral(" ");
            } else if (c.is(QStringLiteral("br"), kW) || c.is(QStringLiteral("cr"), kW)) {
                if (c.attr(kW, QStringLiteral("type")) != QLatin1String("page"))
                    text += QStringLiteral("<br/>");
            } else if (c.is(QStringLiteral("noBreakHyphen"), kW)) {
                text += QStringLiteral("‑");
            } else if (c.is(QStringLiteral("drawing"), kW)) {
                const Node* blip = c.find(QStringLiteral("blip"), kA);
                const Node* pr = c.find(QStringLiteral("docPr"), kWp);
                const QString alt = pr ? pr->attrs.value(QStringLiteral("descr")).toString() : QString();
                if (blip)
                    text += image(blip->attr(kR, QStringLiteral("embed")), alt);
            } else if (c.is(QStringLiteral("pict"), kW) || c.is(QStringLiteral("object"), kW)) {
                if (const Node* img = c.find(QStringLiteral("imagedata"), kV))
                    text += image(img->attr(kR, QStringLiteral("id")), img->attrs.value(QStringLiteral("title")).toString());
            } else if (c.is(QStringLiteral("footnoteReference"), kW)) {
                const QString id = c.attr(kW, QStringLiteral("id"));
                if (footnotes.contains(id)) {
                    const QString number = QString::number(usedNotes.size() + 1);
                    usedNotes.append({number, footnotes.value(id)});
                    text += QStringLiteral("<sup class=\"noteref\">%1</sup>").arg(number);
                }
            }
        }
        if (text.isEmpty())
            return text;
        if (const Node* pr = r.child(QStringLiteral("rPr"), kW)) {
            if (const Node* va = pr->child(QStringLiteral("vertAlign"), kW)) {
                const QString v = va->attr(kW, QStringLiteral("val"));
                if (v == QLatin1String("superscript"))
                    text = QStringLiteral("<sup>%1</sup>").arg(text);
                else if (v == QLatin1String("subscript"))
                    text = QStringLiteral("<sub>%1</sub>").arg(text);
            }
            if (onOff(pr->child(QStringLiteral("strike"), kW)) || onOff(pr->child(QStringLiteral("dstrike"), kW)))
                text = QStringLiteral("<s>%1</s>").arg(text);
            if (onOff(pr->child(QStringLiteral("u"), kW)))
                text = QStringLiteral("<u>%1</u>").arg(text);
            if (onOff(pr->child(QStringLiteral("i"), kW)))
                text = QStringLiteral("<em>%1</em>").arg(text);
            if (onOff(pr->child(QStringLiteral("b"), kW)))
                text = QStringLiteral("<strong>%1</strong>").arg(text);
        }
        return text;
    }

    /// Contenu d'un paragraphe (ou d'un conteneur en ligne : lien, insertion, champ…).
    QString inlineHtml(const Node& p)
    {
        QString out;
        for (const Node& c : p.children) {
            if (c.ns != kW)
                continue;
            if (c.name == QLatin1String("r")) {
                out += run(c);
            } else if (c.name == QLatin1String("hyperlink")) {
                const QString inner = inlineHtml(c);
                const QString rid = c.attr(kR, QStringLiteral("id"));
                if (!rid.isEmpty() && external.value(rid))
                    out += QStringLiteral("<a href=\"%1\">%2</a>").arg(esc(rels.value(rid)), inner);
                else
                    out += QStringLiteral("<span class=\"link\">%1</span>").arg(inner);
            } else if (c.name == QLatin1String("ins") || c.name == QLatin1String("smartTag") ||
                       c.name == QLatin1String("fldSimple") || c.name == QLatin1String("customXml")) {
                out += inlineHtml(c);
            } else if (c.name == QLatin1String("sdt")) {
                if (const Node* content = c.child(QStringLiteral("sdtContent"), kW))
                    out += inlineHtml(*content);
            }
        }
        return out;
    }

    // ----- Blocs ---------------------------------------------------------------------------------------------

    struct ListLevel {
        int level;
        bool ordered;
    };
    std::vector<ListLevel> openLists;
    QString pending;  ///< HTML des listes en cours

    void closeLists(int downTo = -1)
    {
        while (!openLists.empty() && openLists.back().level > downTo) {
            pending += openLists.back().ordered ? QStringLiteral("</li></ol>") : QStringLiteral("</li></ul>");
            openLists.pop_back();
        }
        if (openLists.empty() && !pending.isEmpty()) {
            blocks.append({pending, 0, {}, {}});
            pending.clear();
        }
    }

    void paragraph(const Node& p)
    {
        QString styleId;
        int ilvl = -1;
        QString numId;
        if (const Node* pr = p.child(QStringLiteral("pPr"), kW)) {
            if (const Node* st = pr->child(QStringLiteral("pStyle"), kW))
                styleId = st->attr(kW, QStringLiteral("val"));
            if (const Node* num = pr->child(QStringLiteral("numPr"), kW)) {
                const Node* lv = num->child(QStringLiteral("ilvl"), kW);
                const Node* id = num->child(QStringLiteral("numId"), kW);
                numId = id ? id->attr(kW, QStringLiteral("val")) : QString();
                if (numId != QLatin1String("0") && !numId.isEmpty())
                    ilvl = lv ? std::clamp(lv->attr(kW, QStringLiteral("val")).toInt(), 0, 8) : 0;
            }
        }
        const QString html = inlineHtml(p).trimmed();
        const int heading = headingLevel(styleId);
        if (heading != 0)
            ilvl = -1;  // titre numéroté : un titre, pas un élément de liste

        if (ilvl >= 0) {
            if (html.isEmpty())
                return;
            const bool ord = ordered.value(numId + u'/' + QString::number(ilvl), false);
            if (!openLists.empty() && openLists.back().level >= ilvl) {
                closeLists(ilvl);
                if (!openLists.empty() && openLists.back().level == ilvl && openLists.back().ordered == ord) {
                    pending += QStringLiteral("</li><li>") + html;
                    return;
                }
                closeLists(ilvl - 1);
            }
            pending += (ord ? QStringLiteral("<ol><li>") : QStringLiteral("<ul><li>")) + html;
            openLists.push_back({ilvl, ord});
            return;
        }
        closeLists();
        if (html.isEmpty())
            return;
        if (heading == -1) {
            if (titleFromStyle.isEmpty())
                titleFromStyle = QTextFragmentText(html);
            blocks.append({QStringLiteral("<h1 class=\"doc-title\">%1</h1>").arg(html), 0, {}, {}});
        } else if (heading > 0) {
            const QString id = QStringLiteral("t%1").arg(++headingCount);
            blocks.append({QStringLiteral("<h%1 id=\"%2\">%3</h%1>").arg(heading).arg(id, html), heading, QTextFragmentText(html), id});
        } else if (isQuoteStyle(styleId)) {
            blocks.append({QStringLiteral("<blockquote><p>%1</p></blockquote>").arg(html), 0, {}, {}});
        } else {
            blocks.append({QStringLiteral("<p>%1</p>").arg(html), 0, {}, {}});
        }
    }

    QString cellHtml(const Node& tc)
    {
        QStringList parts;
        for (const Node& c : tc.children) {
            if (c.is(QStringLiteral("p"), kW)) {
                const QString h = inlineHtml(c).trimmed();
                if (!h.isEmpty())
                    parts << h;
            } else if (c.is(QStringLiteral("tbl"), kW)) {
                parts << tableHtml(c);
            }
        }
        return parts.join(QStringLiteral("<br/>"));
    }

    QString tableHtml(const Node& tbl)
    {
        QString html = QStringLiteral("<table><tbody>");
        for (const Node& tr : tbl.children) {
            if (!tr.is(QStringLiteral("tr"), kW))
                continue;
            html += QStringLiteral("<tr>");
            for (const Node& tc : tr.children) {
                if (!tc.is(QStringLiteral("tc"), kW))
                    continue;
                int span = 1;
                bool continued = false;
                if (const Node* pr = tc.child(QStringLiteral("tcPr"), kW)) {
                    if (const Node* gs = pr->child(QStringLiteral("gridSpan"), kW))
                        span = std::max(1, gs->attr(kW, QStringLiteral("val")).toInt());
                    if (const Node* vm = pr->child(QStringLiteral("vMerge"), kW))
                        continued = vm->attr(kW, QStringLiteral("val")) != QLatin1String("restart");
                }
                if (continued) {
                    html += QStringLiteral("<td></td>");
                    continue;
                }
                html += span > 1 ? QStringLiteral("<td colspan=\"%1\">").arg(span) : QStringLiteral("<td>");
                html += cellHtml(tc) + QStringLiteral("</td>");
            }
            html += QStringLiteral("</tr>");
        }
        return html + QStringLiteral("</tbody></table>");
    }

    void body(const Node& container)
    {
        for (const Node& c : container.children) {
            if (c.is(QStringLiteral("p"), kW)) {
                paragraph(c);
            } else if (c.is(QStringLiteral("tbl"), kW)) {
                closeLists();
                blocks.append({tableHtml(c), 0, {}, {}});
            } else if (c.is(QStringLiteral("sdt"), kW)) {
                if (const Node* content = c.child(QStringLiteral("sdtContent"), kW))
                    body(*content);
            } else if (c.is(QStringLiteral("customXml"), kW)) {
                body(c);
            }
        }
        closeLists();
    }

    static QString QTextFragmentText(const QString& html)
    {
        static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
        QString t = QString(html).remove(tags);
        t.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        t.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        t.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        t.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        return t.simplified();
    }
};

}  // namespace

bool DocxBook::isDocxFile(const QString& path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("docx"), Qt::CaseInsensitive) == 0;
}

Result<QByteArray> DocxBook::build(const QString& path)
{
    QZipReader zip(path);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Document Word illisible ou endommagé."));
    const QByteArray document = zip.fileData(QStringLiteral("word/document.xml"));
    if (document.isEmpty())
        return Error::make(Error::Code::InvalidArgument,
                           QStringLiteral("Ce fichier n'est pas un document Word (.docx) : word/document.xml manque."));
    if (!zip.fileData(QStringLiteral("EncryptionInfo")).isEmpty())
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Ce document Word est protégé par un mot de passe."));

    Converter conv(zip);
    conv.loadRels(zip.fileData(QStringLiteral("word/_rels/document.xml.rels")));
    conv.loadStyles(parseXml(zip.fileData(QStringLiteral("word/styles.xml"))).get());
    conv.loadNumbering(parseXml(zip.fileData(QStringLiteral("word/numbering.xml"))).get());
    conv.loadFootnotes(parseXml(zip.fileData(QStringLiteral("word/footnotes.xml"))).get());
    const auto root = parseXml(document);
    const Node* docNode = root ? root->child(QStringLiteral("document"), kW) : nullptr;
    const Node* bodyNode = docNode ? docNode->child(QStringLiteral("body"), kW) : nullptr;
    if (!bodyNode)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Contenu du document Word illisible."));
    conv.body(*bodyNode);
    if (!conv.usedNotes.isEmpty()) {
        QString notes = QStringLiteral("<aside class=\"footnotes\"><hr/><ol>");
        for (const auto& [number, html] : std::as_const(conv.usedNotes))
            notes += QStringLiteral("<li value=\"%1\">%2</li>").arg(number, html);
        conv.blocks.append({notes + QStringLiteral("</ol></aside>"), 0, {}, {}});
    }

    // Propriétés du document
    QString title;
    QStringList authors;
    QString lang;
    if (const auto core = parseXml(zip.fileData(QStringLiteral("docProps/core.xml")))) {
        if (const Node* t = core->find(QStringLiteral("title"), kDc))
            title = t->text.simplified();
        if (const Node* c = core->find(QStringLiteral("creator"), kDc))
            for (const QString& a : c->text.split(QRegularExpression(QStringLiteral("\\s*;\\s*")), Qt::SkipEmptyParts))
                authors << a.trimmed();
        if (const Node* l = core->find(QStringLiteral("language"), kDc))
            lang = l->text.trimmed();
    }
    if (title.isEmpty())
        title = conv.titleFromStyle;
    if (title.isEmpty())
        title = titleFromFileName(path);

    // Découpage : aux titres du plus haut niveau qui se répète (au plus le niveau 3).
    QHash<int, int> counts;
    int minLevel = 7;
    for (const auto& b : std::as_const(conv.blocks))
        if (b.heading > 0) {
            counts[b.heading]++;
            minLevel = std::min(minLevel, b.heading);
        }
    int split = 0;
    for (int level = 1; level <= 3; ++level)
        if (counts.value(level) >= 2) {
            split = level;
            break;
        }
    book::Spec spec;
    spec.title = title;
    spec.authors = authors;
    spec.lang = lang;
    spec.identifierSeed = document;
    spec.images = conv.images;
    book::Section current;
    for (const auto& b : std::as_const(conv.blocks)) {
        if (split > 0 && b.heading > 0 && b.heading <= split && !current.html.isEmpty()) {
            spec.sections << current;
            current = {};
        }
        if (b.heading > 0 && current.title.isEmpty())
            current.title = b.text;
        if (b.heading > 0 && (split == 0 ? b.heading <= minLevel + 1 : b.heading <= split + 1))
            spec.toc.append({b.text, QStringLiteral("s%1.xhtml#%2").arg(spec.sections.size() + 1).arg(b.id),
                             b.heading - std::min(minLevel, 6)});
        current.html += b.html + u'\n';
    }
    if (!current.html.isEmpty() || spec.sections.isEmpty())
        spec.sections << current;
    return book::write(spec);
}

}  // namespace studylab
