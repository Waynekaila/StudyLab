#include "core/domain/Reference.h"
#include "data/migrations/Migrator.h"
#include "data/repositories/DocumentRepository.h"
#include "data/sqlite/Database.h"
#include "services/notes/NoteService.h"
#include "services/references/ReferenceService.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

namespace {

QString readData(const QString& name)
{
    QFile f(QStringLiteral(STUDYLAB_TEST_DATA "/references/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("donnée de test absente");
    return QString::fromUtf8(f.readAll());
}

const QString kBib = readData(QStringLiteral("biblio.bib"));

}  // namespace

/// Références bibliographiques (V1-6) : modèle CSL-JSON, BibTeX, clés, mise en forme, service.
class TstReferences : public QObject {
    Q_OBJECT

    std::unique_ptr<Database> m_db;
    QTemporaryDir m_tmp;

private slots:
    void init()
    {
        m_db = std::move(Database::openInMemory().value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*m_db).ok());
    }

    void names()
    {
        PersonName n = parsePersonName(QStringLiteral("Yunus A. Çengel"));
        QCOMPARE(n.family, QStringLiteral("Çengel"));
        QCOMPARE(n.given, QStringLiteral("Yunus A."));
        QCOMPARE(n.initialsName(), QStringLiteral("Çengel, Y. A."));
        n = parsePersonName(QStringLiteral("de La Fontaine, Jean"));
        QCOMPARE(n.family, QStringLiteral("de La Fontaine"));
        n = parsePersonName(QStringLiteral("Jean de La Fontaine"));
        QCOMPARE(n.family, QStringLiteral("de La Fontaine"));
        QCOMPARE(n.given, QStringLiteral("Jean"));
        QCOMPARE(parsePersonName(QStringLiteral("Jean-Paul Sartre")).initialsName(), QStringLiteral("Sartre, J.-P."));
        n = parsePersonName(QStringLiteral("{World Health Organization}"));
        QCOMPARE(n.literal, QStringLiteral("World Health Organization"));
        QCOMPARE(n.shortName(), QStringLiteral("World Health Organization"));
        QCOMPARE(parsePersonName(QStringLiteral("Voltaire")).family, QStringLiteral("Voltaire"));
    }

    void latex()
    {
        QCOMPARE(latexToUnicode(QStringLiteral("R{\\'e}flexions")), QStringLiteral("Réflexions"));
        QCOMPARE(latexToUnicode(QStringLiteral("\\'{E}cole \\`a \\^ile G\\\"odel")), QStringLiteral("École à île Gödel"));
        QCOMPARE(latexToUnicode(QStringLiteral("\\c{C}engel \\c c")), QStringLiteral("Çengel ç"));
        QCOMPARE(latexToUnicode(QStringLiteral("Stra\\ss e, \\oe{}uvre, p.~3, 1--2, a---b")),
                 QStringLiteral("Straße, œuvre, p. 3, 1–2, a—b"));
        QCOMPARE(latexToUnicode(QStringLiteral("{DNA} \\& {RNA} 50\\%")), QStringLiteral("DNA & RNA 50%"));
    }

    void bibtexParse()
    {
        const BibtexParse p = parseBibtex(kBib);
        QCOMPARE(p.references.size(), 3);
        QCOMPARE(p.warnings.size(), 2);  // sans clé ; mal formée
        QVERIFY2(p.warnings.at(0).contains(QStringLiteral("sans clé")), qPrintable(p.warnings.join(u'\n')));

        const Reference& book = p.references.at(0);
        QCOMPARE(book.citekey, QStringLiteral("cengel2019"));
        QCOMPARE(book.type, QStringLiteral("book"));
        QCOMPARE(book.authors.size(), 2);
        QCOMPARE(book.authors.at(0).family, QStringLiteral("Çengel"));
        QCOMPARE(book.authors.at(1).given, QStringLiteral("Michael A."));
        QCOMPARE(book.publisher, QStringLiteral("McGraw-Hill Education"));
        QCOMPARE(book.place, QStringLiteral("New York"));
        QCOMPARE(book.year, 2019);
        QCOMPARE(book.edition, QStringLiteral("9"));
        QCOMPARE(book.extra.value(QStringLiteral("keyword")).toString(), QStringLiteral("thermo, énergie"));

        const Reference& art = p.references.at(1);
        QCOMPARE(art.type, QStringLiteral("article-journal"));
        QCOMPARE(art.title, QStringLiteral("Réflexions sur la puissance motrice du feu"));
        QCOMPARE(art.containerTitle, QStringLiteral("Annales scientifiques de l'École normale supérieure"));
        QCOMPARE(art.authors.first().family, QStringLiteral("Carnot"));
        QCOMPARE(art.issue, QStringLiteral("2"));
        QCOMPARE(art.pages, QStringLiteral("393–457"));
        QCOMPARE(art.month, 3);
        QCOMPARE(art.doi, QStringLiteral("10.24033/asens.88"));

        const Reference& ch = p.references.at(2);
        QCOMPARE(ch.type, QStringLiteral("chapter"));
        QCOMPARE(ch.authors.size(), 3);
        QCOMPARE(ch.authors.at(2).family, QStringLiteral("de La Fontaine"));
        QCOMPARE(ch.editors.first().literal, QStringLiteral("Société française de physique"));
        QCOMPARE(ch.containerTitle, QStringLiteral("Métamorphose de la science"));
    }

    void bibtexRoundTrip()
    {
        const QList<Reference> refs = parseBibtex(kBib).references;
        const QString out = writeBibtex(refs);
        QVERIFY(out.contains(QStringLiteral("@book{cengel2019,")));
        QVERIFY(out.contains(QStringLiteral("author = {Çengel, Yunus A. and Boles, Michael A.}")));
        QVERIFY(out.contains(QStringLiteral("pages = {393--457}")));
        QVERIFY(out.contains(QStringLiteral("@incollection{prigogine,")));
        QVERIFY(out.contains(QStringLiteral("editor = {{Société française de physique}}")));
        const QList<Reference> again = parseBibtex(out).references;
        QCOMPARE(again.size(), refs.size());
        for (int i = 0; i < refs.size(); ++i) {
            QCOMPARE(again.at(i).title, refs.at(i).title);
            QCOMPARE(again.at(i).authors, refs.at(i).authors);
            QCOMPARE(again.at(i).editors, refs.at(i).editors);
            QCOMPARE(again.at(i).year, refs.at(i).year);
            QCOMPARE(again.at(i).pages, refs.at(i).pages);
            QCOMPARE(again.at(i).containerTitle, refs.at(i).containerTitle);
        }
        // Caractères spéciaux échappés
        Reference r;
        r.citekey = QStringLiteral("x");
        r.title = QStringLiteral("R&D à 50 % {sic}");
        QVERIFY(writeBibtex({r}).contains(QStringLiteral("title = {R\\&D à 50 \\% sic}")));
    }

    void cslJson()
    {
        Reference r = parseBibtex(kBib).references.at(1);
        r.extra.insert(QStringLiteral("PMID"), QStringLiteral("123"));
        const QJsonObject o = toCslJson(r);
        QCOMPARE(o.value(QStringLiteral("id")).toString(), QStringLiteral("Carnot1824"));
        QCOMPARE(o.value(QStringLiteral("type")).toString(), QStringLiteral("article-journal"));
        QCOMPARE(o.value(QStringLiteral("author")).toArray().first().toObject().value(QStringLiteral("family")).toString(),
                 QStringLiteral("Carnot"));
        QCOMPARE(o.value(QStringLiteral("issued")).toObject().value(QStringLiteral("date-parts")).toArray().first().toArray(),
                 (QJsonArray{1872, 3}));
        QCOMPARE(o.value(QStringLiteral("page")).toString(), QStringLiteral("393–457"));
        QCOMPARE(fromCslJson(o), r);  // aller-retour sans perte, champs inconnus compris

        const QByteArray zotero = readData(QStringLiteral("zotero.json")).toUtf8();
        auto parsed = parseCslJson(zotero);
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.value().size(), 2);
        QCOMPARE(parsed.value().at(0).year, 1970);
        QCOMPARE(parsed.value().at(0).extra.value(QStringLiteral("number-of-pages")).toString(), QStringLiteral("197"));
        QCOMPARE(parsed.value().at(1).year, 2021);
        QCOMPARE(parsed.value().at(1).authors.first().literal, QStringLiteral("CNRS"));
        QVERIFY(!parseCslJson("[{").ok());
    }

