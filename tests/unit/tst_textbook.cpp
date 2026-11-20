#include "engines/epub/EpubPackage.h"
#include "engines/epub/EpubTextExtractor.h"
#include "engines/epub/TextBook.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

using namespace studylab;

namespace {

QString data(const QString& name) { return QStringLiteral(STUDYLAB_TEST_DATA "/text/") + name; }

bool wellFormed(const QByteArray& xml, QString* error = nullptr)
{
    QXmlStreamReader r(xml);
    while (!r.atEnd())
        r.readNext();
    if (r.hasError() && error)
        *error = QStringLiteral("%1 (ligne %2)").arg(r.errorString()).arg(r.lineNumber());
    return !r.hasError();
}

}  // namespace

/// Lecture des fichiers TXT et Markdown : mise en forme de livre EPUB en mémoire (V1-6c).
class TstTextBook : public QObject {
    Q_OBJECT

private slots:
    void detection()
    {
        QVERIFY(TextBook::isTextFile(QStringLiteral("a.TXT")));
        QVERIFY(TextBook::isTextFile(QStringLiteral("a.md")));
        QVERIFY(TextBook::isTextFile(QStringLiteral("dossier/a.markdown")));
        QVERIFY(TextBook::isMarkdownFile(QStringLiteral("a.Md")));
        QVERIFY(!TextBook::isMarkdownFile(QStringLiteral("a.txt")));
        QVERIFY(!TextBook::isTextFile(QStringLiteral("a.epub")));
    }

    void markdownBlocks()
    {
        const QString html = TextBook::markdownToXhtml(QStringLiteral(
            "# Titre *en* italique\n\nTexte **gras**, *italique*, _aussi_, ~~barré~~, ==surligné==, `a < b` et \\*pas\\*.\n"
            "snake_case_reste et 2 * 3 * 4.\n\n"
            "Titre souligné\n===\n\n"
            "- un\n- deux\n  - deux bis\n- [x] fait\n\n"
            "3. trois\n4. quatre\n\n"
            "> cité\n\n"
            "***\n\n"
            "[lien](https://exemple.fr \"titre\") <https://a.fr> [[Note]] [[Note|alias]] ![img](x.png)\n"));
        QVERIFY2(html.contains(QStringLiteral("<h1 id=\"t1\">Titre <em>en</em> italique</h1>")), qPrintable(html));
        QVERIFY(html.contains(QStringLiteral("<strong>gras</strong>")));
        QVERIFY(html.contains(QStringLiteral("<em>italique</em>")));
        QVERIFY(html.contains(QStringLiteral("<em>aussi</em>")));
        QVERIFY(html.contains(QStringLiteral("<del>barré</del>")));
        QVERIFY(html.contains(QStringLiteral("<mark>surligné</mark>")));
        QVERIFY(html.contains(QStringLiteral("<code>a &lt; b</code>")));
        QVERIFY(html.contains(QStringLiteral("et *pas*.")));
        QVERIFY(html.contains(QStringLiteral("snake_case_reste et 2 * 3 * 4.")));
        QVERIFY(html.contains(QStringLiteral("<h1 id=\"t2\">Titre souligné</h1>")));
        QVERIFY2(html.contains(QStringLiteral("<ul><li>un</li><li>deux<ul><li>deux bis</li></ul></li><li>☑ fait</li></ul>")),
                 qPrintable(html));
        QVERIFY(html.contains(QStringLiteral("<ol start=\"3\"><li>trois</li><li>quatre</li></ol>")));
        QVERIFY(html.contains(QStringLiteral("<blockquote><p>cité</p></blockquote>")));
        QVERIFY(html.contains(QStringLiteral("<hr/>")));
        QVERIFY(html.contains(QStringLiteral("<a href=\"https://exemple.fr\">lien</a>")));
        QVERIFY(html.contains(QStringLiteral("<a href=\"https://a.fr\">https://a.fr</a>")));
        QVERIFY(html.contains(QStringLiteral("<span class=\"wikilink\">Note</span>")));
        QVERIFY(html.contains(QStringLiteral("<span class=\"wikilink\">alias</span>")));
        QVERIFY(html.contains(QStringLiteral("<span class=\"image\">[img]</span>")));

        QString error;
        QVERIFY2(wellFormed(QStringLiteral("<body>%1</body>").arg(html).toUtf8(), &error), qPrintable(error + html));
    }

