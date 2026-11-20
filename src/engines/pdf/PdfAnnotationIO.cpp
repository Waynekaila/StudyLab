#include "engines/pdf/PdfAnnotationIO.h"

#include "engines/pdf/PdfObjects.h"

#include <QHash>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <map>

namespace studylab::pdf {

namespace {

/// Zone visible de la page en points PDF (origine en bas à gauche) : CropBox, sinon MediaBox, sinon A4.
QRectF pageBox(const PdfFile& f, const PdfObject& page)
{
    for (const char* key : {"CropBox", "MediaBox"}) {
        const PdfObject box = f.resolve(page.value(key));
        if (box.array.size() != 4)
            continue;
        const double x0 = f.resolve(box.array[0]).number();
        const double y0 = f.resolve(box.array[1]).number();
        const double x1 = f.resolve(box.array[2]).number();
        const double y1 = f.resolve(box.array[3]).number();
        const QRectF r(QPointF(std::min(x0, x1), std::min(y0, y1)), QPointF(std::max(x0, x1), std::max(y0, y1)));
        if (r.width() > 1 && r.height() > 1)
            return r;
    }
    return {0, 0, 595, 842};
}

int pageRotation(const PdfFile& f, const PdfObject& page)
{
    const int r = int(f.resolve(page.value("Rotate")).toInt()) % 360;
    return r < 0 ? r + 360 : r;
}

/// Rectangle en points PDF (origine en bas) → rectangle normalisé (origine en haut), borné à la page.
QRectF normalize(const QRectF& pdf, const QRectF& box)
{
    const double left = (pdf.left() - box.left()) / box.width();
    const double right = (pdf.right() - box.left()) / box.width();
    const double top = (box.bottom() - pdf.bottom()) / box.height();  // QRectF : bottom() = y + h = haut en PDF
    const double bottom = (box.bottom() - pdf.top()) / box.height();
    const auto clamp = [](double v) { return std::clamp(v, 0.0, 1.0); };
    return QRectF(QPointF(clamp(left), clamp(top)), QPointF(clamp(right), clamp(bottom))).normalized();
}

/// Rectangle normalisé → rectangle en points PDF (origine en bas).
QRectF denormalize(const QRectF& n, const QRectF& box)
{
    const double left = box.left() + n.left() * box.width();
    const double right = box.left() + n.right() * box.width();
    const double top = box.bottom() - n.top() * box.height();
    const double bottom = box.bottom() - n.bottom() * box.height();
    return QRectF(QPointF(left, bottom), QPointF(right, top)).normalized();
}

QRectF numbersRect(const PdfFile& f, const PdfObject& arr)
{
    if (arr.array.size() != 4)
        return {};
    const double x0 = f.resolve(arr.array[0]).number();
    const double y0 = f.resolve(arr.array[1]).number();
    const double x1 = f.resolve(arr.array[2]).number();
    const double y1 = f.resolve(arr.array[3]).number();
    return QRectF(QPointF(std::min(x0, x1), std::min(y0, y1)), QPointF(std::max(x0, x1), std::max(y0, y1)));
}

QColor colorOf(const PdfFile& f, const PdfObject& c)
{
    const PdfObject arr = f.resolve(c);
    std::vector<double> v;
    for (const PdfObject& item : arr.array)
        v.push_back(std::clamp(f.resolve(item).number(), 0.0, 1.0));
    if (v.size() == 1)
        return QColor::fromRgbF(float(v[0]), float(v[0]), float(v[0]));
    if (v.size() == 3)
        return QColor::fromRgbF(float(v[0]), float(v[1]), float(v[2]));
    if (v.size() == 4)
        return QColor::fromCmykF(float(v[0]), float(v[1]), float(v[2]), float(v[3])).toRgb();
    return {};
}

std::optional<AnnotationType> typeForSubtype(const QByteArray& subtype)
{
    if (subtype == "Highlight")
        return AnnotationType::Highlight;
    if (subtype == "Underline" || subtype == "Squiggly")
        return AnnotationType::Underline;
    if (subtype == "StrikeOut")
        return AnnotationType::Strikeout;
    if (subtype == "Text" || subtype == "FreeText")
        return AnnotationType::Note;
    if (subtype == "Square" || subtype == "Circle" || subtype == "Ink" || subtype == "Polygon" ||
        subtype == "PolyLine" || subtype == "Line" || subtype == "Stamp" || subtype == "Caret")
        return AnnotationType::Marker;
    return std::nullopt;
}

bool isTextMarkup(AnnotationType t)
{
    return t == AnnotationType::Highlight || t == AnnotationType::Underline || t == AnnotationType::Strikeout;
}

QRectF notePoint(const QPointF& p) { return {p.x() - 0.005, p.y() - 0.005, 0.01, 0.01}; }

QByteArray num(double v)
{
    if (std::abs(v - std::round(v)) < 1e-6)
        return QByteArray::number(qint64(std::llround(v)));
    QByteArray s = QByteArray::number(v, 'f', 3);
    while (s.endsWith('0'))
        s.chop(1);
    if (s.endsWith('.'))
        s.chop(1);
    return s;
}

QByteArray rgb(const QColor& c) { return num(c.redF()) + ' ' + num(c.greenF()) + ' ' + num(c.blueF()); }

PdfObject colorArray(const QColor& c)
{
    return PdfObject::makeArray({PdfObject::fromReal(c.redF()), PdfObject::fromReal(c.greenF()), PdfObject::fromReal(c.blueF())});
}

PdfObject rectArray(const QRectF& r)
{
    return PdfObject::makeArray({PdfObject::fromReal(r.left()), PdfObject::fromReal(r.top()), PdfObject::fromReal(r.right()),
                                 PdfObject::fromReal(r.bottom())});
}

QByteArray pdfDate(const QDateTime& when)
{
    const QDateTime utc = (when.isValid() ? when : QDateTime::currentDateTimeUtc()).toUTC();
    return "D:" + utc.toString(QStringLiteral("yyyyMMddHHmmss")).toLatin1() + 'Z';
}

PdfObject extGState(double fillAlpha, double strokeAlpha, bool multiply)
{
    PdfObject gs = PdfObject::makeDict();
    gs.set("Type", PdfObject::fromName("ExtGState"));
    gs.set("ca", PdfObject::fromReal(fillAlpha));
    gs.set("CA", PdfObject::fromReal(strokeAlpha));
    if (multiply)
        gs.set("BM", PdfObject::fromName("Multiply"));
    return gs;
}

/// Flux d'apparence (/AP /N) : formulaire dessiné dans le repère de la page, borné à `bbox`.
PdfObject appearance(const QRectF& bbox, const QByteArray& content, const PdfObject& gs)
{
    PdfObject form;
    form.type = PdfObject::Type::Stream;
    form.set("Type", PdfObject::fromName("XObject"));
    form.set("Subtype", PdfObject::fromName("Form"));
    form.set("FormType", PdfObject::fromInt(1));
    form.set("BBox", rectArray(bbox));
    PdfObject states = PdfObject::makeDict();
    states.set("G0", gs);
    PdfObject resources = PdfObject::makeDict();
    resources.set("ExtGState", states);
    form.set("Resources", resources);
    form.stream = content;
    return form;
}

}  // namespace

// ----- Lecture ---------------------------------------------------------------------------------------------------

Result<PdfAnnotationScan> readPdfAnnotations(const QByteArray& data)
{
    auto loaded = PdfFile::load(data);
    if (!loaded)
        return loaded.error();
    const PdfFile& f = loaded.value();
    if (f.encrypted())
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Ce PDF est chiffré : ses annotations ne peuvent pas être lues."));

    PdfAnnotationScan scan;
    QHash<int, qsizetype> byRef;               // numéro d'objet → index dans scan.annotations
    std::vector<std::pair<int, QString>> replies;  // (parent, texte)
    const auto& pages = f.pages();
    for (int pageIndex = 0; pageIndex < int(pages.size()); ++pageIndex) {
        const PdfObject& page = pages[size_t(pageIndex)].dict;
        const PdfObject annots = f.resolve(page.value("Annots"));
        if (annots.array.empty())
            continue;
        const QRectF box = pageBox(f, page);
        const int rotation = pageRotation(f, page);
        for (const PdfObject& item : annots.array) {
            const PdfObject a = f.resolve(item);
            if (!a.isDict())
                continue;
            const QByteArray subtype = f.resolve(a.value("Subtype")).bytes;
            const auto type = typeForSubtype(subtype);
            if (subtype == "Widget")
                scan.hasForms = true;
            if (subtype == "Link" || subtype == "Popup" || subtype == "Widget")
                continue;
            if (const qint64 flags = f.resolve(a.value("F")).toInt(); flags & 2)  // Hidden
                continue;
            if (const PdfObject irt = a.value("IRT"); irt.type == PdfObject::Type::Ref) {
                const QString reply = f.resolve(a.value("Contents")).text().trimmed();
                if (!reply.isEmpty())
                    replies.emplace_back(irt.ref.num, reply);
                continue;
            }
            if (!type || rotation != 0) {
                ++scan.unsupported;
                continue;
            }
            PdfAnnotation out;
            out.page = pageIndex;
            out.type = *type;
            out.subtype = subtype;
            out.color = colorOf(f, a.value("C"));
            out.contents = f.resolve(a.value("Contents")).text().trimmed();
            out.author = f.resolve(a.value("T")).text().trimmed();
            out.name = f.resolve(a.value("NM")).text();
            const QRectF rect = numbersRect(f, f.resolve(a.value("Rect")));
            if (isTextMarkup(*type)) {
                const PdfObject quads = f.resolve(a.value("QuadPoints"));
                for (size_t i = 0; i + 7 < quads.array.size(); i += 8) {
                    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
                    for (size_t k = 0; k < 8; k += 2) {
                        const double x = f.resolve(quads.array[i + k]).number();
                        const double y = f.resolve(quads.array[i + k + 1]).number();
                        minX = std::min(minX, x);
                        maxX = std::max(maxX, x);
                        minY = std::min(minY, y);
                        maxY = std::max(maxY, y);
                    }
                    const QRectF n = normalize(QRectF(QPointF(minX, minY), QPointF(maxX, maxY)), box);
                    if (n.width() > 0 && n.height() > 0)
                        out.rects << n;
                }
                if (out.rects.isEmpty() && rect.isValid())
                    out.rects << normalize(rect, box);
            } else if (*type == AnnotationType::Note) {
                if (rect.isValid()) {
                    const QRectF n = normalize(rect, box);
                    out.rects << notePoint(n.topLeft());
                }
            } else if (rect.isValid()) {
                // Rectangle et ellipse : /Rect inclut la moitié du trait.
                QRectF shape = rect;
                if (subtype == "Square" || subtype == "Circle") {
                    const PdfObject bs = f.resolve(a.value("BS"));
                    const double w = bs.isDict() && bs.find("W") ? f.resolve(bs.value("W")).number(1) : 1.0;
                    if (shape.width() > w && shape.height() > w)
                        shape.adjust(w / 2, w / 2, -w / 2, -w / 2);
                }
                out.rects << normalize(shape, box);
            }
            if (out.rects.isEmpty() || out.rects.first().width() <= 0 || out.rects.first().height() <= 0) {
                ++scan.unsupported;
                continue;
            }
            // Note vide (icône posée sans texte) : rien à importer.
            if (*type == AnnotationType::Note && out.contents.isEmpty()) {
                ++scan.unsupported;
                continue;
            }
            if (*type == AnnotationType::Marker ? subtype != "Square" : subtype == "FreeText")
                ++scan.approximated;
            if (item.type == PdfObject::Type::Ref)
                byRef.insert(item.ref.num, scan.annotations.size());
            scan.annotations << out;
        }
    }
    for (const auto& [parent, text] : replies) {
        const auto it = byRef.constFind(parent);
        if (it == byRef.cend())
            continue;
        QString& contents = scan.annotations[*it].contents;
        contents = contents.isEmpty() ? text : contents + QStringLiteral("\n\n") + text;
    }
    return scan;
}

void extractQuotes(QPdfDocument& document, QList<PdfAnnotation>& annotations)
{
    QHash<int, QList<QRectF>> lines;  // page → zones de texte (une par ligne ou segment de ligne), en points
    for (PdfAnnotation& a : annotations) {
        if (a.page >= document.pageCount())
            continue;
        const QSizeF size = document.pagePointSize(a.page);
        if (a.type == AnnotationType::Marker && a.rects.size() == 1) {
            const QRectF r = a.rects.first();
            const QPdfSelection sel = document.getSelection(a.page, QPointF(r.left() * size.width(), r.top() * size.height()),
                                                            QPointF(r.right() * size.width(), r.bottom() * size.height()));
            if (sel.isValid())
                a.text = sel.text().trimmed();
            continue;
        }
        if (!isTextMarkup(a.type))
            continue;
        if (!lines.contains(a.page)) {
            QList<QRectF> rects;
            for (const QPolygonF& poly : document.getAllText(a.page).bounds())
                rects << poly.boundingRect();
            lines.insert(a.page, rects);
        }
        QStringList parts;
        for (const QRectF& n : a.rects) {
            // Qt PDF ne sélectionne qu'entre deux points posés sur des caractères : les extrémités sont ramenées
            // à l'intersection de la zone annotée et du texte de la ligne.
            const QRectF r(n.left() * size.width(), n.top() * size.height(), n.width() * size.width(),
                           n.height() * size.height());
            const double y = r.center().y();
            double x0 = r.right();
            double x1 = r.left();
            for (const QRectF& t : lines.value(a.page)) {
                if (y < t.top() || y > t.bottom() || t.right() < r.left() || t.left() > r.right())
                    continue;
                x0 = std::min(x0, std::max(r.left(), t.left()));
                x1 = std::max(x1, std::min(r.right(), t.right()));
            }
            if (x1 - x0 < 1)
                continue;
            const QPdfSelection sel = document.getSelection(a.page, QPointF(x0 + 0.5, y), QPointF(x1 - 0.5, y));
            if (sel.isValid() && !sel.text().trimmed().isEmpty())
                parts << sel.text().trimmed();
        }
        a.text = parts.join(QLatin1Char(' '));
    }
}

bool sameAnnotationGeometry(int pageA, AnnotationType typeA, const QList<QRectF>& rectsA, int pageB,
                            AnnotationType typeB, const QList<QRectF>& rectsB)
{
    if (pageA != pageB || typeA != typeB || rectsA.isEmpty() || rectsA.size() != rectsB.size())
        return false;
    constexpr double tol = 0.003;
    const auto close = [](const QRectF& a, const QRectF& b) {
        return std::abs(a.left() - b.left()) <= tol && std::abs(a.top() - b.top()) <= tol &&
               std::abs(a.right() - b.right()) <= tol && std::abs(a.bottom() - b.bottom()) <= tol;
    };
    if (typeA == AnnotationType::Note)  // un point : seul le centre compte
        return std::abs(rectsA.first().center().x() - rectsB.first().center().x()) <= tol &&
               std::abs(rectsA.first().center().y() - rectsB.first().center().y()) <= tol;
    for (const QRectF& a : rectsA)
        if (std::none_of(rectsB.cbegin(), rectsB.cend(), [&](const QRectF& b) { return close(a, b); }))
            return false;
    return true;
}

// ----- Écriture --------------------------------------------------------------------------------------------------

Result<PdfWriteReport> writeAnnotatedCopy(const QByteArray& original, const QList<PdfAnnotationOut>& annotations)
{
    auto loaded = PdfFile::load(original);
    if (!loaded)
        return loaded.error();
    const PdfFile& f = loaded.value();
    if (f.encrypted())
        return Error::make(Error::Code::InvalidArgument,
                           QStringLiteral("Ce PDF est chiffré : StudyLab ne peut pas y écrire d'annotations."));
    if (f.lastXref() <= 0)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral(
            "La structure de ce PDF est abîmée : StudyLab n'y ajoute pas d'annotations pour ne pas risquer de le rendre illisible."));
    const PdfObject root = f.trailer().value("Root");
    if (root.type != PdfObject::Type::Ref)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Structure du PDF non prise en charge."));