    void keysAndFormatting()
    {
        const QList<Reference> refs = parseBibtex(kBib).references;
        QCOMPARE(makeCitekey(refs.at(0), {}), QStringLiteral("cengel2019thermodynamics"));
        QCOMPARE(makeCitekey(refs.at(0), {QStringLiteral("cengel2019thermodynamics")}),
                 QStringLiteral("cengel2019thermodynamicsa"));
        QCOMPARE(makeCitekey(refs.at(2), {}), QStringLiteral("prigogine1979nouvelle"));
        Reference anonymous;
        QCOMPARE(makeCitekey(anonymous, {}), QStringLiteral("ref"));
        QVERIFY(isValidCitekey(QStringLiteral("cengel2019:ch-2.1")));
        QVERIFY(!isValidCitekey(QStringLiteral("deux mots")));
        QVERIFY(!isValidCitekey(QStringLiteral("a]]b")));
        QVERIFY(!isValidCitekey(QString()));

        QCOMPARE(formatCitation(refs.at(0)), QStringLiteral("Çengel et Boles, 2019"));
        QCOMPARE(formatCitation(refs.at(1)), QStringLiteral("Carnot, 1872"));
        QCOMPARE(formatCitation(refs.at(2)), QStringLiteral("Prigogine et al., 1979"));
        Reference undated;
        undated.title = QStringLiteral("Notes de cours de thermodynamique");
        QCOMPARE(formatCitation(undated), QStringLiteral("Notes de cours de…, s. d."));

        QCOMPARE(formatBibliography(refs.at(0)),
                 QStringLiteral("Çengel, Y. A. et Boles, M. A. (2019). Thermodynamics: An Engineering Approach. 9ᵉ éd. "
                                "New York : McGraw-Hill Education."));
        QCOMPARE(formatBibliography(refs.at(1)),
                 QStringLiteral("Carnot, S. (1872). Réflexions sur la puissance motrice du feu. Annales scientifiques "
                                "de l'École normale supérieure, 1(2), 393–457. https://doi.org/10.24033/asens.88"));
        const QString html = formatBibliography(refs.at(1), true);
        QVERIFY2(html.contains(QStringLiteral("<i>Annales scientifiques de l&#39;École normale supérieure</i>, <i>1</i>(2)"))
                     || html.contains(QStringLiteral("<i>Annales scientifiques de l'École normale supérieure</i>, <i>1</i>(2)")),
                 qPrintable(html));
        QVERIFY(formatBibliography(refs.at(2)).contains(QStringLiteral("Dans Société française de physique (dir.), "
                                                                       "Métamorphose de la science (p. 10–20).")));

        QCOMPARE(citedKeys(QStringLiteral("Voir [[@cengel2019]], [[@Carnot1824|Carnot]] et [[@cengel2019#p. 12]]. "
                                          "[[Note]] [[@ ]]")),
                 (QStringList{QStringLiteral("cengel2019"), QStringLiteral("Carnot1824")}));
    }