    void markdownBook()
    {
        auto pkg = EpubPackage::open(data(QStringLiteral("cours.md")));
        QVERIFY2(pkg.ok(), pkg.ok() ? "" : qPrintable(pkg.error().message));
        EpubPackage& p = *pkg.value();
        const DocumentMetadata md = p.metadata();
        QCOMPARE(md.title, QStringLiteral("Thermodynamique — cours"));
        QCOMPARE(md.authors, (QStringList{QStringLiteral("Yunus Çengel"), QStringLiteral("Michael Boles")}));
        QCOMPARE(md.language, QStringLiteral("fr"));
        // Découpage aux « ## » (niveau le plus haut qui se répète) : introduction + 2 sections.
        QCOMPARE(p.spine().size(), 3);
        QCOMPARE(md.pageCount, 3);
        const QList<EpubNavPoint> toc = p.toc();
        QCOMPARE(toc.size(), 4);
        QCOMPARE(toc.at(0).label, QStringLiteral("Thermodynamique"));
        QCOMPARE(toc.at(1).label, QStringLiteral("Premier principe"));
        QCOMPARE(toc.at(1).section, 1);
        QCOMPARE(toc.at(1).depth, 1);
        QCOMPARE(toc.at(3).label, QStringLiteral("Détail"));
        QCOMPARE(toc.at(3).section, 2);
        QCOMPARE(toc.at(3).depth, 2);

        for (const EpubManifestItem& item : p.manifest()) {
            if (item.mediaType != QLatin1String("application/xhtml+xml"))
                continue;
            QString error;
            QVERIFY2(wellFormed(p.fileData(item.href), &error), qPrintable(item.href + u' ' + error));
        }
        const QString s2 = QString::fromUtf8(p.fileData(p.spine().at(1).href));
        QVERIFY(s2.contains(QStringLiteral("<blockquote><p>L'énergie se conserve.\nElle change de forme.</p></blockquote>")));
        QVERIFY(s2.contains(QStringLiteral("<img src=\"images/i1.png\" alt=\"Schéma du cycle\"/>")));
        QVERIFY(s2.contains(QStringLiteral("<span class=\"image\">[Absente]</span>")));
        QVERIFY(s2.contains(QStringLiteral("<span class=\"link\">une note</span>")));
        QVERIFY(s2.contains(QStringLiteral("<li>☐ À revoir</li>")));
        QVERIFY(!p.fileData(QStringLiteral("OEBPS/images/i1.png")).isEmpty());
        const QString s3 = QString::fromUtf8(p.fileData(p.spine().at(2).href));
        QVERIFY(s3.contains(QStringLiteral("<th style=\"text-align:center\">Unité</th>")));
        QVERIFY2(s3.contains(QStringLiteral("<td style=\"text-align:left\">Énergie | travail</td>")), qPrintable(s3));
        QVERIFY(s3.contains(QStringLiteral("# &lt;Carnot&gt;")));
        QVERIFY(s3.contains(QStringLiteral("<pre class=\"math\">")));
        QVERIFY(s3.contains(QStringLiteral("Fin du chapitre &amp; conclusion &lt; 1 &gt; 0.")));
        const QString s1 = QString::fromUtf8(p.fileData(p.spine().at(0).href));
        QVERIFY(s1.contains(QStringLiteral("Une ligne qui continue\nle même paragraphe.<br/>\nAprès un saut forcé.")));
        QVERIFY(!s1.contains(QStringLiteral("title:")));  // front matter retiré

        // Même fichier, même livre : les CFI restent valables d'une ouverture à l'autre.
        QCOMPARE(TextBook::build(data(QStringLiteral("cours.md"))).value().size(),
                 TextBook::build(data(QStringLiteral("cours.md"))).value().size());
        auto again = EpubPackage::open(data(QStringLiteral("cours.md")));
        QCOMPARE(again.value()->fileData(p.spine().at(2).href), p.fileData(p.spine().at(2).href));

        // Texte extrait : une entrée par section, comme le lecteur.
        auto text = EpubTextExtractor::extract(data(QStringLiteral("cours.md")));
        QVERIFY(text.ok());
        QCOMPARE(text.value().size(), 3);
        QVERIFY(text.value().at(2).contains(QStringLiteral("Sous-section")));
        QVERIFY(text.value().at(1).contains(QStringLiteral("régime permanent")));
    }

    void plainTextBook()
    {
        // Windows-1252 et fins de ligne CRLF
        auto read = TextBook::readText(data(QStringLiteral("notes-cp1252.txt")));
        QVERIFY(read.ok());
        QVERIFY(read.value().contains(QStringLiteral("deuxième ligne – tiret long, « guillemets ».")));
        QVERIFY(!read.value().contains(u'\r'));

        auto pkg = EpubPackage::open(data(QStringLiteral("notes-cp1252.txt")));
        QVERIFY(pkg.ok());
        QCOMPARE(pkg.value()->metadata().title, QStringLiteral("notes cp1252"));  // titre tiré du nom de fichier
        QCOMPARE(pkg.value()->spine().size(), 1);
        const QString s1 = QString::fromUtf8(pkg.value()->fileData(pkg.value()->spine().first().href));
        QVERIFY2(s1.contains(QStringLiteral("<p>Première ligne<br/>\ndeuxième ligne")), qPrintable(s1));
        QVERIFY(s1.contains(QStringLiteral("<p>Dernier paragraphe : café.</p>")));

        // Long texte : découpé en parties, caractères de contrôle retirés.
        QTemporaryDir dir;
        const QString big = dir.filePath(QStringLiteral("long.txt"));
        QFile f(big);
        QVERIFY(f.open(QIODevice::WriteOnly));
        for (int i = 0; i < 2000; ++i)
            f.write(QStringLiteral("Paragraphe %1 : du texte pour remplir la page, encore et encore.\x0c\n\n").arg(i).toUtf8());
        f.close();
        auto long_ = EpubPackage::open(big);
        QVERIFY(long_.ok());
        QVERIFY(long_.value()->spine().size() >= 3);
        QCOMPARE(long_.value()->toc().size(), long_.value()->spine().size());
        QCOMPARE(long_.value()->toc().at(1).label, QStringLiteral("Partie 2"));
        for (const EpubManifestItem& item : long_.value()->spine())
            QVERIFY(wellFormed(long_.value()->fileData(item.href)));

        // Fichier vide : un livre d'une section vide, pas d'erreur.
        const QString empty = dir.filePath(QStringLiteral("vide.md"));
        QFile e(empty);
        QVERIFY(e.open(QIODevice::WriteOnly));
        e.close();
        auto emptyPkg = EpubPackage::open(empty);
        QVERIFY(emptyPkg.ok());
        QCOMPARE(emptyPkg.value()->spine().size(), 1);
        QVERIFY(!EpubPackage::open(dir.filePath(QStringLiteral("absent.md"))).ok());
    }
};

QTEST_GUILESS_MAIN(TstTextBook)
#include "tst_textbook.moc"
