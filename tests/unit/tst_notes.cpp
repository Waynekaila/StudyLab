#include "core/domain/Note.h"
#include "data/migrations/Migrator.h"
#include "data/sqlite/Database.h"
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

    void fullTextSearchSeesNotes()
    {
        NoteService notes = service();
        const qint64 id = notes.create(QStringLiteral("Entropie"), NoteType::Concept).value().id;
        QVERIFY(notes.save(id, QStringLiteral("Entropie"), QStringLiteral("Fonction d'état ; l'énergie se dégrade.")).ok());
        QCOMPARE(m_db->queryInt(QStringLiteral("SELECT count(*) FROM fts_notes WHERE fts_notes MATCH 'energie'")).value(), 1);
    }
};

QTEST_GUILESS_MAIN(TstNotes)
#include "tst_notes.moc"