    void serviceCrudAndCitations()
    {
        NoteService notes(*m_db, m_tmp.filePath(QStringLiteral("att")));
        ReferenceService refs(*m_db, notes);
        Reference r;
        r.title = QStringLiteral("Thermodynamique");
        r.authors = {parsePersonName(QStringLiteral("Yunus A. Çengel"))};
        r.year = 2019;
        auto a = refs.create(r);
        QVERIFY(a.ok());
        QCOMPARE(a.value().citekey, QStringLiteral("cengel2019thermodynamique"));
        auto b = refs.create(r);  // même titre : clé suffixée
        QCOMPARE(b.value().citekey, QStringLiteral("cengel2019thermodynamiquea"));
        r.citekey = QStringLiteral("mauvaise clé");
        QCOMPARE(refs.create(r).error().code, Error::Code::InvalidArgument);
        QCOMPARE(refs.byCitekey(QStringLiteral("CENGEL2019THERMODYNAMIQUE")).value().id, a.value().id);
        QCOMPARE(refs.completions(),
                 (QStringList{QStringLiteral("@cengel2019thermodynamique"), QStringLiteral("@cengel2019thermodynamiquea")}));

        auto note = notes.create(QStringLiteral("Cours"), NoteType::Course, {},
                                 QStringLiteral("Selon [[@cengel2019thermodynamique#p. 12]] et "
                                                "[[@cengel2019thermodynamique|Çengel]], mais pas [[@cengel2019thermodynamiquea]]."));
        QVERIFY(note.ok());
        QCOMPARE(refs.citingNotes(QStringLiteral("cengel2019thermodynamique")).value().size(), 1);
        // Le [[@…]] n'est ni un lien de note ni un lien non résolu.
        QVERIFY(notes.links(note.value().id).value().isEmpty());

        Reference edited = a.value();
        edited.citekey = QStringLiteral("cengel-thermo");
        edited.pages = QStringLiteral("12–30");
        int rewritten = -1;
        auto updated = refs.update(edited, &rewritten);
        QVERIFY(updated.ok());
        QCOMPARE(rewritten, 1);
        QCOMPARE(updated.value().pages, QStringLiteral("12–30"));
        QCOMPARE(notes.find(note.value().id).value().body,
                 QStringLiteral("Selon [[@cengel-thermo#p. 12]] et [[@cengel-thermo|Çengel]], mais pas "
                                "[[@cengel2019thermodynamiquea]]."));
        edited.citekey = QStringLiteral("Cengel2019ThermodynamiqueA");  // prise, sans casse
        QCOMPARE(refs.update(edited).error().code, Error::Code::InvalidArgument);

        const auto resolved = refs.resolveKeys({QStringLiteral("CENGEL-thermo"), QStringLiteral("inconnue")});
        QCOMPARE(resolved.size(), 1);
        QVERIFY(resolved.contains(QStringLiteral("cengel-thermo")));

        QVERIFY(refs.remove(b.value().id).ok());
        QCOMPARE(refs.list().value().size(), 1);
        QCOMPARE(refs.remove(b.value().id).error().code, Error::Code::NotFound);
    }

