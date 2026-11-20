#include "engines/epub/DocxBook.h"
#include "engines/epub/EpubPackage.h"
#include "engines/epub/EpubTextExtractor.h"
#include "support/Fixtures.h"

#include <private/qzipwriter_p.h>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

using namespace studylab;

namespace {

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

/// Lecture des documents Word (.docx) : mise en forme de livre EPUB en mémoire (V2-6).
class TstDocxBook : public QObject {
    Q_OBJECT

private slots:
    void detection()
    {
        QVERIFY(DocxBook::isDocxFile(QStringLiteral("rapport.DOCX")));
        QVERIFY(!DocxBook::isDocxFile(QStringLiteral("rapport.doc")));
        QVERIFY(!DocxBook::isDocxFile(QStringLiteral("livre.epub")));
    }

    void book()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("cours.docx"));
        QVERIFY(test::writeDocx(path));

        auto pkg = EpubPackage::open(path);
        QVERIFY2(pkg.ok(), pkg.ok() ? "" : qPrintable(pkg.error().message));
        EpubPackage& p = *pkg.value();
        const DocumentMetadata md = p.metadata();
        QCOMPARE(md.title, QStringLiteral("Physique des fluides"));
        QCOMPARE(md.authors, (QStringList{QStringLiteral("Ada Lovelace"), QStringLiteral("Blaise Pascal")}));
        QCOMPARE(md.language, QStringLiteral("fr"));  // fr-FR ramené à la langue

        // Découpage aux titres de niveau 1 (le plus haut qui se répète) : début + « Statique » + « Dynamique ».
        QCOMPARE(p.spine().size(), 3);
        const QList<EpubNavPoint> toc = p.toc();
        QCOMPARE(toc.size(), 3);
        QCOMPARE(toc.at(0).label, QStringLiteral("Statique"));
        QCOMPARE(toc.at(0).section, 1);
        QCOMPARE(toc.at(1).label, QStringLiteral("Unités"));
        QCOMPARE(toc.at(1).depth, 1);
        QCOMPARE(toc.at(2).label, QStringLiteral("Dynamique"));
        QCOMPARE(toc.at(2).section, 2);

        for (const EpubManifestItem& item : p.manifest()) {
            if (item.mediaType != QLatin1String("application/xhtml+xml"))
                continue;
            QString error;
            QVERIFY2(wellFormed(p.fileData(item.href), &error), qPrintable(item.href + u' ' + error));
        }

        const QString s1 = QString::fromUtf8(p.fileData(p.spine().at(0).href));
        QVERIFY(s1.contains(QStringLiteral("<h1 class=\"doc-title\">Physique des fluides</h1>")));
        QVERIFY2(s1.contains(QStringLiteral("<strong>gras</strong>")), qPrintable(s1));
        QVERIFY(s1.contains(QStringLiteral("<em>italique</em>")));
        QVERIFY(s1.contains(QStringLiteral("<u>souligné</u>")));
        QVERIFY(s1.contains(QStringLiteral("<s>barré</s>")));
        QVERIFY(s1.contains(QStringLiteral("m<sup>2</sup>")));
        QVERIFY(s1.contains(QStringLiteral("H<sub>2</sub>O, pas gras")));  // <w:b w:val="0"/> : pas de gras
        QVERIFY(s1.contains(QStringLiteral("<a href=\"https://fr.wikipedia.org/wiki/Pression\">un lien</a>")));
        QVERIFY(s1.contains(QStringLiteral("&amp; &lt;fin&gt;.")));

