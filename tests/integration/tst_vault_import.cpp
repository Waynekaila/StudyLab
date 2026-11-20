#include "core/domain/Note.h"
#include "data/migrations/Migrator.h"
#include "data/sqlite/Database.h"
#include "services/export/LibraryExporter.h"
#include "services/notes/NoteService.h"
#include "services/notes/VaultImporter.h"
#include "services/references/ReferenceService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

namespace {

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

// PNG 1×1 valide.
QByteArray pngBytes()
{
    return QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");
}

const NoteImport* byTitle(const QList<NoteImport>& notes, const QString& title)
{
    for (const NoteImport& n : notes)
        if (n.title == title)
            return &n;
    return nullptr;
}

QString withoutImageFiles(QString body)
{
    static const QRegularExpression img(QStringLiteral("attachments/[\\w.-]+"));
    return body.replace(img, QStringLiteral("attachments/x"));
}

}  // namespace

/// Import d'un coffre Obsidian, d'un dossier Markdown ou d'un export StudyLab (V1-6b, R14).
class TstVaultImport : public QObject {
    Q_OBJECT

    std::unique_ptr<Database> m_db;
    std::unique_ptr<QTemporaryDir> m_dir;

    QString vault() const { return m_dir->filePath(QStringLiteral("Coffre")); }