    void serviceImportExport()
    {
        // Document de la bibliothèque reconnu par son DOI, un autre par son titre.
        DocumentRepository docs(*m_db);
        DocumentRecord carnot;
        carnot.title = QStringLiteral("Réflexions sur la puissance motrice du feu");
        carnot.doi = QStringLiteral("https://doi.org/10.24033/ASENS.88");
        carnot.fileHash = QStringLiteral("h1");
        carnot.filePath = QStringLiteral("c.pdf");
        QVERIFY(docs.insert(carnot).ok());
        DocumentRecord alliance;
        alliance.title = QStringLiteral("La Nouvelle Alliance");
        alliance.fileHash = QStringLiteral("h2");
        alliance.filePath = QStringLiteral("a.pdf");
        alliance.authors = {QStringLiteral("Ilya Prigogine")};
        alliance.year = 1979;
        alliance.publisher = QStringLiteral("Gallimard");
        alliance.isbn = QStringLiteral("2-07-028765-3");
        QVERIFY(docs.insert(alliance).ok());

        NoteService notes(*m_db, m_tmp.filePath(QStringLiteral("att")));
        ReferenceService refs(*m_db, notes);
        // La clé « cengel2019 » est déjà prise par une autre œuvre.
        Reference other;
        other.citekey = QStringLiteral("cengel2019");
        other.title = QStringLiteral("Heat and Mass Transfer");
        QVERIFY(refs.create(other).ok());

        const QString bib = m_tmp.filePath(QStringLiteral("biblio.bib"));
        QFile f(bib);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kBib.toUtf8());
        f.close();
        auto report = refs.importFile(bib);
        QVERIFY(report.ok());
        QCOMPARE(report.value().added, 3);
        QCOMPARE(report.value().linked, 2);
        QCOMPARE(report.value().renamed, 1);
        QCOMPARE(report.value().warnings.size(), 2);
        QCOMPARE(refs.byCitekey(QStringLiteral("Carnot1824")).value().documentId, carnot.id);
        QCOMPARE(refs.byCitekey(QStringLiteral("prigogine")).value().documentId, alliance.id);
        QCOMPARE(refs.byCitekey(QStringLiteral("cengel2019a")).value().publisher, QStringLiteral("McGraw-Hill Education"));
        // Deuxième import : tout est déjà là.
        report = refs.importFile(bib);
        QCOMPARE(report.value().added, 0);
        QCOMPARE(report.value().duplicates, 3);