        const QString s2 = QString::fromUtf8(p.fileData(p.spine().at(1).href));
        QVERIFY(s2.contains(QStringLiteral("<h1 id=\"t1\">Statique</h1>")));
        QVERIFY2(s2.contains(QStringLiteral("<p>La pression est une force par unité de surface.<sup class=\"noteref\">1</sup> Ajouté.</p>")),
                 qPrintable(s2));
        QVERIFY(!s2.contains(QStringLiteral("supprimé")));  // suivi des modifications : texte accepté
        QVERIFY2(s2.contains(QStringLiteral("<ul><li>Pascal<ol><li>Premier point</li><li>Second point</li></ol></li><li>Archimède</li></ul>")),
                 qPrintable(s2));
        QVERIFY(s2.contains(QStringLiteral("<h2 id=\"t2\">Unités</h2>")));
        QVERIFY2(s2.contains(QStringLiteral("<table><tbody><tr><td>Grandeur</td><td>Pa</td></tr><tr><td></td><td>bar</td></tr>"
                                            "<tr><td colspan=\"2\">Deux colonnes</td></tr></tbody></table>")),
                 qPrintable(s2));

        const QString s3 = QString::fromUtf8(p.fileData(p.spine().at(2).href));
        QVERIFY(s3.contains(QStringLiteral("<blockquote><p>Tout corps plongé dans un fluide…</p></blockquote>")));
        QVERIFY2(s3.contains(QStringLiteral("<img src=\"images/i1.png\" alt=\"Schéma du tube\"/>")), qPrintable(s3));
        QVERIFY(!p.fileData(QStringLiteral("OEBPS/images/i1.png")).isEmpty());
        QVERIFY(s3.contains(QStringLiteral("<ol><li>Étape un</li><li>Étape deux</li></ol>")));
        QVERIFY(s3.contains(QStringLiteral("<p>Bernoulli<br/>et Venturi.</p>")));
        // Notes de bas de page : en fin de livre, sans le séparateur.
        QVERIFY2(s3.contains(QStringLiteral("<li value=\"1\">Blaise Pascal, 1648.</li>")), qPrintable(s3));

        // Même fichier, même livre (les CFI des annotations restent valables).
        QCOMPARE(DocxBook::build(path).value(), DocxBook::build(path).value());

        // Texte extrait pour la recherche : une entrée par section.
        auto text = EpubTextExtractor::extract(path);
        QVERIFY(text.ok());
        QCOMPARE(text.value().size(), 3);
        QVERIFY(text.value().at(1).contains(QStringLiteral("force par unité de surface")));
    }

    void titleFallbacks()
    {
        // Sans propriétés : titre tiré du nom de fichier ; sans titre répété : une seule section.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("mes_notes.docx"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QZipWriter zip(&f);
        zip.addFile(QStringLiteral("word/document.xml"),
                    QByteArrayLiteral("<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
                                      "<w:body><w:p><w:r><w:t>Seul paragraphe.</w:t></w:r></w:p></w:body></w:document>"));
        zip.close();
        f.close();
        auto pkg = EpubPackage::open(path);
        QVERIFY2(pkg.ok(), pkg.ok() ? "" : qPrintable(pkg.error().message));
        QCOMPARE(pkg.value()->metadata().title, QStringLiteral("mes notes"));
        QCOMPARE(pkg.value()->spine().size(), 1);
        QCOMPARE(pkg.value()->toc().size(), 1);
    }

    void refusals()
    {
        QTemporaryDir dir;
        // Fichier qui n'est pas une archive
        const QString notZip = dir.filePath(QStringLiteral("faux.docx"));
        QFile f(notZip);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("pas un document");
        f.close();
        QVERIFY(!DocxBook::build(notZip).ok());

        // Archive sans word/document.xml (un .xlsx renommé, par exemple)
        const QString other = dir.filePath(QStringLiteral("tableur.docx"));
        QFile o(other);
        QVERIFY(o.open(QIODevice::WriteOnly));
        QZipWriter zip(&o);
        zip.addFile(QStringLiteral("xl/workbook.xml"), QByteArrayLiteral("<workbook/>"));
        zip.close();
        o.close();
        const auto r = DocxBook::build(other);
        QVERIFY(!r.ok());
        QVERIFY(r.error().message.contains(QStringLiteral("word/document.xml")));
        QVERIFY(!DocxBook::build(dir.filePath(QStringLiteral("absent.docx"))).ok());
    }
};

QTEST_GUILESS_MAIN(TstDocxBook)
#include "tst_docxbook.moc"
