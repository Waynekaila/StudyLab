#include "core/domain/Note.h"
#include "data/migrations/Migrator.h"
#include "data/sqlite/Database.h"
#include "data/repositories/DocumentRepository.h"
#include "services/annotations/AnnotationService.h"
#include "services/comments/CommentService.h"
#include "services/notes/NoteDrafts.h"
#include "services/notes/NoteService.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

/// Espace de notes : front matter, tags, plan, noms de fichier, liens [[…]], rétroliens, renommage.
class TstNotes : public QObject {
    Q_OBJECT

    std::unique_ptr<Database> m_db;
    QTemporaryDir m_dir;

    NoteService service() { return NoteService(*m_db, m_dir.filePath(QStringLiteral("attachments"))); }

private slots:
    void init()
    {
        m_db = std::move(Database::openInMemory().value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*m_db).ok());
    }

    void typesAndTemplates()
    {
        QCOMPARE(allNoteTypes().size(), 8);
        for (NoteType t : allNoteTypes()) {
            QCOMPARE(noteTypeFromKey(noteTypeKey(t)), std::optional(t));
            QVERIFY(!noteTypeLabel(t).isEmpty());
            const QString tpl = noteTemplate(t, QStringLiteral("X"));
            if (t != NoteType::Personal)
                QVERIFY(parseFrontMatter(tpl).present);
        }
        QCOMPARE(noteTypeFromKey(QStringLiteral("inconnu")), std::nullopt);
        QVERIFY(noteTemplate(NoteType::Formula, {}).contains(QStringLiteral("$$")));
    }

    void frontMatter()
    {
        const QString md = QStringLiteral(
            "---\n"
            "type: cours\n"
            "matière: \"[[Thermodynamique]]\"\n"
            "tags: [entropie, examen]\n"
            "aliases:\n"
            "  - Deuxième principe\n"
            "  - Principe d'évolution\n"
            "brouillon: true\n"
            "---\n"
            "# Second principe\n\nTexte #a-revoir et `#pasuntag` ; #42 n'est pas un tag ; l'ancre http://x.fr/#frag non plus.\n"
            "```\n# pas un titre\n#codetag\n```\n"
            "## Inégalité de Clausius ##\n");
        const FrontMatter fm = parseFrontMatter(md);
        QVERIFY(fm.present);
        QCOMPARE(fm.lineCount, 9);
        QCOMPARE(fm.values.value(QStringLiteral("type")).toString(), QStringLiteral("cours"));
        QCOMPARE(fm.values.value(QStringLiteral("matière")).toString(), QStringLiteral("[[Thermodynamique]]"));
        QCOMPARE(fm.values.value(QStringLiteral("tags")).toArray().size(), 2);
        QCOMPARE(fm.values.value(QStringLiteral("brouillon")).toBool(), true);
        QVERIFY(md.mid(fm.bodyStart).startsWith(QStringLiteral("# Second principe")));
        QCOMPARE(stripFrontMatter(md), md.mid(fm.bodyStart));

        QCOMPARE(noteTags(md), (QStringList{QStringLiteral("entropie"), QStringLiteral("examen"), QStringLiteral("a-revoir")}));
        QCOMPARE(noteAliases(md), (QStringList{QStringLiteral("Deuxième principe"), QStringLiteral("Principe d'évolution")}));
        QCOMPARE(noteAliases(QStringLiteral("---\naliases: Carnot, Cycle idéal\n---\n")),
                 (QStringList{QStringLiteral("Carnot"), QStringLiteral("Cycle idéal")}));

        const auto outline = noteOutline(md);
        QCOMPARE(outline.size(), 2);
        QCOMPARE(outline.at(0).text, QStringLiteral("Second principe"));
        QCOMPARE(outline.at(0).level, 1);
        QCOMPARE(outline.at(0).line, 9);
        QCOMPARE(outline.at(1).text, QStringLiteral("Inégalité de Clausius"));
        QCOMPARE(outline.at(1).level, 2);

        // Pas de front matter : bloc non fermé ou absent.
        QVERIFY(!parseFrontMatter(QStringLiteral("---\ntags: [a]\nsans fin")).present);
        QVERIFY(!parseFrontMatter(QStringLiteral("Texte\n---\n")).present);
        QCOMPARE(noteTags(QStringLiteral("---\ntags: thermo, #cycles\n---\n")),
                 (QStringList{QStringLiteral("thermo"), QStringLiteral("cycles")}));
    }

    void slugsAndKeys()
    {
        QCOMPARE(noteSlug(QStringLiteral("Q : énergie / travail ?")), QStringLiteral("Q énergie travail"));
        QCOMPARE(noteSlug(QStringLiteral("  ...  ")), QStringLiteral("Sans titre"));
        QCOMPARE(noteSlug(QStringLiteral("CON")), QStringLiteral("CON (note)"));
        QCOMPARE(noteSlug(QStringLiteral("Fin.")), QStringLiteral("Fin"));
        QCOMPARE(wikiLinkKey(QStringLiteral("Cycle de Carnot#Rendement|le cycle")), QStringLiteral("cycle de carnot"));
        QCOMPARE(wikiLinkKey(QStringLiteral("ÉNERGIE.md")), wikiLinkKey(QStringLiteral("énergie")));
        QCOMPARE(wikiLinkKey(QStringLiteral("Bloc^abc")), QStringLiteral("bloc"));
        QCOMPARE(NoteService::normalizeFolder(QStringLiteral(" Thermo / Cours/ ")), QStringLiteral("Thermo/Cours"));
        QCOMPARE(NoteService::normalizeFolder(QStringLiteral("//")), QString());

        const QString snippet = wikiLinkSnippet(QStringLiteral("---\ntags: []\n---\nLe rendement du [[Cycle de Carnot|cycle]] est maximal."),
                                                {QStringLiteral("cycle de carnot")}, 5);
        QVERIFY2(snippet.contains(QStringLiteral("[[Cycle de Carnot|cycle]]")), qPrintable(snippet));
        QVERIFY(snippet.startsWith(QStringLiteral("…")));
    }

    void createSaveAndUniqueTitles()
    {
        NoteService notes = service();
        QSignalSpy added(&notes, &NoteService::noteAdded);
        auto a = notes.create(QStringLiteral("Second principe"), NoteType::Course, QStringLiteral("Thermodynamique / Cours"));
        QVERIFY2(a.ok(), qPrintable(a.ok() ? QString() : a.error().message));
        QCOMPARE(added.count(), 1);
        QCOMPARE(a.value().folder, QStringLiteral("Thermodynamique/Cours"));
        QCOMPARE(a.value().type, NoteType::Course);
        QVERIFY(a.value().body.contains(QStringLiteral("tags: [cours]")));
        QCOMPARE(a.value().frontMatter.value(QStringLiteral("tags")).toArray().first().toString(), QStringLiteral("cours"));

        auto b = notes.create(QStringLiteral("second principe"), NoteType::Personal);
        QVERIFY2(b.ok(), qPrintable(b.ok() ? QString() : b.error().message));
        QCOMPARE(b.value().title, QStringLiteral("second principe 2"));
        QCOMPARE(b.value().slug, QStringLiteral("second principe 2"));
        auto c = notes.create(QString(), NoteType::Personal);
        QCOMPARE(c.value().title, QStringLiteral("Sans titre"));
        QCOMPARE(c.value().body, QString());

        auto saved = notes.save(c.value().id, QStringLiteral("Énergie : définition"), QStringLiteral("Corps"));
        QVERIFY(saved.ok());
        QCOMPARE(saved.value().title, QStringLiteral("Énergie : définition"));
        QCOMPARE(saved.value().slug, QStringLiteral("Énergie définition"));
        QVERIFY(!notes.save(c.value().id, QStringLiteral("   "), QString()).ok());
        QCOMPARE(notes.count().value(), 3);

        QCOMPARE(notes.setPinned(a.value().id, true).value().pinned, true);
        QCOMPARE(notes.setType(a.value().id, NoteType::Concept).value().type, NoteType::Concept);
        QCOMPARE(notes.list().value().first().folder, QString());  // racine d'abord
    }

    void linksBacklinksAndRename()
    {
        NoteService notes = service();
        QSignalSpy linksChanged(&notes, &NoteService::linksChanged);
        QSignalSpy changed(&notes, &NoteService::noteChanged);
        const qint64 carnot = notes.create(QStringLiteral("Cycle de Carnot"), NoteType::Concept).value().id;
        const qint64 principe = notes.create(QStringLiteral("Second principe"), NoteType::Course).value().id;
        QVERIFY(notes.save(principe, QStringLiteral("Second principe"),
                           QStringLiteral("Voir [[cycle de carnot|le cycle]], [[Cycle de Carnot#Rendement]], "
                                          "[[Production d'entropie]] et [[@cengel2019, p. 411]].")).ok());
        auto links = notes.links(principe).value();
        QCOMPARE(links.size(), 2);  // doublons fusionnés, citation ignorée
        QCOMPARE(links.at(0).noteId, carnot);
        QCOMPARE(links.at(1).target, QStringLiteral("Production d'entropie"));
        QCOMPARE(links.at(1).noteId, 0);

        auto back = notes.backlinks(carnot).value();
        QCOMPARE(back.size(), 1);
        QCOMPARE(back.first().noteId, principe);
        QVERIFY(back.first().snippet.contains(QStringLiteral("[[cycle de carnot|le cycle]]")));

        // Le lien non résolu se résout dès que la note existe ; il redevient non résolu à sa suppression.
        linksChanged.clear();
        const qint64 production = notes.create(QStringLiteral("Production d'entropie"), NoteType::Concept).value().id;
        QVERIFY(linksChanged.count() >= 1);
        QCOMPARE(notes.links(principe).value().at(1).noteId, production);
        QVERIFY(notes.remove(production).ok());
        QCOMPARE(notes.links(principe).value().at(1).noteId, 0);
        QVERIFY(notes.restore(production).ok());
        QCOMPARE(notes.links(principe).value().at(1).noteId, production);
        QVERIFY(!notes.find(999).ok());

        // Alias
        QVERIFY(notes.save(carnot, QStringLiteral("Cycle de Carnot"), QStringLiteral("---\naliases: [Carnot]\n---\nCycle idéal.")).ok());
        QCOMPARE(notes.resolve(QStringLiteral("carnot")).value(), carnot);
        QCOMPARE(notes.resolve(QStringLiteral("@cengel2019")).value(), 0);

        // Renommage : les liens des autres notes suivent, section et alias conservés.
        changed.clear();
        QVERIFY(notes.save(carnot, QStringLiteral("Cycle de Carnot idéal"), QStringLiteral("---\naliases: [Carnot]\n---\nCycle idéal.")).ok());
        const QString body = notes.find(principe).value().body;
        QVERIFY2(body.contains(QStringLiteral("[[Cycle de Carnot idéal|le cycle]]")), qPrintable(body));
        QVERIFY(body.contains(QStringLiteral("[[Cycle de Carnot idéal#Rendement]]")));
        QCOMPARE(notes.links(principe).value().at(0).noteId, carnot);
        QCOMPARE(changed.count(), 2);  // la note renommée et celle dont les liens ont changé

        // Une note supprimée ne figure plus dans les rétroliens.
        QVERIFY(notes.remove(principe).ok());
        QVERIFY(notes.backlinks(carnot).value().isEmpty());
    }

    void foldersAndImages()
    {
        NoteService notes = service();
        const qint64 a = notes.create(QStringLiteral("A"), NoteType::Personal, QStringLiteral("Thermo/Cours")).value().id;
        const qint64 b = notes.create(QStringLiteral("B"), NoteType::Personal, QStringLiteral("Thermo")).value().id;
        const qint64 c = notes.create(QStringLiteral("C"), NoteType::Personal, QStringLiteral("Thermodynamique")).value().id;
        QCOMPARE(notes.renameFolder(QStringLiteral("Thermo"), QStringLiteral("Physique/Thermo")).value(), 2);
        QCOMPARE(notes.find(a).value().folder, QStringLiteral("Physique/Thermo/Cours"));
        QCOMPARE(notes.find(b).value().folder, QStringLiteral("Physique/Thermo"));
        QCOMPARE(notes.find(c).value().folder, QStringLiteral("Thermodynamique"));
        QVERIFY(!notes.renameFolder(QStringLiteral("Physique"), QStringLiteral("Physique/Sous")).ok());
        QCOMPARE(notes.setFolder(c, QString()).value().folder, QString());

        auto rel = notes.addImage(a, QByteArray("\x89PNG\r\n", 6), QStringLiteral("PNG"));
        QVERIFY(rel.ok());
        QVERIFY(rel.value().startsWith(QStringLiteral("attachments/")));
        QVERIFY(rel.value().endsWith(QStringLiteral(".png")));
        QVERIFY(QFile::exists(QDir(notes.attachmentsDir()).filePath(rel.value().mid(12))));
        QCOMPARE(m_db->queryInt(QStringLiteral("SELECT count(*) FROM attachments WHERE note_id = %1").arg(a)).value(), 1);
        QVERIFY(!notes.addImage(a, QByteArray("x"), QStringLiteral("exe")).ok());
    }

    void notesFromReadingKeepTheirSources()
    {
        DocumentRepository docs(*m_db);
        DocumentRecord doc;
        doc.title = QStringLiteral("Thermodynamique");
        doc.fileHash = QStringLiteral("h1");
        doc.filePath = QStringLiteral("h1.pdf");
        doc.authors = {QStringLiteral("Yunus A. Çengel"), QStringLiteral("Michael A. Boles")};
        doc.year = 2019;
        QVERIFY(docs.insert(doc).ok());
        doc = docs.findById(doc.id).value();

        AnnotationService annotations(*m_db);
        auto annotate = [&](int page, double y, const QString& text) {
            PdfLocator l;
            l.page = page;
            l.rects = {QRectF(0.1, y, 0.8, 0.02)};
            l.exact = text;
            AnnotationRecord r;
            r.documentId = doc.id;
            r.locator = l.toJson();
            r.selectedText = text;
            return annotations.create(r).value();
        };
        const AnnotationRecord second = annotate(410, 0.5, QStringLiteral("L'entropie d'un système isolé ne peut que croître."));
        const AnnotationRecord first = annotate(404, 0.2, QStringLiteral("Inégalité de\nClausius"));
        QVERIFY(annotations.setNote(second.id, QStringLiteral("Principe d'évolution.")).ok());
        CommentService comments(*m_db);
        CommentRecord q;
        q.documentId = doc.id;
        q.kind = CommentKind::Question;
        q.body = QStringLiteral("Pourquoi $\\delta Q / T$ et pas $dQ$ ?");
        const CommentRecord question = comments.create(q).value();

        // Une annotation
        const NoteDraft one = NoteDrafts::fromAnnotation(doc, annotations.find(second.id).value());
        QCOMPARE(one.title, QStringLiteral("L'entropie d'un système isolé ne peut que croître."));
        QCOMPARE(one.folder, QStringLiteral("Notes de lecture"));
        QVERIFY(one.body.contains(QStringLiteral("> L'entropie d'un système isolé ne peut que croître.")));
        QVERIFY2(one.body.contains(QStringLiteral("> — *Yunus A. Çengel*, [[Thermodynamique]], [p. 411](studylab://open?doc=")),
                 qPrintable(one.body));
        QVERIFY(one.body.contains(QStringLiteral("Principe d'évolution.")));
        QCOMPARE(parseFrontMatter(one.body).values.value(QStringLiteral("année")).toString(), QStringLiteral("2019"));

        // Plusieurs annotations : ordre de lecture
        const NoteDraft many = NoteDrafts::fromAnnotations(doc, annotations.list(doc.id).value());
        QCOMPARE(many.title, QStringLiteral("Thermodynamique — passages"));
        QVERIFY(many.body.indexOf(QStringLiteral("Clausius")) < many.body.indexOf(QStringLiteral("isolé")));
        QVERIFY(many.body.contains(QStringLiteral("> Inégalité de Clausius")));

        // Commentaire (question → note de recherche)
        const NoteDraft fromQ = NoteDrafts::fromComment(doc, comments.find(question.id).value());
        QCOMPARE(fromQ.type, NoteType::Research);
        QCOMPARE(fromQ.title, QStringLiteral("Pourquoi \\delta Q / T et pas dQ ?"));
        QVERIFY(fromQ.body.contains(QStringLiteral("&comment=") + question.uuid));

        // Document entier
        const NoteDraft reading = NoteDrafts::fromDocument(doc, annotations.list(doc.id).value(), comments.list(doc.id).value());
        QCOMPARE(reading.title, QStringLiteral("Lecture · Thermodynamique"));
        QVERIFY(reading.body.contains(QStringLiteral("## Passages annotés")));
        QVERIFY(reading.body.contains(QStringLiteral("## Questions en suspens\n\n- [ ] Pourquoi")));

        NoteService notes = service();
        auto created = notes.createDerived(reading.title, reading.type, reading.folder, reading.body,
                                           {{QStringLiteral("document"), doc.id},
                                            {QStringLiteral("annotation"), second.id},
                                            {QStringLiteral("comment"), question.id}});
        QVERIFY2(created.ok(), qPrintable(created.ok() ? QString() : created.error().message));
        const qint64 id = created.value().id;
        auto sources = notes.sources(id).value();
        QCOMPARE(sources.size(), 3);
        QCOMPARE(sources.at(0).kind, QStringLiteral("document"));
        QCOMPARE(sources.at(0).documentTitle, QStringLiteral("Thermodynamique"));
        QCOMPARE(sources.at(0).authors.first(), QStringLiteral("Yunus A. Çengel"));
        QCOMPARE(sources.at(1).kind, QStringLiteral("annotation"));
        QCOMPARE(sources.at(1).documentId, doc.id);
        QCOMPARE(sources.at(1).itemUuid, second.uuid);
        QCOMPARE(sources.at(1).unitIndex, 410);
        QVERIFY(sources.at(1).quote.contains(QStringLiteral("isolé")));
        QCOMPARE(sources.at(2).kind, QStringLiteral("comment"));
        QVERIFY(!sources.at(2).missing);
        QVERIFY(!notes.createDerived(QStringLiteral("x"), NoteType::Personal, {}, {}, {{QStringLiteral("carte"), 1}}).ok());

        // Source supprimée : signalée, la note reste.
        QVERIFY(annotations.remove(second.id).ok());
        QVERIFY(notes.sources(id).value().at(1).missing);

        // Notes tirées d'un document (export d'une sélection)
        QCOMPARE(notes.derivedFrom({doc.id}).value().size(), 1);
        QVERIFY(notes.derivedFrom({doc.id + 100}).value().isEmpty());

        // [[Thermodynamique]] désigne le document (aucune note de ce titre).
        const auto links = notes.links(id).value();
        QCOMPARE(links.size(), 1);
        QCOMPARE(links.first().documentId, doc.id);
        QCOMPARE(links.first().noteId, 0);
        QVERIFY(links.first().resolved());
        QCOMPARE(notes.resolveTarget(QStringLiteral("thermodynamique")).kind, NoteLinkTarget::Kind::Document);
        // Une note du même titre devient prioritaire.
        const qint64 homonym = notes.create(QStringLiteral("Thermodynamique"), NoteType::Course).value().id;
        QCOMPARE(notes.links(id).value().first().noteId, homonym);
        QVERIFY(notes.linkCompletions().contains(QStringLiteral("Lecture · Thermodynamique")));
        QCOMPARE(notes.linkCompletions().count(QStringLiteral("Thermodynamique")), 1);
    }

    void fullTextSearchSeesNotes()
    {
        NoteService notes = service();
        const qint64 id = notes.create(QStringLiteral("Entropie"), NoteType::Concept).value().id;
        QVERIFY(notes.save(id, QStringLiteral("Entropie"), QStringLiteral("Fonction d'état ; l'énergie se dégrade.")).ok());
        QCOMPARE(m_db->queryInt(QStringLiteral("SELECT count(*) FROM fts_notes WHERE fts_notes MATCH 'energie'")).value(), 1);
    }

    void revisionSheetDraft()
    {
        DocumentRecord doc;
        doc.id = 7;
        doc.uuid = QStringLiteral("d-uuid");
        doc.title = QStringLiteral("Thermodynamique");
        auto annotation = [&](int page, const QString& text, const QString& note) {
            AnnotationRecord a;
            a.uuid = QStringLiteral("a%1").arg(page);
            a.documentId = doc.id;
            a.unitIndex = page - 1;
            a.sortKey = page;
            a.selectedText = text;
            a.note = note;
            return a;
        };
        NoteDrafts::SheetMaterial m;
        m.subject = QStringLiteral("Physique › Thermodynamique");
        m.date = QDate(2026, 11, 20);
        NoteDrafts::SheetDocument sd;
        sd.doc = doc;
        sd.annotations = {annotation(12, QStringLiteral("Le rendement de Carnot\nne dépend que des températures."),
                                     QStringLiteral("Retenir : $$\\eta = 1 - \\frac{T_F}{T_C}$$")),
                          annotation(3, QStringLiteral("Premier principe."), QString())};
        AnnotationRecord bookmark = annotation(5, QString(), QString());
        bookmark.type = AnnotationType::Bookmark;
        sd.annotations << bookmark;
        CommentRecord q;
        q.uuid = QStringLiteral("c1");
        q.kind = CommentKind::Question;
        q.body = QStringLiteral("Pourquoi des températures absolues ?");
        CommentRecord done = q;
        done.uuid = QStringLiteral("c2");
        done.resolved = true;
        sd.comments = {q, done};
        m.documents << sd;
        NoteRecord conceptNote;
        conceptNote.title = QStringLiteral("Entropie");
        conceptNote.type = NoteType::Concept;
        conceptNote.body = QStringLiteral("---\ntags: [concept]\n---\n\n# Entropie\n\nGrandeur d'état qui mesure le désordre. "
                                      "Elle croît dans un système isolé.\n");
        NoteRecord course;
        course.title = QStringLiteral("Cours 3");
        course.type = NoteType::Course;
        course.body = QStringLiteral("Bilan : $$dS = \\frac{\\delta Q}{T}$$");
        NoteRecord oldSheet;
        oldSheet.title = QStringLiteral("Fiche de révision · Physique › Thermodynamique");
        oldSheet.type = NoteType::Revision;
        m.notes = {conceptNote, course, oldSheet};
        m.cardQuestions = {QStringLiteral("Unité de l'entropie ?")};

        const NoteDraft d = NoteDrafts::revisionSheet(m);
        QCOMPARE(d.type, NoteType::Revision);
        QCOMPARE(d.folder, QStringLiteral("Fiches de révision"));
        QCOMPARE(d.title, QStringLiteral("Fiche de révision · Physique › Thermodynamique"));
        QVERIFY(NoteDrafts::isDraftSheet(d.body));
        const FrontMatter fm = parseFrontMatter(d.body);
        QCOMPARE(fm.values.value(QStringLiteral("matière")).toString(), QStringLiteral("Physique › Thermodynamique"));
        QCOMPARE(fm.values.value(QStringLiteral("généré")).toString(), QStringLiteral("2026-11-20"));
        QVERIFY(noteTags(d.body).contains(QStringLiteral("brouillon")));
        QVERIFY2(d.body.contains(QStringLiteral("depuis 1 document, 2 passages annotés, 2 notes de « Physique")), qPrintable(d.body));
        // Passages : ordre de lecture, citation sur une ligne, lien vers le passage, commentaire en retrait.
        const qsizetype p3 = d.body.indexOf(QStringLiteral("- « Premier principe. » ([p. 3](studylab://open?doc=d-uuid&annotation=a3))"));
        const qsizetype p12 = d.body.indexOf(QStringLiteral("- « Le rendement de Carnot ne dépend que des températures. » ([p. 12]"));
        QVERIFY2(p3 > 0 && p12 > p3, qPrintable(d.body));
        QVERIFY(d.body.contains(QStringLiteral("\n  Retenir : $$")));
        QVERIFY(d.body.contains(QStringLiteral("### [[Thermodynamique]]")));
        // Définitions, formules (notes et commentaires), questions, autres notes.
        QVERIFY(d.body.contains(QStringLiteral("- **[[Entropie]]** : Grandeur d'état qui mesure le désordre.")));
        QVERIFY(d.body.contains(QStringLiteral("$$\ndS = \\frac{\\delta Q}{T}\n$$\n*[[Cours 3]]*")));
        QVERIFY(d.body.contains(QStringLiteral("$$\n\\eta = 1 - \\frac{T_F}{T_C}\n$$\n*[Thermodynamique, p. 12]")));
        QVERIFY(d.body.contains(QStringLiteral("- [ ] Pourquoi des températures absolues ? (Thermodynamique, [")));
        QVERIFY(!d.body.contains(QStringLiteral("&comment=c2")));
        QVERIFY(d.body.contains(QStringLiteral("- [ ] Unité de l'entropie ?")));
        QVERIFY(d.body.contains(QStringLiteral("## Notes de la matière\n\n- [[Cours 3]] · Fiche de cours")));
        QVERIFY(!d.body.contains(QStringLiteral("[[Fiche de révision")));

        // Rubriques décochées.
        NoteDrafts::SheetOptions o;
        o.passages = false;
        o.formulas = false;
        const NoteDraft lean = NoteDrafts::revisionSheet(m, o);
        QVERIFY(!lean.body.contains(QStringLiteral("## Passages clés")));
        QVERIFY(!lean.body.contains(QStringLiteral("## Formules")));
        QVERIFY(lean.body.contains(QStringLiteral("## Définitions")));

        // Trop de passages : les commentés d'abord.
        NoteDrafts::SheetOptions few;
        few.maxPassagesPerDocument = 1;
        const NoteDraft one = NoteDrafts::revisionSheet(m, few);
        QVERIFY(one.body.contains(QStringLiteral("[p. 12]")));
        QVERIFY(!one.body.contains(QStringLiteral("[p. 3]")));

        // Relue : statut et tag retirés, avertissement supprimé, le reste intact.
        const QString reviewed = NoteDrafts::markSheetReviewed(d.body);
        QVERIFY(!NoteDrafts::isDraftSheet(reviewed));
        QCOMPARE(noteTags(reviewed), QStringList{QStringLiteral("révision")});
        QVERIFY(!reviewed.contains(QStringLiteral("**Brouillon**")));
        QVERIFY(reviewed.contains(QStringLiteral("## Définitions")));
        QCOMPARE(parseFrontMatter(reviewed).values.value(QStringLiteral("matière")).toString(),
                 QStringLiteral("Physique › Thermodynamique"));
        QCOMPARE(NoteDrafts::markSheetReviewed(reviewed), reviewed);
    }
};

QTEST_GUILESS_MAIN(TstNotes)
#include "tst_notes.moc"