        // Export BibTeX et CSL-JSON, relus.
        const QString outBib = m_tmp.filePath(QStringLiteral("out.bib"));
        QCOMPARE(refs.exportFile(outBib).value(), 4);
        QFile ob(outBib);
        QVERIFY(ob.open(QIODevice::ReadOnly));
        QCOMPARE(parseBibtex(QString::fromUtf8(ob.readAll())).references.size(), 4);
        const QString outJson = m_tmp.filePath(QStringLiteral("out.json"));
        const qint64 carnotRef = refs.byCitekey(QStringLiteral("Carnot1824")).value().id;
        QCOMPARE(refs.exportFile(outJson, {carnotRef}).value(), 1);
        QFile oj(outJson);
        QVERIFY(oj.open(QIODevice::ReadOnly));
        const QJsonArray arr = QJsonDocument::fromJson(oj.readAll()).array();
        QCOMPARE(arr.size(), 1);
        QCOMPARE(arr.first().toObject().value(QStringLiteral("id")).toString(), QStringLiteral("Carnot1824"));
        // Liste triée par premier auteur.
        const QList<Reference> all = refs.list().value();
        QCOMPARE(all.first().citekey, QStringLiteral("Carnot1824"));

        QCOMPARE(refs.importFile(m_tmp.filePath(QStringLiteral("absent.bib"))).error().code, Error::Code::Io);
        QFile bad(m_tmp.filePath(QStringLiteral("x.ris")));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.close();
        QCOMPARE(refs.importFile(bad.fileName()).error().code, Error::Code::InvalidArgument);

        // Référence d'un document : tirée de ses métadonnées, puis retrouvée.
        DocumentRecord monod;
        monod.title = QStringLiteral("Le Hasard et la Nécessité");
        monod.subtitle = QStringLiteral("Essai sur la philosophie naturelle");
        monod.authors = {QStringLiteral("Jacques Monod")};
        monod.year = 1970;
        monod.publisher = QStringLiteral("Seuil");
        monod.fileHash = QStringLiteral("h3");
        monod.filePath = QStringLiteral("m.epub");
        QVERIFY(docs.insert(monod).ok());
        auto mref = refs.ensureForDocument(monod.id);
        QVERIFY(mref.ok());
        QCOMPARE(mref.value().citekey, QStringLiteral("monod1970hasard"));
        QCOMPARE(mref.value().title, QStringLiteral("Le Hasard et la Nécessité : Essai sur la philosophie naturelle"));
        QCOMPARE(mref.value().authors.first().family, QStringLiteral("Monod"));
        QCOMPARE(refs.ensureForDocument(monod.id).value().id, mref.value().id);
        QCOMPARE(refs.ensureForDocument(alliance.id).value().citekey, QStringLiteral("prigogine"));
    }
};

QTEST_MAIN(TstReferences)
#include "tst_references.moc"
