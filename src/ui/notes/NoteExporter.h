#pragma once

#include "services/references/ReferenceService.h"
#include "ui/shell/AppContext.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QWidget;

namespace studylab::ui {

class MarkdownView;

/// Export d'une note ou d'un ensemble de notes (matière) en HTML autonome ou en PDF (07-versions, V1-7).
/// Le rendu est celui de l'aperçu (markdown-it, KaTeX, citations, images) dans une vue Web invisible, aux couleurs
/// du thème clair : le PDF sort de Chromium (`printToPdf`, A4), le HTML embarque ses styles, polices KaTeX et images.
class NoteExporter : public QObject {
    Q_OBJECT
public:
    enum class Format { Html, Pdf };

    explicit NoteExporter(AppContext& ctx, QObject* parent = nullptr);
    ~NoteExporter() override;

    /// Format d'après l'extension (.pdf, sinon HTML).
    static Format formatFor(const QString& path);
    /// Lance l'export ; finished() suit. false si un export est déjà en cours ou s'il n'y a aucune note.
    /// `heading` : titre du document (matière), vide pour une note seule.
    bool start(const QList<qint64>& noteIds, const QString& heading, const QString& path);
    bool isRunning() const { return m_running; }
    /// Demande le fichier de destination (Documents/<nom>.pdf|.html), exporte, puis propose d'ouvrir le fichier.
    void exportWithDialog(QWidget* parent, const QList<qint64>& noteIds, const QString& heading, const QString& baseName,
                          Format format);

    /// Page HTML autonome autour du contenu rendu (exposé pour les tests).
    static QString standaloneHtml(const QString& title, const QString& body);

signals:
    void finished(bool ok, const QString& path, const QString& error);

private:
    void render();
    void waitForFonts(int attempt);
    void write();
    void done(bool ok, const QString& error = {});

    AppContext& m_ctx;
    ReferenceService m_refs;
    QPointer<MarkdownView> m_view;
    QList<qint64> m_ids;
    QString m_heading;
    QString m_path;
    Format m_format = Format::Html;
    bool m_running = false;
};

}  // namespace studylab::ui