    PdfWriteReport report;
    report.data = original;
    if (!report.data.endsWith('\n') && !report.data.endsWith('\r'))
        report.data += '\n';
    std::map<int, qint64> offsets;  // objets écrits : numéro → position
    int next = f.size();
    const auto write = [&](int number, int gen, const PdfObject& o) {
        offsets[number] = report.data.size();
        report.data += QByteArray::number(number) + ' ' + QByteArray::number(gen) + " obj\n" + serialize(o) + "\nendobj\n";
    };

    std::map<int, std::vector<PdfObject>> added;  // page → références des nouvelles annotations
    const auto& pages = f.pages();
    for (int i = 0; i < annotations.size(); ++i) {
        const PdfAnnotationOut& a = annotations.at(i);
        if (a.page < 0 || a.page >= int(pages.size()) || a.rects.isEmpty() || a.type == AnnotationType::Bookmark ||
            pageRotation(f, pages[size_t(a.page)].dict) != 0 || pages[size_t(a.page)].ref.num <= 0) {
            ++report.skipped;
            report.skippedIndexes << i;
            continue;
        }
        const auto& page = pages[size_t(a.page)];
        const QRectF box = pageBox(f, page.dict);
        PdfObject annot = PdfObject::makeDict();
        annot.set("Type", PdfObject::fromName("Annot"));
        QByteArray content;
        QRectF bbox;
        PdfObject gs;
        switch (a.type) {
        case AnnotationType::Highlight:
        case AnnotationType::Underline:
        case AnnotationType::Strikeout: {
            annot.set("Subtype", PdfObject::fromName(a.type == AnnotationType::Highlight   ? "Highlight"
                                                     : a.type == AnnotationType::Underline ? "Underline"
                                                                                           : "StrikeOut"));
            PdfObject quads = PdfObject::makeArray();
            for (const QRectF& n : a.rects) {
                const QRectF r = denormalize(n, box);
                // Ordre usuel des lecteurs : haut gauche, haut droite, bas gauche, bas droite.
                for (const QPointF& p : {QPointF(r.left(), r.bottom()), QPointF(r.right(), r.bottom()),
                                         QPointF(r.left(), r.top()), QPointF(r.right(), r.top())}) {
                    quads.array.push_back(PdfObject::fromReal(p.x()));
                    quads.array.push_back(PdfObject::fromReal(p.y()));
                }
                bbox |= r;
                if (a.type == AnnotationType::Highlight) {
                    content += num(r.left()) + ' ' + num(r.top()) + ' ' + num(r.width()) + ' ' + num(r.height()) + " re\n";
                } else {
                    const double width = std::max(0.6, r.height() * 0.07);
                    const double y = a.type == AnnotationType::Underline ? r.top() + width : r.center().y();
                    content += num(width) + " w " + num(r.left()) + ' ' + num(y) + " m " + num(r.right()) + ' ' + num(y) + " l S\n";
                }
            }
            annot.set("QuadPoints", quads);
            const QColor c = a.type == AnnotationType::Highlight ? a.fill : a.stroke;
            annot.set("C", colorArray(c));
            content = a.type == AnnotationType::Highlight ? "/G0 gs " + rgb(c) + " rg\n" + content + "f\n"
                                                          : "/G0 gs " + rgb(c) + " RG\n" + content;
            gs = extGState(1, 1, a.type == AnnotationType::Highlight);
            break;
        }
        case AnnotationType::Marker: {
            annot.set("Subtype", PdfObject::fromName("Square"));
            const QRectF r = denormalize(a.rects.first(), box);
            bbox = r.adjusted(-0.75, -0.75, 0.75, 0.75);  // le trait (1,5) déborde de moitié
            annot.set("C", colorArray(a.stroke));
            PdfObject border = PdfObject::makeDict();
            border.set("W", PdfObject::fromReal(1.5));
            annot.set("BS", border);
            const QByteArray re = num(r.left()) + ' ' + num(r.top()) + ' ' + num(r.width()) + ' ' + num(r.height()) + " re";
            content = "/G0 gs " + rgb(a.fill) + " rg " + re + " f\n";
            content += "Q q 1.5 w " + rgb(a.stroke) + " RG " + re + " S\n";
            content = "q " + content + "Q\n";
            gs = extGState(0.35, 1, true);
            break;
        }
        case AnnotationType::Note: {
            annot.set("Subtype", PdfObject::fromName("Text"));
            annot.set("Name", PdfObject::fromName("Comment"));
            annot.set("Open", PdfObject::fromBool(false));
            const double size = 18;
            const QPointF topLeft(box.left() + a.rects.first().center().x() * box.width(),
                                  box.bottom() - a.rects.first().center().y() * box.height());
            const QRectF r(topLeft.x(), topLeft.y() - size, size, size);
            bbox = r;
            annot.set("C", colorArray(a.stroke));
            const auto at = [&r](double x, double y) { return num(r.left() + x) + ' ' + num(r.top() + y); };
            content = "/G0 gs " + rgb(a.stroke) + " rg " + at(1, 3) + ' ' + num(16) + ' ' + num(14) + " re f\n";
            content += at(4, 1) + " m " + at(4, 3) + " l " + at(7, 3) + " l f\n";  // pointe de la bulle
            content += "1 1 1 RG 1.2 w";
            for (const double y : {13.0, 10.0, 7.0})
                content += ' ' + at(4, y) + " m " + at(14, y) + " l S";
            content += '\n';
            gs = extGState(1, 1, false);
            break;
        }
        case AnnotationType::Bookmark:
            break;
        }
        const QRectF rect = isTextMarkup(a.type) ? bbox.adjusted(-1, -1, 1, 1) : bbox;
        annot.set("Rect", rectArray(rect));
        if (!a.contents.isEmpty())
            annot.set("Contents", PdfObject::fromText(a.contents));
        annot.set("T", PdfObject::fromText(QStringLiteral("StudyLab")));
        annot.set("NM", PdfObject::fromText(a.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : a.id));
        annot.set("M", PdfObject::fromString(pdfDate(a.modified)));
        annot.set("F", PdfObject::fromInt(4));  // Print
        if (a.type == AnnotationType::Highlight)
            annot.set("CA", PdfObject::fromReal(1));
        annot.set("P", PdfObject::fromRef(page.ref));

        const int apNum = next++;
        const int annotNum = next++;
        write(apNum, 0, appearance(rect, content, gs));
        PdfObject ap = PdfObject::makeDict();
        ap.set("N", PdfObject::fromRef({apNum, 0}));
        annot.set("AP", ap);
        write(annotNum, 0, annot);
        added[a.page].push_back(PdfObject::fromRef({annotNum, 0}));
        ++report.written;
    }

