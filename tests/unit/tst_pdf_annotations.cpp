#include "core/AppPaths.h"
#include "data/migrations/Migrator.h"
#include "data/repositories/DocumentRepository.h"
#include "data/sqlite/Database.h"
#include "engines/pdf/PdfAnnotationIO.h"
#include "engines/pdf/PdfObjects.h"
#include "services/annotations/AnnotationService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "services/pdfinterop/PdfInteropService.h"
#include "support/Fixtures.h"

#include <QFile>
#include <QImage>
#include <QPdfDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::pdf;

namespace {

/// PDF minimal écrit à la main : objets 1..N dans l'ordre, table classique ou flux de références,
/// avec éventuellement certains objets rangés dans un flux d'objets.
QByteArray buildPdf(const QList<QByteArray>& objects, bool xrefStream = false, const QList<int>& inObjectStream = {})
{
    QByteArray out("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QList<qint64> offsets(objects.size() + 1, 0);
    QByteArray objStmHeader;
    QByteArray objStmBody;
    for (int i = 0; i < objects.size(); ++i) {
        const int num = i + 1;
        if (inObjectStream.contains(num)) {
            objStmHeader += QByteArray::number(num) + ' ' + QByteArray::number(objStmBody.size()) + ' ';
            objStmBody += objects[i] + '\n';
            continue;
        }
        offsets[num] = out.size();
        out += QByteArray::number(num) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    int size = int(objects.size()) + 1;
    int objStmNum = 0;
    if (!inObjectStream.isEmpty()) {
        objStmNum = size++;
        const QByteArray body = objStmHeader + objStmBody;
        offsets << out.size();
        out += QByteArray::number(objStmNum) + " 0 obj\n<</Type/ObjStm/N " + QByteArray::number(inObjectStream.size()) +
               "/First " + QByteArray::number(objStmHeader.size()) + "/Length " + QByteArray::number(body.size()) +
               ">>\nstream\n" + body + "\nendstream\nendobj\n";
    }
    if (!xrefStream) {
        const qint64 start = out.size();
        out += "xref\n0 " + QByteArray::number(size) + "\n0000000000 65535 f\r\n";
        for (int n = 1; n < size; ++n)
            out += QByteArray::number(offsets[n]).rightJustified(10, '0') + " 00000 n\r\n";
        out += "trailer\n<</Size " + QByteArray::number(size) + "/Root 1 0 R>>\nstartxref\n" + QByteArray::number(start) +
               "\n%%EOF\n";
        return out;
    }
    const int xrefNum = size++;
    const qint64 start = out.size();
    offsets << start;
    QByteArray rows;
    QHash<int, int> indexInStream;
    for (int i = 0; i < inObjectStream.size(); ++i)
        indexInStream[inObjectStream[i]] = i;
    for (int n = 0; n < size; ++n) {
        if (n == 0) {
            rows += QByteArray("\x00\x00\x00\x00\x00\xFF\xFF", 7);
            continue;
        }
        if (indexInStream.contains(n)) {
            rows += char(2);
            rows += QByteArray("\x00\x00", 2) + char(objStmNum >> 8) + char(objStmNum & 0xFF);
            rows += char(0);
            rows += char(indexInStream[n]);
            continue;
        }
        const qint64 off = offsets[n];
        rows += char(1);
        rows += char((off >> 24) & 0xFF);
        rows += char((off >> 16) & 0xFF);
        rows += char((off >> 8) & 0xFF);
        rows += char(off & 0xFF);
        rows += QByteArray("\x00\x00", 2);
    }
    out += QByteArray::number(xrefNum) + " 0 obj\n<</Type/XRef/Size " + QByteArray::number(size) +
           "/W[1 4 2]/Root 1 0 R/Length " + QByteArray::number(rows.size()) + ">>\nstream\n" + rows +
           "\nendstream\nendobj\nstartxref\n" + QByteArray::number(start) + "\n%%EOF\n";
    return out;
}

/// Une page 600 × 800 avec une ligne de texte, un surlignage jaune commenté (et une réponse), une note,
/// un lien et une pièce jointe.
QList<QByteArray> sampleObjects()
{
    const QByteArray content = "BT /F1 24 Tf 100 700 Td (Hello studious world) Tj ET";
    return {
        "<</Type/Catalog/Pages 2 0 R>>",
        "<</Type/Pages/Kids[3 0 R]/Count 1/MediaBox[0 0 600 800]>>",
        "<</Type/Page/Parent 2 0 R/Resources<</Font<</F1 4 0 R>>>>/Contents 5 0 R/Annots[6 0 R 7 0 R 8 0 R 9 0 R 10 0 R]>>",
        "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>",
        "<</Length " + QByteArray::number(content.size()) + ">>\nstream\n" + content + "\nendstream",
        "<</Type/Annot/Subtype/Highlight/Rect[98 694 332 724]/QuadPoints[100 722 330 722 100 696 330 696]"
        "/C[1 1 0]/Contents(Important)/T(Alice)>>",
        "<</Type/Annot/Subtype/Text/Rect[400 600 420 620]/Contents<FEFF00C90074007500640069006500720020006300650063006900>"
        "/C[0 0 1]>>",
        "<</Type/Annot/Subtype/Text/Rect[400 600 420 620]/IRT 6 0 R/Contents(D'accord)>>",
        "<</Type/Annot/Subtype/Link/Rect[0 0 10 10]>>",
        "<</Type/Annot/Subtype/FileAttachment/Rect[0 0 10 10]>>",
    };
}

PdfColorScheme scheme()
{
    PdfColorScheme s;
    s.fill = {QColor("#F6E7A1"), QColor("#D3EACF"), QColor("#D3E3F6"), QColor("#F6D5DF"), QColor("#E4D9F4")};
    s.stroke = {QColor("#DDBE4E"), QColor("#79B472"), QColor("#6E9BD6"), QColor("#D98AA6"), QColor("#A88BD8")};
    return s;
}

QImage renderPage(const QByteArray& data, int page = 0)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("r.pdf"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(data);
    f.close();
    QPdfDocument doc;
    if (doc.load(path) != QPdfDocument::Error::None)
        return {};
    const QSizeF size = doc.pagePointSize(page);
    QPdfDocumentRenderOptions options;
    options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
    return doc.render(page, size.toSize(), options);
}

}  // namespace

class TstPdfAnnotations : public QObject {
    Q_OBJECT

private slots:
    void parsesObjects()
    {
        const QByteArray src = "<</A [1 2.5 -3 (str\\)ing) <414243> /N#20x true null 4 0 R] /B<</C/D>>>>";
        PdfParser p(src);
        auto o = p.parse();
        QVERIFY(o.has_value());
        QVERIFY(o->isDict());
        const PdfObject a = o->value("A");
        QCOMPARE(int(a.array.size()), 9);
        QCOMPARE(a.array[1].number(), 2.5);
        QCOMPARE(a.array[3].bytes, QByteArray("str)ing"));
        QCOMPARE(a.array[4].bytes, QByteArray("ABC"));
        QCOMPARE(a.array[5].bytes, QByteArray("N x"));
        QCOMPARE(a.array[8].type, PdfObject::Type::Ref);
        QCOMPARE(a.array[8].ref.num, 4);
        QVERIFY(o->value("B").value("C").isName("D"));
        // Aller-retour par la sérialisation
        PdfParser again(serialize(*o));
        auto back = again.parse();
        QVERIFY(back.has_value());
        QCOMPARE(serialize(*back), serialize(*o));
        QCOMPARE(PdfObject::fromText(QStringLiteral("Été")).text(), QStringLiteral("Été"));
    }

    void readsForeignAnnotations_data()
    {
        QTest::addColumn<bool>("xrefStream");
        QTest::addColumn<QList<int>>("packed");
        QTest::newRow("table classique") << false << QList<int>{};
        QTest::newRow("flux de références") << true << QList<int>{};
        QTest::newRow("flux d'objets") << true << QList<int>{2, 4, 7};
    }

    void readsForeignAnnotations()
    {
        QFETCH(bool, xrefStream);
        QFETCH(QList<int>, packed);
        const QByteArray data = buildPdf(sampleObjects(), xrefStream, packed);
        auto scan = readPdfAnnotations(data);
        QVERIFY2(scan.ok(), qPrintable(scan.ok() ? QString() : scan.error().message));
        const auto& items = scan.value().annotations;
        QCOMPARE(items.size(), 2);
        QCOMPARE(scan.value().unsupported, 1);  // pièce jointe
        const PdfAnnotation& h = items[0];
        QCOMPARE(h.type, AnnotationType::Highlight);
        QCOMPARE(h.page, 0);
        QCOMPARE(h.author, QStringLiteral("Alice"));
        QCOMPARE(h.contents, QStringLiteral("Important\n\nD'accord"));
        QCOMPARE(h.color, QColor(255, 255, 0));
        QCOMPARE(h.rects.size(), 1);
        QVERIFY(qAbs(h.rects[0].left() - 100.0 / 600) < 1e-6);
        QVERIFY(qAbs(h.rects[0].top() - (800.0 - 722) / 800) < 1e-6);
        QVERIFY(qAbs(h.rects[0].bottom() - (800.0 - 696) / 800) < 1e-6);
        const PdfAnnotation& n = items[1];
        QCOMPARE(n.type, AnnotationType::Note);
        QCOMPARE(n.contents, QStringLiteral("Étudier ceci"));
        QVERIFY(qAbs(n.rects[0].center().x() - 400.0 / 600) < 1e-6);
        QVERIFY(qAbs(n.rects[0].center().y() - (800.0 - 620) / 800) < 1e-6);
    }

    void extractsQuotedText()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a.pdf"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(buildPdf(sampleObjects()));
        f.close();
        auto scan = readPdfAnnotations(buildPdf(sampleObjects()));
        QVERIFY(scan.ok());
        QList<PdfAnnotation> items = scan.value().annotations;
        QPdfDocument doc;
        QCOMPARE(doc.load(path), QPdfDocument::Error::None);
        extractQuotes(doc, items);
        QVERIFY2(items[0].text.contains(QStringLiteral("studious")), qPrintable(items[0].text));
        QVERIFY(items[1].text.isEmpty());
    }

    void writesIncrementalCopy_data()
    {
        QTest::addColumn<bool>("xrefStream");
        QTest::newRow("table classique") << false;
        QTest::newRow("flux de références") << true;
    }

    void writesIncrementalCopy()
    {
        QFETCH(bool, xrefStream);
        const QByteArray original = buildPdf(sampleObjects(), xrefStream, xrefStream ? QList<int>{3} : QList<int>{});
        const PdfColorScheme colors = scheme();
        QList<PdfAnnotationOut> out;
        PdfAnnotationOut hl;
        hl.page = 0;
        hl.type = AnnotationType::Highlight;
        hl.fill = colors.fill[2];
        hl.stroke = colors.stroke[2];
        hl.rects = {QRectF(0.1, 0.4, 0.5, 0.03), QRectF(0.1, 0.44, 0.3, 0.03)};
        hl.contents = QStringLiteral("Été : à revoir (important)");
        hl.id = QStringLiteral("abc");
        out << hl;
        PdfAnnotationOut ul = hl;
        ul.type = AnnotationType::Underline;
        ul.rects = {QRectF(0.1, 0.6, 0.4, 0.03)};
        ul.contents.clear();
        out << ul;
        PdfAnnotationOut marker = hl;
        marker.type = AnnotationType::Marker;
        marker.rects = {QRectF(0.5, 0.7, 0.3, 0.2)};
        out << marker;
        PdfAnnotationOut note = hl;
        note.type = AnnotationType::Note;
        note.rects = {QRectF(0.795, 0.095, 0.01, 0.01)};
        note.contents = QStringLiteral("Une note");
        out << note;
        PdfAnnotationOut outside = hl;
        outside.page = 7;
        out << outside;

        auto written = writeAnnotatedCopy(original, out);
        QVERIFY2(written.ok(), qPrintable(written.ok() ? QString() : written.error().message));
        QCOMPARE(written.value().written, 4);
        QCOMPARE(written.value().skippedIndexes, QList<int>{4});
        const QByteArray copy = written.value().data;
        QVERIFY(copy.startsWith(original));  // octets d'origine intacts

        auto file = PdfFile::load(copy);
        QVERIFY(file.ok());
        QVERIFY(file.value().lastXref() > original.size());  // table de la révision ajoutée, pas reconstruite
        QCOMPARE(file.value().usesXrefStream(), xrefStream);

        auto scan = readPdfAnnotations(copy);
        QVERIFY(scan.ok());
        const auto& items = scan.value().annotations;
        QCOMPARE(items.size(), 2 + 4);  // annotations d'origine conservées
        for (int i = 0; i < 4; ++i) {
            const PdfAnnotation& a = items[2 + i];
            QVERIFY2(sameAnnotationGeometry(a.page, a.type, a.rects, out[i].page, out[i].type, out[i].rects),
                     qPrintable(QStringLiteral("annotation %1").arg(i)));
            QCOMPARE(a.author, QStringLiteral("StudyLab"));
        }
        QCOMPARE(items[2].contents, hl.contents);
        QCOMPARE(items[2].name, QStringLiteral("abc"));
        QCOMPARE(items[5].contents, QStringLiteral("Une note"));

        // Le moteur de Qt PDF ouvre la copie et dessine le surlignage (apparence fournie).
        const QImage before = renderPage(original);
        const QImage after = renderPage(copy);
        QVERIFY(!after.isNull());
        const QPoint inHighlight(int(0.3 * after.width()), int(0.415 * after.height()));
        QCOMPARE(before.pixelColor(inHighlight).alpha(), 0);  // fond transparent : rien de dessiné
        const QColor drawn = after.pixelColor(inHighlight);
        QVERIFY2(drawn.alpha() > 0, qPrintable(drawn.name(QColor::HexArgb)));
        QVERIFY(drawn.blue() > drawn.red());

        // Deuxième révision ajoutée sur la première (chaîne /Prev).
        PdfAnnotationOut again = ul;
        again.rects = {QRectF(0.1, 0.8, 0.4, 0.03)};
        auto second = writeAnnotatedCopy(copy, {again});
        QVERIFY(second.ok());
        auto rescan = readPdfAnnotations(second.value().data);
        QVERIFY(rescan.ok());
        QCOMPARE(rescan.value().annotations.size(), 7);
        QVERIFY(!renderPage(second.value().data).isNull());
    }

    void writesOnQtGeneratedPdf()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("q.pdf"));
        QVERIFY(test::writePdf(path, {QStringLiteral("Cours"), 3}));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray original = f.readAll();
        PdfAnnotationOut a;
        a.page = 2;
        a.type = AnnotationType::Strikeout;
        a.fill = Qt::yellow;
        a.stroke = Qt::red;
        a.rects = {QRectF(0.1, 0.1, 0.5, 0.05)};
        auto written = writeAnnotatedCopy(original, {a});
        QVERIFY2(written.ok(), qPrintable(written.ok() ? QString() : written.error().message));
        auto scan = readPdfAnnotations(written.value().data);
        QVERIFY(scan.ok());
        QCOMPARE(scan.value().annotations.size(), 1);
        QCOMPARE(scan.value().annotations[0].page, 2);
        QCOMPARE(scan.value().annotations[0].type, AnnotationType::Strikeout);
        const QImage img = renderPage(written.value().data, 2);
        QVERIFY(!img.isNull());
    }

    void refusesEncryptedAndSkipsRotated()
    {
        QList<QByteArray> objects = sampleObjects();
        QByteArray enc = buildPdf(objects);
        enc.replace("/Root 1 0 R>>", "/Root 1 0 R/Encrypt<</Filter/Standard>>>>");
        QVERIFY(!readPdfAnnotations(enc).ok());
        QVERIFY(!writeAnnotatedCopy(enc, {}).ok());

        objects[2].replace("/Type/Page", "/Type/Page/Rotate 90");
        const QByteArray rotated = buildPdf(objects);
        auto scan = readPdfAnnotations(rotated);
        QVERIFY(scan.ok());
        QCOMPARE(scan.value().annotations.size(), 0);
        QCOMPARE(scan.value().unsupported, 3);
        PdfAnnotationOut a;
        a.rects = {QRectF(0.1, 0.1, 0.2, 0.02)};
        auto written = writeAnnotatedCopy(rotated, {a});
        QVERIFY(written.ok());
        QCOMPARE(written.value().written, 0);
        QCOMPARE(written.value().data, rotated);
    }

    void rebuildsBrokenXrefForReading()
    {
        QByteArray data = buildPdf(sampleObjects());
        data.replace(data.lastIndexOf("startxref\n") + 10, 3, "999");  // décalage faux
        auto scan = readPdfAnnotations(data);
        QVERIFY(scan.ok());
        QCOMPARE(scan.value().annotations.size(), 2);
        // Écrire dans un fichier abîmé : refusé plutôt que risquer une copie illisible.
        QVERIFY(!writeAnnotatedCopy(data, {}).ok());
    }

    void serviceImportsAndExports()
    {
        QTemporaryDir dir;
        auto db = std::move(db::Database::openInMemory().value());
        QVERIFY(db::Migrator(db::Migrator::loadFrom().value()).migrate(*db).ok());
        const QString pdfPath = dir.filePath(QStringLiteral("source.pdf"));
        {
            QFile f(pdfPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(buildPdf(sampleObjects()));
        }
        DocumentRecord doc;
        doc.title = QStringLiteral("Cours : chapitre 1");
        doc.fileHash = QStringLiteral("h1");
        doc.storageMode = StorageMode::Linked;
        doc.filePath = pdfPath;
        doc.pageCount = 1;
        QVERIFY(db::DocumentRepository(*db).insert(doc).ok());

        LibraryService library(*db, AppPaths(dir.path()));
        AnnotationService annotations(*db);
        CommentService comments(*db);
        PdfInteropService interop(library, annotations, comments);
        const PdfColorScheme colors = scheme();

        auto preview = interop.preview(doc.id);
        QVERIFY(preview.ok());
        QCOMPARE(preview.value().fresh, 2);
        QCOMPARE(preview.value().unsupported, 1);

        auto imported = interop.importAnnotations(doc.id, colors);
        QVERIFY2(imported.ok(), qPrintable(imported.ok() ? QString() : imported.error().message));
        QCOMPARE(imported.value().imported, 2);
        auto list = annotations.list(doc.id);
        QVERIFY(list.ok());
        QCOMPARE(list.value().size(), 2);
        const AnnotationRecord& h = list.value().first().type == AnnotationType::Highlight ? list.value()[0] : list.value()[1];
        QCOMPARE(h.color, AnnotationColor::Yellow);
        QVERIFY(h.selectedText.contains(QStringLiteral("studious")));
        QCOMPARE(h.note, QStringLiteral("Important\n\nD'accord"));

        // Deuxième import : rien de nouveau.
        QCOMPARE(interop.preview(doc.id).value().fresh, 0);
        QCOMPARE(interop.importAnnotations(doc.id, colors).value().imported, 0);

        // Annotation et commentaire de page StudyLab, puis export.
        AnnotationRecord mine;
        mine.documentId = doc.id;
        mine.type = AnnotationType::Underline;
        mine.color = AnnotationColor::Green;
        PdfLocator loc;
        loc.page = 0;
        loc.rects = {QRectF(0.2, 0.5, 0.4, 0.03)};
        mine.locator = loc.toJson();
        mine.note = QStringLiteral("À retenir");
        QVERIFY(annotations.create(mine).ok());
        CommentRecord c;
        c.documentId = doc.id;
        c.anchor = CommentAnchor::Page;
        c.unitIndex = 0;
        PdfLocator pageLoc;
        pageLoc.page = 0;
        c.locator = pageLoc.toJson();
        c.kind = CommentKind::Question;
        c.body = QStringLiteral("Pourquoi ?");
        QVERIFY(comments.create(c).ok());

        QVERIFY(!interop.exportAnnotatedCopy(doc.id, pdfPath, colors).ok());  // jamais l'original
        const QString target = dir.filePath(QStringLiteral("copie.pdf"));
        auto exported = interop.exportAnnotatedCopy(doc.id, target, colors);
        QVERIFY2(exported.ok(), qPrintable(exported.ok() ? QString() : exported.error().message));
        QCOMPARE(exported.value().annotations, 1);
        QCOMPARE(exported.value().comments, 1);
        QCOMPARE(exported.value().alreadyPresent, 2);  // celles importées du fichier
        auto scan = PdfInteropService::readFile(target);
        QVERIFY(scan.ok());
        QCOMPARE(scan.value().annotations.size(), 4);
        QCOMPARE(scan.value().annotations[2].contents, QStringLiteral("À retenir"));
        QVERIFY(scan.value().annotations[3].contents.contains(QStringLiteral("Pourquoi ?")));
        QCOMPARE(PdfInteropService::nearestColor(scan.value().annotations[2].color, colors), AnnotationColor::Green);
    }

    void nearestColorAndFileName()
    {
        const PdfColorScheme s = scheme();
        QCOMPARE(PdfInteropService::nearestColor(QColor(255, 255, 0), s), AnnotationColor::Yellow);
        QCOMPARE(PdfInteropService::nearestColor(QColor(0, 255, 0), s), AnnotationColor::Green);
        QCOMPARE(PdfInteropService::nearestColor(QColor(0, 120, 255), s), AnnotationColor::Blue);
        QCOMPARE(PdfInteropService::nearestColor(QColor(255, 105, 180), s), AnnotationColor::Pink);
        QCOMPARE(PdfInteropService::nearestColor(QColor(140, 80, 220), s), AnnotationColor::Purple);
        QCOMPARE(PdfInteropService::nearestColor(QColor(), s), AnnotationColor::Yellow);
        DocumentRecord d;
        d.title = QStringLiteral("Physique : ondes/optique");
        QCOMPARE(PdfInteropService::suggestedFileName(d), QStringLiteral("Physique ondes optique (annoté).pdf"));
    }
};

QTEST_MAIN(TstPdfAnnotations)
#include "tst_pdf_annotations.moc"
