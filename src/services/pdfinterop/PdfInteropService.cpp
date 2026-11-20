#include "services/pdfinterop/PdfInteropService.h"

#include "core/domain/Comment.h"
#include "core/domain/Document.h"
#include "services/annotations/AnnotationService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QPdfDocument>
#include <QRegularExpression>
#include <QSaveFile>

#include <cmath>
#include <map>

namespace studylab {

namespace {

QDateTime parseTimestamp(const QString& s)
{
    QDateTime t = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!t.isValid())
        t = QDateTime::fromString(s, Qt::ISODate);
    return t;
}

}  // namespace

PdfInteropService::PdfInteropService(LibraryService& library, AnnotationService& annotations, CommentService& comments)
    : m_library(library), m_annotations(annotations), m_comments(comments)
{
}

Result<pdf::PdfAnnotationScan> PdfInteropService::readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Error::make(Error::Code::Io, tr("Lecture impossible de %1 : %2").arg(path, file.errorString()));
    return pdf::readPdfAnnotations(file.readAll());
}

Result<QString> PdfInteropService::pdfPath(qint64 documentId)
{
    auto doc = m_library.document(documentId);
    if (!doc)
        return doc.error();
    if (doc.value().format != DocumentFormat::Pdf)
        return Error::make(Error::Code::InvalidArgument, tr("Ce document n'est pas un PDF."));
    const QString path = m_library.documentFile(doc.value());
    if (!QFileInfo::exists(path))
        return Error::make(Error::Code::NotFound, tr("Le fichier est introuvable : %1").arg(path));
    return path;
}

Result<PdfImportPreview> PdfInteropService::preview(qint64 documentId, const pdf::PdfAnnotationScan& scan)
{
    auto existing = m_annotations.list(documentId);
    if (!existing)
        return existing.error();
    std::vector<PdfLocator> locators;
    std::vector<AnnotationType> types;
    for (const AnnotationRecord& r : existing.value()) {
        if (const auto loc = PdfLocator::fromJson(r.locator)) {
            locators.push_back(*loc);
            types.push_back(r.type);
        }
    }
    PdfImportPreview out;
    out.unsupported = scan.unsupported;
    for (const pdf::PdfAnnotation& a : scan.annotations) {
        bool known = false;
        for (size_t i = 0; i < locators.size() && !known; ++i)
            known = pdf::sameAnnotationGeometry(a.page, a.type, a.rects, locators[i].page, types[i], locators[i].rects);
        known ? ++out.duplicates : ++out.fresh;
    }
    return out;
}

Result<PdfImportPreview> PdfInteropService::preview(qint64 documentId)
{
    auto path = pdfPath(documentId);
    if (!path)
        return path.error();
    auto scan = readFile(path.value());
    if (!scan)
        return scan.error();
    return preview(documentId, scan.value());
}

Result<PdfImportReport> PdfInteropService::importAnnotations(qint64 documentId, const PdfColorScheme& colors)
{
    auto path = pdfPath(documentId);
    if (!path)
        return path.error();
    auto scan = readFile(path.value());
    if (!scan)
        return scan.error();
    QList<pdf::PdfAnnotation> items = scan.value().annotations;
    {
        QPdfDocument doc;
        if (doc.load(path.value()) == QPdfDocument::Error::None)
            pdf::extractQuotes(doc, items);
    }
    auto existing = m_annotations.list(documentId);
    if (!existing)
        return existing.error();
    std::vector<std::pair<PdfLocator, AnnotationType>> known;
    for (const AnnotationRecord& r : existing.value())
        if (const auto loc = PdfLocator::fromJson(r.locator))
            known.emplace_back(*loc, r.type);

    PdfImportReport report;
    report.unsupported = scan.value().unsupported;
    for (const pdf::PdfAnnotation& a : items) {
        bool duplicate = false;
        for (const auto& [loc, type] : known)
            duplicate = duplicate || pdf::sameAnnotationGeometry(a.page, a.type, a.rects, loc.page, type, loc.rects);
        if (duplicate) {
            ++report.duplicates;
            continue;
        }
        PdfLocator loc;
        loc.page = a.page;
        loc.rects = a.rects;
        loc.exact = normalizeQuote(a.text);
        AnnotationRecord r;
        r.documentId = documentId;
        r.type = a.type;
        r.color = nearestColor(a.color, colors);
        r.locator = loc.toJson();
        r.selectedText = a.text;
        r.note = a.contents;
        auto created = m_annotations.create(r);
        if (!created)
            return created.error();
        known.emplace_back(loc, a.type);  // doublons à l'intérieur du fichier
        ++report.imported;
    }
    report.fileFullyImported = !items.isEmpty() && scan.value().fullyRepresentable() &&
                               report.imported + report.duplicates == items.size();
    return report;
}