    if (report.written == 0) {
        report.data = original;
        return report;
    }

    // Pages révisées : tableau /Annots direct (annotations existantes conservées, puis les nouvelles).
    for (const auto& [pageIndex, refs] : added) {
        const auto& page = pages[size_t(pageIndex)];
        PdfObject dict = f.object(page.ref.num);
        if (!dict.isDict())
            continue;
        PdfObject list = f.resolve(dict.value("Annots"));
        if (list.type != PdfObject::Type::Array)
            list = PdfObject::makeArray();
        for (const PdfObject& r : refs)
            list.array.push_back(r);
        dict.set("Annots", list);
        write(page.ref.num, page.ref.gen, dict);
    }

    // Table des références de la révision ajoutée.
    PdfObject trailer = PdfObject::makeDict();
    for (const char* key : {"Root", "Info", "ID"})
        if (const PdfObject* v = f.trailer().find(key))
            trailer.set(key, *v);
    trailer.set("Prev", PdfObject::fromInt(f.lastXref()));
    std::map<int, int> gens;
    for (const auto& [pageIndex, refs] : added)
        gens[pages[size_t(pageIndex)].ref.num] = pages[size_t(pageIndex)].ref.gen;

    // Sous-sections de numéros consécutifs.
    const auto sections = [&offsets]() {
        std::vector<std::pair<int, int>> out;  // (premier, nombre)
        for (const auto& [number, offset] : offsets) {
            Q_UNUSED(offset);
            if (!out.empty() && out.back().first + out.back().second == number)
                ++out.back().second;
            else
                out.emplace_back(number, 1);
        }
        return out;
    };

