#include "ui/notes/NotePreview.h"

#include "core/domain/Comment.h"
#include "core/domain/Document.h"
#include "core/domain/Note.h"
#include "core/domain/Reference.h"
#include "services/notes/NoteService.h"
#include "services/references/ReferenceService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace studylab::ui {

namespace {
QString tr_(const char* text) { return QCoreApplication::translate("NotesPage", text); }
}  // namespace

QString attachmentDataUri(const QString& attachmentsDir, const QString& relPath)
{
    static const QRegularExpression safe(QStringLiteral("^attachments/[\\w.-]+$"));
    if (!safe.match(relPath).hasMatch())
        return {};
    QFile f(QDir(attachmentsDir).filePath(relPath.mid(12)));
    if (f.size() >= 20 * 1024 * 1024 || !f.open(QIODevice::ReadOnly))
        return {};
    QString ext = relPath.section(u'.', -1).toLower();
    if (ext == QLatin1String("jpg"))
        ext = QStringLiteral("jpeg");
    return QStringLiteral("data:image/%1;base64,").arg(ext) + QString::fromLatin1(f.readAll().toBase64());
}

QJsonObject notePreviewPayload(NoteService& notes, ReferenceService& refs, const QString& title, const QString& folder,
                               const QString& text, const std::function<QString(const QString&)>& imageData)
{
    const FrontMatter fm = parseFrontMatter(text);
    QJsonArray chips;
    if (!folder.isEmpty())
        chips.append(folder.section(u'/', -1));
    for (const QString& t : noteTags(text))
        chips.append(QStringLiteral("#") + t);

    QStringList targets;
    for (const QString& t : wikiLinkTargets(fm.present ? text.mid(fm.bodyStart) : text))
        if (!t.startsWith(u'@'))
            targets << t;
    QJsonArray unresolved;
    QJsonArray documents;
    const QHash<QString, NoteLinkTarget> resolved = notes.resolveMany(targets);
    for (auto it = resolved.cbegin(); it != resolved.cend(); ++it) {
        if (!it.value().resolved())
            unresolved.append(it.key());
        else if (it.value().kind == NoteLinkTarget::Kind::Document)
            documents.append(it.key());
    }

    // Citations [[@clé]] : libellé auteur-date, entrée complète ; bibliographie en fin d'aperçu.
    QJsonObject citations;
    QJsonArray bibliography;
    const QHash<QString, Reference> cited = refs.resolveKeys(citedKeys(text));
    QList<Reference> citedRefs = cited.values();
    std::sort(citedRefs.begin(), citedRefs.end(), [](const Reference& a, const Reference& b) {
        return foldForSort(formatBibliography(a)) < foldForSort(formatBibliography(b));
    });
    for (const Reference& r : std::as_const(citedRefs)) {
        citations.insert(r.citekey.toLower(), QJsonObject{{QStringLiteral("label"), formatCitation(r)},
                                                          {QStringLiteral("entry"), formatBibliography(r)}});
        bibliography.append(formatBibliography(r, true));
    }

    QJsonObject images;
    static const QRegularExpression img(QStringLiteral("!\\[[^\\]]*\\]\\((attachments/[\\w.-]+)\\)"));
    auto it = img.globalMatch(text);
    while (it.hasNext()) {
        const QString rel = it.next().captured(1);
        const QString uri = imageData ? imageData(rel) : QString();
        if (!uri.isEmpty())
            images.insert(rel, uri);
    }
    return {
        {QStringLiteral("title"), title},
        {QStringLiteral("body"), fm.present ? text.mid(fm.bodyStart) : text},
        {QStringLiteral("lineOffset"), fm.present ? fm.lineCount : 0},
        {QStringLiteral("chips"), chips},
        {QStringLiteral("unresolved"), unresolved},
        {QStringLiteral("unresolvedLabel"), tr_("note à créer")},
        {QStringLiteral("documents"), documents},
        {QStringLiteral("documentLabel"), tr_("document de la bibliothèque")},
        {QStringLiteral("images"), images},
        {QStringLiteral("citations"), citations},
        {QStringLiteral("citationMissingLabel"), tr_("référence introuvable")},
        {QStringLiteral("bibliography"), bibliography},
        {QStringLiteral("bibliographyTitle"), tr_("Références")},
    };
}

}  // namespace studylab::ui