Result<PdfExportReport> PdfInteropService::exportAnnotatedCopy(qint64 documentId, const QString& target,
                                                               const PdfColorScheme& colors, bool withComments)
{
    auto path = pdfPath(documentId);
    if (!path)
        return path.error();
    if (QFileInfo(target).absoluteFilePath() == QFileInfo(path.value()).absoluteFilePath())
        return Error::make(Error::Code::InvalidArgument,
                           tr("Choisissez un autre fichier : l'original de la bibliothèque n'est jamais modifié."));
    QFile source(path.value());
    if (!source.open(QIODevice::ReadOnly))
        return Error::make(Error::Code::Io, tr("Lecture impossible de %1 : %2").arg(path.value(), source.errorString()));
    const QByteArray original = source.readAll();

    // Annotations déjà dans le fichier (import antérieur, autre logiciel) : à ne pas dupliquer.
    QList<pdf::PdfAnnotation> present;
    if (auto scan = pdf::readPdfAnnotations(original))
        present = scan.value().annotations;
    const auto alreadyThere = [&present](const pdf::PdfAnnotationOut& o) {
        for (const pdf::PdfAnnotation& p : present)
            if (pdf::sameAnnotationGeometry(o.page, o.type, o.rects, p.page, p.type, p.rects))
                return true;
        return false;
    };

    auto records = m_annotations.list(documentId);
    if (!records)
        return records.error();
    PdfExportReport report;
    QList<pdf::PdfAnnotationOut> out;
    QList<bool> isComment;
    for (const AnnotationRecord& r : records.value()) {
        if (r.type == AnnotationType::Bookmark)
            continue;
        const auto loc = PdfLocator::fromJson(r.locator);
        if (!loc || loc->rects.isEmpty())
            continue;
        pdf::PdfAnnotationOut o;
        o.page = loc->page;
        o.type = r.type;
        o.fill = colors.fill[size_t(r.color)];
        o.stroke = colors.stroke[size_t(r.color)];
        o.rects = loc->rects;
        o.contents = r.note.trimmed();
        o.id = r.uuid;
        o.modified = parseTimestamp(r.updatedAt);
        if (alreadyThere(o)) {
            ++report.alreadyPresent;
            continue;
        }
        out << o;
        isComment << false;
    }
    if (withComments) {
        auto comments = m_comments.list(documentId);
        if (!comments)
            return comments.error();
        std::map<int, int> perPage;  // notes de page empilées dans la marge
        for (const CommentRecord& c : comments.value()) {
            if (c.anchor != CommentAnchor::Page && c.anchor != CommentAnchor::Passage)
                continue;
            const auto loc = PdfLocator::fromJson(c.locator);
            if (!loc)
                continue;
            QPointF point;
            if (loc->rects.isEmpty()) {
                const int k = perPage[loc->page]++;
                point = QPointF(0.03, std::min(0.95, 0.03 + 0.035 * k));
            } else {
                const QRectF b = loc->bounds();
                point = QPointF(std::max(0.005, b.left() - 0.035), b.top());
            }
            pdf::PdfAnnotationOut o;
            o.page = loc->page;
            o.type = AnnotationType::Note;
            o.fill = colors.fill[size_t(AnnotationColor::Blue)];
            o.stroke = colors.stroke[size_t(AnnotationColor::Blue)];
            o.rects = {QRectF(point.x() - 0.005, point.y() - 0.005, 0.01, 0.01)};
            QString text = commentKindLabel(c.kind) + QStringLiteral(" — ") + c.body.trimmed();
            if (c.anchor == CommentAnchor::Passage && !c.quote.isEmpty())
                text = QStringLiteral("« %1 »\n\n").arg(normalizeQuote(c.quote)) + text;
            o.contents = text;
            o.id = c.uuid;
            o.modified = parseTimestamp(c.updatedAt);
            if (alreadyThere(o)) {
                ++report.alreadyPresent;
                continue;
            }
            out << o;
            isComment << true;
        }
    }

    auto written = pdf::writeAnnotatedCopy(original, out);
    if (!written)
        return written.error();
    report.skipped = written.value().skipped;
    for (int i = 0; i < isComment.size(); ++i) {
        if (written.value().skippedIndexes.contains(i))
            continue;
        isComment.at(i) ? ++report.comments : ++report.annotations;
    }

    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly))
        return Error::make(Error::Code::Io, tr("Écriture impossible de %1 : %2").arg(target, file.errorString()));
    file.write(written.value().data);
    if (!file.commit())
        return Error::make(Error::Code::Io, tr("Écriture impossible de %1 : %2").arg(target, file.errorString()));
    return report;
}

AnnotationColor PdfInteropService::nearestColor(const QColor& color, const PdfColorScheme& scheme)
{
    if (!color.isValid())
        return AnnotationColor::Yellow;
    const QColor hsv = color.toHsv();
    AnnotationColor best = AnnotationColor::Yellow;
    double bestD = 1e9;
    for (const AnnotationColor c : allAnnotationColors()) {
        for (const QColor& ref : {scheme.fill[size_t(c)], scheme.stroke[size_t(c)]}) {
            if (!ref.isValid())
                continue;
            const QColor r = ref.toHsv();
            double d;
            if (hsv.hsvSaturationF() < 0.12 || hsv.hsvHueF() < 0) {
                // Gris : seule la distance RVB a un sens.
                d = std::hypot(color.redF() - ref.redF(), color.greenF() - ref.greenF(), color.blueF() - ref.blueF()) + 1;
            } else {
                double dh = std::abs(hsv.hsvHueF() - r.hsvHueF());
                dh = std::min(dh, 1 - dh);
                d = dh * 4 + std::hypot(color.redF() - ref.redF(), color.greenF() - ref.greenF(), color.blueF() - ref.blueF()) * 0.5;
            }
            if (d < bestD) {
                bestD = d;
                best = c;
            }
        }
    }
    return best;
}

QString PdfInteropService::suggestedFileName(const DocumentRecord& document)
{
    QString base = document.title.trimmed();
    if (base.isEmpty())
        base = QFileInfo(document.filePath).completeBaseName();
    if (base.isEmpty())
        base = tr("Document");
    static const QRegularExpression forbidden(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1F]"));
    base.replace(forbidden, QStringLiteral(" "));
    base = base.simplified().left(120);
    return tr("%1 (annoté).pdf").arg(base);
}

}  // namespace studylab