    if (f.usesXrefStream()) {
        const int xrefNum = next++;
        offsets[xrefNum] = report.data.size();  // position de l'objet lui-même
        trailer.set("Type", PdfObject::fromName("XRef"));
        trailer.set("Size", PdfObject::fromInt(next));
        PdfObject index = PdfObject::makeArray();
        QByteArray rows;
        for (const auto& [first, count] : sections()) {
            index.array.push_back(PdfObject::fromInt(first));
            index.array.push_back(PdfObject::fromInt(count));
            for (int n = first; n < first + count; ++n) {
                const qint64 off = offsets[n];
                const int gen = gens.count(n) ? gens[n] : 0;
                rows += char(1);
                for (int shift = 24; shift >= 0; shift -= 8)
                    rows += char((off >> shift) & 0xFF);
                rows += char((gen >> 8) & 0xFF);
                rows += char(gen & 0xFF);
            }
        }
        trailer.set("Index", index);
        trailer.set("W", PdfObject::makeArray({PdfObject::fromInt(1), PdfObject::fromInt(4), PdfObject::fromInt(2)}));
        trailer.type = PdfObject::Type::Stream;
        trailer.stream = rows;
        const qint64 start = report.data.size();
        report.data += QByteArray::number(xrefNum) + " 0 obj\n" + serialize(trailer) + "\nendobj\n";
        report.data += "startxref\n" + QByteArray::number(start) + "\n%%EOF\n";
    } else {
        const qint64 start = report.data.size();
        report.data += "xref\n";
        for (const auto& [first, count] : sections()) {
            report.data += QByteArray::number(first) + ' ' + QByteArray::number(count) + '\n';
            for (int n = first; n < first + count; ++n) {
                const int gen = gens.count(n) ? gens[n] : 0;
                report.data += QByteArray::number(offsets[n]).rightJustified(10, '0') + ' ' +
                               QByteArray::number(gen).rightJustified(5, '0') + " n\r\n";
            }
        }
        trailer.set("Size", PdfObject::fromInt(next));
        report.data += "trailer\n" + serialize(trailer) + "\nstartxref\n" + QByteArray::number(start) + "\n%%EOF\n";
    }
    return report;
}

}  // namespace studylab::pdf