    /// Coffre type : notes à la racine et dans un sous-dossier, images à côté ou ailleurs, dossiers cachés.
    void makeVault()
    {
        const QString v = vault();
        writeFile(v + QStringLiteral("/.obsidian/app.json"), "{}");
        writeFile(v + QStringLiteral("/.trash/Jetée.md"), "# jetée");
        writeFile(v + QStringLiteral("/assets/schema.png"), pngBytes());
        writeFile(v + QStringLiteral("/img/photo.jpg"), pngBytes());
        writeFile(v + QStringLiteral("/Accueil.md"),
                  QStringLiteral("# Accueil\n\nVoir [[Entropie]] et ![[Entropie]].\n\n![[schema.png|300]]\n\n"
                                 "![Photo du banc](img/photo.jpg)\n\n![[absent.png]]\n\n![web](https://exemple.fr/a.png)\n")
                      .toUtf8());
        writeFile(v + QStringLiteral("/Cours/Entropie.md"),
                  "\xEF\xBB\xBF---\r\ntype: concept\r\ntags: [thermo]\r\n---\r\n\r\n# Entropie\r\n\r\nS = Q/T\r\n");
        writeFile(v + QStringLiteral("/Cours/Libellé.md"),
                  QStringLiteral("---\ntype: Fiche de cours\n---\nTexte\n").toUtf8());
        writeFile(v + QStringLiteral("/biblio.bib"), "@book{cengel2019, title = {Thermodynamique}, year = {2019}}\n");
        writeFile(v + QStringLiteral("/notes.txt"), "pas une note");
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_db = std::move(Database::openInMemory().value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*m_db).ok());
    }

    void scanVault()
    {
        makeVault();
        auto scan = VaultImporter::scan(vault(), QStringLiteral("Coffre"));
        QVERIFY(scan.ok());
        const VaultScan& s = scan.value();
        QVERIFY(!s.studylabExport);
        QCOMPARE(s.notes.size(), 3);  // .trash et .obsidian ignorés
        QCOMPARE(s.bibFiles.size(), 1);

        const NoteImport* home = byTitle(s.notes, QStringLiteral("Accueil"));
        QVERIFY(home);
        QCOMPARE(home->folder, QStringLiteral("Coffre"));
        QCOMPARE(home->images.size(), 2);
        QVERIFY(home->images.at(0).second.endsWith(QStringLiteral("assets/schema.png")));
        QVERIFY(home->images.at(1).second.endsWith(QStringLiteral("img/photo.jpg")));
        QVERIFY(home->body.contains(QStringLiteral("![schema](") + VaultImporter::imageToken(0) + u')'));
        QVERIFY(home->body.contains(QStringLiteral("![Photo du banc](") + VaultImporter::imageToken(1) + u')'));
        QVERIFY(home->body.contains(QStringLiteral("![[Entropie]]")));    // note intégrée : laissée
        QVERIFY(home->body.contains(QStringLiteral("![[absent.png]]")));  // image manquante : laissée
        QVERIFY(home->body.contains(QStringLiteral("https://exemple.fr/a.png")));
        QCOMPARE(s.missingImages, 1);
        QCOMPARE(s.warnings.size(), 1);
        QVERIFY(s.warnings.first().contains(QStringLiteral("absent.png")));

        const NoteImport* entropy = byTitle(s.notes, QStringLiteral("Entropie"));
        QVERIFY(entropy);
        QCOMPARE(entropy->folder, QStringLiteral("Coffre/Cours"));
        QCOMPARE(entropy->type, NoteType::Concept);
        // BOM et CRLF retirés ; « type: » rendu dans le type, les autres clés restent.
        QCOMPARE(entropy->body, QStringLiteral("---\ntags: [thermo]\n---\n\n# Entropie\n\nS = Q/T\n"));

        const NoteImport* label = byTitle(s.notes, QStringLiteral("Libellé"));
        QVERIFY(label);
        QCOMPARE(label->type, NoteType::Course);
        QVERIFY(label->body.contains(QStringLiteral("type: Fiche de cours")));  // libellé : conservé

        QVERIFY(!VaultImporter::scan(m_dir->filePath(QStringLiteral("absent"))).ok());
    }

    void importVault()
    {
        makeVault();
        NoteService notes(*m_db, m_dir->filePath(QStringLiteral("attachments")));
        QSignalSpy added(&notes, &NoteService::noteAdded);
        QSignalSpy links(&notes, &NoteService::linksChanged);
        auto scan = VaultImporter::scan(vault(), QStringLiteral("Coffre"));
        QVERIFY(scan.ok());
        auto report = notes.importNotes(scan.value().notes);
        QVERIFY2(report.ok(), report.ok() ? "" : qPrintable(report.error().message));
        const NoteImportReport& r = report.value();
        QCOMPARE(r.imported, 3);
        QCOMPARE(r.duplicates, 0);
        QCOMPARE(r.images, 2);
        QCOMPARE(r.noteIds.size(), 3);
        QCOMPARE(added.size(), 3);
        QCOMPARE(links.size(), 1);

        auto home = notes.find(notes.resolve(QStringLiteral("Accueil")).value());
        QVERIFY(home.ok());
        QCOMPARE(home.value().folder, QStringLiteral("Coffre"));
        QVERIFY(!home.value().body.contains(QChar(0x2063)));
        static const QRegularExpression img(QStringLiteral("!\\[schema\\]\\((attachments/[\\w-]+\\.png)\\)"));
        const auto m = img.match(home.value().body);
        QVERIFY(m.hasMatch());
        QVERIFY(QFileInfo::exists(m_dir->filePath(m.captured(1))));
        // [[Entropie]] résolu vers la note importée, même créée après Accueil.
        auto out = notes.links(home.value().id);
        QVERIFY(out.ok());
        QVERIFY(!out.value().isEmpty());
        for (const NoteLink& l : out.value())  // seule l'image manquante reste un lien non résolu
            QVERIFY2(l.resolved() == (l.target != QLatin1String("absent.png")), qPrintable(l.target));
        const qint64 entropy = notes.resolve(QStringLiteral("Entropie")).value();
        QVERIFY(entropy > 0);
        QCOMPARE(notes.backlinks(entropy).value().size(), 1);

        // Réimporter le même coffre : rien de neuf, images comprises.
        auto again = notes.importNotes(VaultImporter::scan(vault(), QStringLiteral("Coffre")).value().notes);
        QVERIFY(again.ok());
        QCOMPARE(again.value().imported, 0);
        QCOMPARE(again.value().duplicates, 3);
        QCOMPARE(notes.count().value(), 3);

        // Même titre, autre texte : importée sous un titre libre.
        NoteImport other;
        other.title = QStringLiteral("Accueil");
        other.body = QStringLiteral("Autre accueil");
        auto renamed = notes.importNotes({other});
        QVERIFY(renamed.ok());
        QCOMPARE(renamed.value().imported, 1);
        QCOMPARE(renamed.value().renamed, 1);
        QCOMPARE(notes.find(renamed.value().noteIds.first()).value().title, QStringLiteral("Accueil 2"));
    }

    void bibFilesImportAsReferences()
    {
        makeVault();
        NoteService notes(*m_db, m_dir->filePath(QStringLiteral("attachments")));
        ReferenceService refs(*m_db, notes);
        auto scan = VaultImporter::scan(vault());
        QVERIFY(scan.ok());
        auto r = refs.importFile(scan.value().bibFiles.first());
        QVERIFY(r.ok());
        QCOMPARE(r.value().added, 1);
        QVERIFY(refs.byCitekey(QStringLiteral("cengel2019")).ok());
    }

    void uuidOfTrashedNoteIsReplaced()
    {
        NoteService notes(*m_db, m_dir->filePath(QStringLiteral("attachments")));
        auto created = notes.create(QStringLiteral("Carnot"), NoteType::Concept, {}, QStringLiteral("Cycle"));
        QVERIFY(created.ok());
        const QString uuid = created.value().uuid;
        QVERIFY(notes.remove(created.value().id).ok());

        NoteImport in;
        in.title = QStringLiteral("Carnot");
        in.uuid = uuid;
        in.body = QStringLiteral("Cycle");
        auto r = notes.importNotes({in});
        QVERIFY(r.ok());
        QCOMPARE(r.value().imported, 1);
        const NoteRecord n = notes.find(r.value().noteIds.first()).value();
        QVERIFY(n.uuid != uuid);  // uuid de la corbeille : pris

        // uuid d'une note présente : doublon, même sous un autre titre.
        in.uuid = n.uuid;
        in.title = QStringLiteral("Renommée depuis");
        auto dup = notes.importNotes({in});
        QVERIFY(dup.ok());
        QCOMPARE(dup.value().duplicates, 1);
        QCOMPARE(dup.value().imported, 0);
    }

    /// R14 : un export StudyLab réimporté dans une base vide redonne les mêmes notes ; dans la base d'origine, rien.
    void exportRoundTrip()
    {
        const QString attachmentsA = m_dir->filePath(QStringLiteral("a/attachments"));
        NoteService a(*m_db, attachmentsA);
        auto formula = a.create(QStringLiteral("Rendement de Carnot"), NoteType::Formula, QStringLiteral("Thermo/Cours"),
                                QStringLiteral("---\ntags: [thermo]\naliases: [Carnot]\n---\n\n$$\\eta = 1 - T_f/T_c$$\n"));
        QVERIFY(formula.ok());
        auto image = a.addImage(formula.value().id, pngBytes(), QStringLiteral("png"));
        QVERIFY(image.ok());
        QVERIFY(a.save(formula.value().id, formula.value().title,
                       formula.value().body + QStringLiteral("\n![cycle](") + image.value() + QStringLiteral(")\n")).ok());
        auto plain = a.create(QStringLiteral("Révisions"), NoteType::Personal, {},
                              QStringLiteral("Revoir [[Carnot]] et [[Entropie]].\n"));
        QVERIFY(plain.ok());

        // Export comme LibraryExporter : Notes/<dossier>/<nom>.md, images dans _attachments/, README et Bibliothèque/.
        const QString exportDir = m_dir->filePath(QStringLiteral("Export StudyLab"));
        writeFile(exportDir + QStringLiteral("/README.md"), "# Export");
        writeFile(exportDir + QStringLiteral("/Bibliothèque/Livre/Livre.md"), "# Fiche de document");
        const QList<NoteRecord> original = a.list().value();
        for (const NoteRecord& n : original) {
            writeFile(exportDir + u'/' + LibraryExporter::notePath(n), LibraryExporter::noteMarkdown(n).toUtf8());
            for (const QString& rel : LibraryExporter::noteAttachments(n.body)) {
                QFile src(QDir(attachmentsA).filePath(rel.mid(rel.indexOf(u'/') + 1)));
                QVERIFY(src.open(QIODevice::ReadOnly));
                writeFile(exportDir + QStringLiteral("/_attachments/") + rel.mid(rel.indexOf(u'/') + 1), src.readAll());
            }
        }

        auto scan = VaultImporter::scan(exportDir);
        QVERIFY(scan.ok());
        QVERIFY(scan.value().studylabExport);
        QCOMPARE(scan.value().notes.size(), 2);  // la fiche de document n'est pas une note

        // Base d'origine : uuid connus, tout est déjà là.
        auto same = a.importNotes(scan.value().notes);
        QVERIFY(same.ok());
        QCOMPARE(same.value().imported, 0);
        QCOMPARE(same.value().duplicates, 2);

        // Base vide : mêmes titres, dossiers, types, uuid et textes.
        auto other = Database::openInMemory();
        QVERIFY(other.ok());
        std::unique_ptr<Database> dbB = std::move(other.value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*dbB).ok());
        const QString attachmentsB = m_dir->filePath(QStringLiteral("b/attachments"));
        NoteService b(*dbB, attachmentsB);
        auto r = b.importNotes(scan.value().notes);
        QVERIFY(r.ok());
        QCOMPARE(r.value().imported, 2);
        QCOMPARE(r.value().images, 1);
        QCOMPARE(r.value().renamed, 0);
        const QList<NoteRecord> restored = b.list().value();
        QCOMPARE(restored.size(), original.size());
        for (int i = 0; i < original.size(); ++i) {
            const NoteRecord& x = original.at(i);
            const NoteRecord& y = restored.at(i);
            QCOMPARE(y.title, x.title);
            QCOMPARE(y.folder, x.folder);
            QCOMPARE(y.type, x.type);
            QCOMPARE(y.uuid, x.uuid);
            QCOMPARE(withoutImageFiles(y.body), withoutImageFiles(x.body));
            for (const QString& rel : LibraryExporter::noteAttachments(y.body))
                QVERIFY(QFileInfo::exists(QDir(attachmentsB).filePath(rel.mid(rel.indexOf(u'/') + 1))));
        }
        // [[Carnot]] (alias) résolu dans la base restaurée ; [[Entropie]] reste à écrire.
        const qint64 revisions = b.resolve(QStringLiteral("Révisions")).value();
        int resolved = 0;
        const QList<NoteLink> revisionLinks = b.links(revisions).value();
        for (const NoteLink& l : revisionLinks)
            resolved += l.resolved() ? 1 : 0;
        QCOMPARE(resolved, 1);

        auto twice = b.importNotes(VaultImporter::scan(exportDir).value().notes);
        QVERIFY(twice.ok());
        QCOMPARE(twice.value().imported, 0);
        QCOMPARE(twice.value().duplicates, 2);
    }
};

QTEST_GUILESS_MAIN(TstVaultImport)
#include "tst_vault_import.moc"
