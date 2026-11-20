#include "services/search/SearchService.h"

#include "data/repositories/PageTextRepository.h"

#include "core/domain/Comment.h"
#include "core/domain/Note.h"
#include "data/repositories/DocumentRepository.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>

namespace studylab {

namespace {

QString marks()
{
    return QStringLiteral("'%1', '%2', '…', 14").arg(QChar(SearchHit::kMarkStart)).arg(QChar(SearchHit::kMarkEnd));
}

/// Extrait d'une note sans balisage Markdown gênant (front matter, titres, crochets de liens).
QString cleanNoteSnippet(const QString& snippet)
{
    static const QRegularExpression frontMatter(QStringLiteral("^\\s*---[\\s\\S]*?\\n---\\s*"));
    static const QRegularExpression inlineMarks(QStringLiteral("(\\[\\[|\\]\\]|\\*\\*|__|==)"));
    static const QRegularExpression headings(QStringLiteral("(^|\\n)#+\\s*"));
    QString s = snippet;
    s.remove(frontMatter);
    s.remove(inlineMarks);
    s.replace(headings, QStringLiteral(" "));
    return s.simplified();
}

DocumentFormat formatOf(const QString& key)
{
    return documentFormatFromKey(key).value_or(DocumentFormat::Pdf);
}

}  // namespace

SearchService::SearchService(db::Database& db) : m_db(db) {}

QString SearchService::plain(const QString& snippet)
{
    QString s = snippet;
    s.remove(QChar(SearchHit::kMarkStart));
    s.remove(QChar(SearchHit::kMarkEnd));
    return s;
}

Result<SearchResults> SearchService::search(const QString& text, int perGroup)
{
    SearchResults out;
    out.query = text;
    const QString fts = db::DocumentRepository::toFtsQuery(text);
    if (fts.isEmpty())
        return out;

    // Documents : table sans contenu (pas d'extrait) ; on affiche les auteurs.
    {
        auto stmt = m_db.prepare(QStringLiteral(
            "SELECT d.id, d.title, d.format, (SELECT group_concat(name, ', ') FROM (SELECT au.name FROM document_authors da "
            "JOIN authors au ON au.id = da.author_id WHERE da.document_id = d.id ORDER BY da.position)) "
            "FROM fts_documents f JOIN documents d ON d.id = f.rowid "
            "WHERE fts_documents MATCH ?1 AND d.deleted_at IS NULL ORDER BY f.rank LIMIT ?2"));
        if (!stmt)
            return stmt.error();
        stmt.value().bind(1, fts).bind(2, perGroup);
        while (true) {
            auto row = stmt.value().step();
            if (!row)
                return row.error();
            if (!row.value())
                break;
            SearchHit h;
            h.kind = SearchHit::Kind::Document;
            h.documentId = h.itemId = stmt.value().columnInt64(0);
            h.documentTitle = stmt.value().columnText(1);
            h.format = formatOf(stmt.value().columnText(2));
            h.snippet = stmt.value().columnText(3);
            out.documents << h;
        }
    }

    // Annotations : passage cité ou titre.
    {
        auto stmt = m_db.prepare(QStringLiteral(
            "SELECT a.id, a.document_id, d.title, d.format, a.color, a.unit_index, a.page_label, a.locator_json, "
            "snippet(fts_annotations, -1, %1) "
            "FROM fts_annotations f JOIN annotations a ON a.id = f.rowid JOIN documents d ON d.id = a.document_id "
            "WHERE fts_annotations MATCH ?1 AND a.deleted_at IS NULL AND d.deleted_at IS NULL "
            "ORDER BY f.rank LIMIT ?2").arg(marks()));
        if (!stmt)
            return stmt.error();
        stmt.value().bind(1, fts).bind(2, perGroup);
        while (true) {
            auto row = stmt.value().step();
            if (!row)
                return row.error();
            if (!row.value())
                break;
            const auto& s = stmt.value();
            SearchHit h;
            h.kind = SearchHit::Kind::Annotation;
            h.itemId = s.columnInt64(0);
            h.documentId = s.columnInt64(1);
            h.documentTitle = s.columnText(2);
            h.format = formatOf(s.columnText(3));
            h.color = annotationColorFromKey(s.columnText(4)).value_or(AnnotationColor::Yellow);
            AnnotationRecord r;
            r.unitIndex = int(s.columnInt64(5));
            r.pageLabel = s.columnText(6);
            r.locator = QJsonDocument::fromJson(s.columnText(7).toUtf8()).object();
            h.location = annotationLocation(r);
            h.snippet = normalizeQuote(s.columnText(8));
            out.annotations << h;
        }
    }

    // Commentaires (y compris les notes d'annotation).
    {
        auto stmt = m_db.prepare(QStringLiteral(
            "SELECT c.id, c.document_id, d.title, d.format, c.kind, c.anchor_kind, c.unit_index, "
            "COALESCE(a.page_label, json_extract(c.locator_json, '$.pageLabel')), COALESCE(a.locator_json, c.locator_json), "
            "snippet(fts_comments, 0, %1) "
            "FROM fts_comments f JOIN comments c ON c.id = f.rowid JOIN documents d ON d.id = c.document_id "
            "LEFT JOIN annotations a ON a.id = c.annotation_id "
            "WHERE fts_comments MATCH ?1 AND c.deleted_at IS NULL AND d.deleted_at IS NULL "
            "AND (c.annotation_id IS NULL OR a.deleted_at IS NULL) ORDER BY f.rank LIMIT ?2").arg(marks()));
        if (!stmt)
            return stmt.error();
        stmt.value().bind(1, fts).bind(2, perGroup);
        while (true) {
            auto row = stmt.value().step();
            if (!row)
                return row.error();
            if (!row.value())
                break;
            const auto& s = stmt.value();
            SearchHit h;
            h.kind = SearchHit::Kind::Comment;
            h.itemId = s.columnInt64(0);
            h.documentId = s.columnInt64(1);
            h.documentTitle = s.columnText(2);
            h.format = formatOf(s.columnText(3));
            const CommentKind kind = commentKindFromKey(s.columnText(4)).value_or(CommentKind::Reflection);
            h.commentKind = commentKindLabel(kind);
            CommentRecord c;
            c.anchor = commentAnchorFromKey(s.columnText(5)).value_or(CommentAnchor::Free);
            c.unitIndex = s.isNull(6) ? -1 : int(s.columnInt64(6));
            c.pageLabel = s.columnText(7);
            if (!s.isNull(8))
                c.locator = QJsonDocument::fromJson(s.columnText(8).toUtf8()).object();
            h.location = commentLocation(c);
            h.snippet = s.columnText(9).simplified();
            out.comments << h;
        }
    }

    // Notes (hors corbeille) : titre, extrait du texte.
    {
        auto stmt = m_db.prepare(QStringLiteral(
            "SELECT n.id, n.title, n.note_type, snippet(fts_notes, 1, %1) "
            "FROM fts_notes f JOIN notes n ON n.id = f.rowid "
            "WHERE fts_notes MATCH ?1 AND n.deleted_at IS NULL ORDER BY f.rank LIMIT ?2").arg(marks()));
        if (!stmt)
            return stmt.error();
        stmt.value().bind(1, fts).bind(2, perGroup);
        while (true) {
            auto row = stmt.value().step();
            if (!row)
                return row.error();
            if (!row.value())
                break;
            const auto& s = stmt.value();
            SearchHit h;
            h.kind = SearchHit::Kind::Note;
            h.itemId = s.columnInt64(0);
            h.documentTitle = s.columnText(1);
            h.location = noteTypeLabel(noteTypeFromKey(s.columnText(2)).value_or(NoteType::Personal));
            h.snippet = cleanNoteSnippet(s.columnText(3));
            out.notes << h;
        }
    }

    // Texte intégral : au plus deux passages par document, pour que plusieurs documents apparaissent.
    {
        auto stmt = m_db.prepare(QStringLiteral(
            "SELECT p.rowid, p.document_id, p.unit_index, d.title, d.format, snippet(fts_page_text, 0, %1) "
            "FROM fts_page_text f JOIN page_text p ON p.rowid = f.rowid JOIN documents d ON d.id = p.document_id "
            "WHERE fts_page_text MATCH ?1 AND d.deleted_at IS NULL ORDER BY f.rank LIMIT ?2").arg(marks()));
        if (!stmt)
            return stmt.error();
        stmt.value().bind(1, fts).bind(2, perGroup * 8);
        QHash<qint64, int> perDocument;
        while (out.passages.size() < perGroup) {
            auto row = stmt.value().step();
            if (!row)
                return row.error();
            if (!row.value())
                break;
            const auto& s = stmt.value();
            const qint64 doc = s.columnInt64(1);
            if (perDocument.value(doc) >= 2)
                continue;
            perDocument[doc] += 1;
            SearchHit h;
            h.kind = SearchHit::Kind::Passage;
            h.itemId = s.columnInt64(0);
            h.documentId = doc;
            h.unit = int(s.columnInt64(2));
            h.documentTitle = s.columnText(3);
            h.format = formatOf(s.columnText(4));
            h.location = h.format == DocumentFormat::Pdf
                             ? QCoreApplication::translate("SearchService", "p. %1").arg(h.unit + 1)
                             : QCoreApplication::translate("SearchService", "section %1").arg(h.unit + 1);
            h.snippet = s.columnText(5).simplified();
            out.passages << h;
        }
    }
    return out;
}

Result<ResolvedLink> SearchService::resolve(const AppLink& link)
{
    auto lookup = [this](const QString& sql, const QString& uuid, qint64 documentId) -> Result<qint64> {
        auto stmt = m_db.prepare(sql);
        if (!stmt)
            return stmt.error();
        stmt.value().bind(1, uuid);
        if (documentId > 0)
            stmt.value().bind(2, documentId);
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        return row.value() ? stmt.value().columnInt64(0) : qint64(0);
    };
    ResolvedLink r;
    auto doc = lookup(QStringLiteral("SELECT id FROM documents WHERE uuid = ?1 AND deleted_at IS NULL"), link.documentUuid, 0);
    if (!doc)
        return doc.error();
    if (doc.value() == 0)
        return Error::make(Error::Code::NotFound,
                           QCoreApplication::translate("SearchService", "Ce document n'est plus dans la bibliothèque."));
    r.documentId = doc.value();
    if (!link.annotationUuid.isEmpty()) {
        auto id = lookup(QStringLiteral("SELECT id FROM annotations WHERE uuid = ?1 AND document_id = ?2 AND deleted_at IS NULL"),
                         link.annotationUuid, r.documentId);
        if (!id)
            return id.error();
        r.itemId = id.value();
    } else if (!link.commentUuid.isEmpty()) {
        auto id = lookup(QStringLiteral("SELECT id FROM comments WHERE uuid = ?1 AND document_id = ?2 AND deleted_at IS NULL"),
                         link.commentUuid, r.documentId);
        if (!id)
            return id.error();
        r.itemId = id.value();
        r.comment = true;
    }
    return r;
}

Result<QHash<int, OcrPage>> SearchService::ocrPages(qint64 documentId)
{
    auto words = db::PageTextRepository(m_db).ocrWords(documentId);
    if (!words)
        return words.error();
    QHash<int, OcrPage> out;
    for (const auto& [page, json] : words.value()) {
        OcrPage p = OcrPage::fromJson(json);
        if (!p.isEmpty())
            out.insert(page, std::move(p));
    }
    return out;
}

}  // namespace studylab
