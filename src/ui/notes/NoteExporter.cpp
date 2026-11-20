#include "ui/notes/NoteExporter.h"

#include "services/notes/NoteService.h"
#include "ui/notes/NotePreview.h"
#include "ui/reader/MarkdownView.h"
#include "ui/theme/Tokens.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QWebEnginePage>

#include <memory>

namespace studylab::ui {

namespace {

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

/// Couleurs de l'aperçu, thème clair : un document exporté s'imprime et se lit sur fond blanc.
QJsonObject printVars()
{
    const Palette p = Palette::light();
    return {
        {QStringLiteral("bg"), QStringLiteral("#ffffff")},
        {QStringLiteral("card"), hex(p.bg)},
        {QStringLiteral("card-border"), hex(p.borderSoft)},
        {QStringLiteral("text"), hex(p.text)},
        {QStringLiteral("muted"), hex(p.text2)},
        {QStringLiteral("border"), hex(p.border)},
        {QStringLiteral("link"), hex(p.primaryStrong)},
        {QStringLiteral("accent"), hex(p.primary)},
        {QStringLiteral("wiki"), hex(p.primaryStrong)},
        {QStringLiteral("quote-bar"), hex(p.accYellow)},
        {QStringLiteral("code-bg"), QStringLiteral("rgba(0,0,0,.05)")},
        {QStringLiteral("font"), QStringLiteral("\"Inter\", system-ui, \"Segoe UI\", sans-serif")},
        {QStringLiteral("size"), QStringLiteral("14px")},
    };
}

QString resource(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString jsonArg(const QJsonValue& v)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

}  // namespace

NoteExporter::NoteExporter(AppContext& ctx, QObject* parent) : QObject(parent), m_ctx(ctx), m_refs(ctx.db, ctx.notes) {}

NoteExporter::~NoteExporter() { delete m_view; }

NoteExporter::Format NoteExporter::formatFor(const QString& path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0 ? Format::Pdf : Format::Html;
}

bool NoteExporter::start(const QList<qint64>& noteIds, const QString& heading, const QString& path)
{
    if (m_running || noteIds.isEmpty() || path.isEmpty())
        return false;
    m_running = true;
    m_ids = noteIds;
    m_heading = heading;
    m_path = path;
    m_format = formatFor(path);
    if (!m_view) {
        // Vue jamais affichée : le rendu et l'impression n'en ont pas besoin.
        m_view = new MarkdownView;
        m_view->setAttribute(Qt::WA_DontShowOnScreen);
        m_view->resize(900, 1200);
        m_view->start();  // jamais affichée : la page est créée tout de suite
        connect(m_view, &MarkdownView::ready, this, [this] {
            if (m_running)
                render();
        });
        connect(m_view->page(), &QWebEnginePage::pdfPrintingFinished, this, [this](const QString&, bool ok) {
            done(ok, ok ? QString() : tr("Impossible d'écrire le PDF %1.").arg(m_path));
        });
    }
    if (m_view->isReady())
        QTimer::singleShot(0, this, &NoteExporter::render);
    return true;
}

void NoteExporter::exportWithDialog(QWidget* parent, const QList<qint64>& noteIds, const QString& heading,
                                    const QString& baseName, Format format)
{
    if (m_running || noteIds.isEmpty())
        return;
    static const QRegularExpression unsafe(QStringLiteral("[\\\\/:*?\"<>|]"));
    QString name = baseName.simplified();
    name.replace(unsafe, QStringLiteral(" "));
    const bool pdf = format == Format::Pdf;
    const QString suggested = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + u'/'
                            + name.simplified() + (pdf ? QStringLiteral(".pdf") : QStringLiteral(".html"));
    QString path = QFileDialog::getSaveFileName(parent, pdf ? tr("Exporter en PDF") : tr("Exporter en HTML"), suggested,
                                                pdf ? tr("PDF (*.pdf)") : tr("Page HTML (*.html *.htm)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += pdf ? QStringLiteral(".pdf") : QStringLiteral(".html");
    QPointer<QWidget> owner(parent);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(this, &NoteExporter::finished, this, [owner, conn](bool ok, const QString& file, const QString& error) {
        QObject::disconnect(*conn);
        if (!owner)
            return;
        if (!ok) {
            QMessageBox::warning(owner, tr("Exporter"), error);
            return;
        }
        QMessageBox box(QMessageBox::Information, tr("Exporter"),
                        tr("« %1 » est prêt.").arg(QFileInfo(file).fileName()), QMessageBox::NoButton, owner);
        QPushButton* open = box.addButton(tr("Ouvrir"), QMessageBox::AcceptRole);
        box.addButton(tr("Fermer"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == open)
            QDesktopServices::openUrl(QUrl::fromLocalFile(file));
    });
    if (!start(noteIds, heading, path)) {
        QObject::disconnect(*conn);
        QMessageBox::warning(parent, tr("Exporter"), tr("Un export est déjà en cours."));
    }
}

void NoteExporter::render()
{
    QJsonArray notes;
    for (qint64 id : std::as_const(m_ids)) {
        auto n = m_ctx.notes.find(id);
        if (!n)
            continue;
        const QString dir = m_ctx.notes.attachmentsDir();
        notes.append(notePreviewPayload(m_ctx.notes, m_refs, n.value().title, n.value().folder, n.value().body,
                                        [dir](const QString& rel) { return attachmentDataUri(dir, rel); }));
    }
    if (notes.isEmpty()) {
        done(false, tr("Aucune note à exporter."));
        return;
    }
    m_view->setThemeVars(printVars());
    m_view->evaluate(QStringLiteral("StudyLab.setNotes(%1, %2)").arg(jsonArg(notes), jsonArg(m_heading)),
                     [this](const QVariant& count) {
                         if (count.toInt() <= 0) {
                             done(false, tr("Le rendu des notes a échoué."));
                             return;
                         }
                         waitForFonts(0);
                     });
}

void NoteExporter::waitForFonts(int attempt)
{
    // Polices KaTeX et images décodées avant l'impression ou la copie.
    m_view->evaluate(QStringLiteral("document.fonts.status === 'loaded' && [...document.images].every(i => i.complete)"),
                     [this, attempt](const QVariant& ready) {
                         if (ready.toBool() || attempt >= 50)
                             write();
                         else
                             QTimer::singleShot(40, this, [this, attempt] { waitForFonts(attempt + 1); });
                     });
}

void NoteExporter::write()
{
    if (m_format == Format::Pdf) {
        const QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(18, 16, 18, 18),
                                 QPageLayout::Millimeter);
        m_view->page()->printToPdf(m_path, layout);
        return;
    }
    m_view->evaluate(QStringLiteral("StudyLab.exportHtml()"), [this](const QVariant& body) {
        QString title = m_heading;
        if (title.isEmpty() && !m_ids.isEmpty())
            if (auto n = m_ctx.notes.find(m_ids.first()))
                title = n.value().title;
        QSaveFile f(m_path);
        if (!f.open(QIODevice::WriteOnly) || f.write(standaloneHtml(title, body.toString()).toUtf8()) < 0 || !f.commit()) {
            done(false, tr("Impossible d'écrire %1 : %2").arg(m_path, f.errorString()));
            return;
        }
        done(true);
    });
}

void NoteExporter::done(bool ok, const QString& error)
{
    if (!m_running)
        return;
    m_running = false;
    emit finished(ok, m_path, error);
}

QString NoteExporter::standaloneHtml(const QString& title, const QString& body)
{
    // Feuille KaTeX avec ses polices en data: (woff2 seulement, lu par tous les navigateurs actuels).
    QString katex = resource(QStringLiteral(":/epub-reader/markdown/vendor/katex/katex.min.css"));
    static const QRegularExpression otherFormats(QStringLiteral(",\\s*url\\([^)]*\\.(?:woff|ttf)\\)\\s*format\\(\"(?:woff|truetype)\"\\)"));
    katex.remove(otherFormats);
    static const QRegularExpression font(QStringLiteral("url\\((fonts/[\\w-]+\\.woff2)\\)"));
    QString inlined;
    qsizetype last = 0;
    auto it = font.globalMatch(katex);
    while (it.hasNext()) {
        const auto m = it.next();
        inlined += katex.mid(last, m.capturedStart() - last);
        QFile f(QStringLiteral(":/epub-reader/markdown/vendor/katex/") + m.captured(1));
        inlined += f.open(QIODevice::ReadOnly)
                       ? QStringLiteral("url(data:font/woff2;base64,%1)").arg(QString::fromLatin1(f.readAll().toBase64()))
                       : m.captured(0);
        last = m.capturedEnd();
    }
    inlined += katex.mid(last);

    QString vars;
    const QJsonObject v = printVars();
    for (auto i = v.constBegin(); i != v.constEnd(); ++i)
        vars += QStringLiteral("--%1: %2; ").arg(i.key(), i.value().toString());
    QString escapedTitle = title.toHtmlEscaped();
    return QStringLiteral("<!doctype html>\n<html lang=\"fr\">\n<head>\n<meta charset=\"utf-8\">\n"
                          "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                          "<meta name=\"generator\" content=\"StudyLab\">\n<title>%1</title>\n"
                          "<style>\n%2\n</style>\n<style>\n%3\n:root { %4}\n</style>\n</head>\n"
                          "<body class=\"preview note export\">\n<main id=\"root\">%5</main>\n</body>\n</html>\n")
        .arg(escapedTitle, inlined, resource(QStringLiteral(":/epub-reader/markdown/view.css")), vars, body);
}

}  // namespace studylab::ui
