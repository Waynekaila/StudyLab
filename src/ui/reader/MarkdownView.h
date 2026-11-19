#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QWebEngineView>

#include <functional>

namespace studylab::ui {

class MarkdownBridge;

/// Rendu Markdown (markdown-it + KaTeX, ressources embarquées, hors ligne) dans Qt WebEngine.
/// Deux usages : la liste des commentaires sous forme de cartes et l'aperçu de l'éditeur.
/// Les appels faits avant le chargement de la page sont rejoués dès qu'elle est prête.
class MarkdownView : public QWebEngineView {
    Q_OBJECT
public:
    explicit MarkdownView(QWidget* parent = nullptr);
    ~MarkdownView() override;

    bool isReady() const { return m_ready; }

    /// Variables CSS (sans « -- ») : bg, card, text, muted, border, link, accent…
    void setThemeVars(const QJsonObject& vars);
    /// Cartes : {id, kind, anchor, title, where, date, quote, quoteColor, body, resolved, …}.
    void setCards(const QJsonArray& cards, const QString& emptyText);
    void setActive(qint64 id, bool scroll);
    void setMarkdown(const QString& text, const QString& emptyText);
    /// Aperçu d'une note : {title, body (sans front matter), lineOffset, chips, unresolved, images, unresolvedLabel}.
    void setNote(const QJsonObject& note, const QString& emptyText);
    /// Fait défiler l'aperçu de note jusqu'au titre qui précède la ligne (0) du texte complet.
    void scrollToLine(int line);

    /// Évalue une expression JavaScript (résultat sérialisé en JSON) — tests et diagnostic.
    void evaluate(const QString& expression, std::function<void(const QVariant&)> callback);

signals:
    void ready();
    void cardActivated(qint64 id);
    void cardEditRequested(qint64 id);
    void cardDeleteRequested(qint64 id);
    void cardMenuRequested(qint64 id, QPoint globalPos);
    void linkActivated(const QUrl& url);
    /// Clic sur un [[lien]] dans l'aperçu d'une note (cible sans « |alias »).
    void wikiLinkActivated(const QString& target);

private:
    friend class MarkdownBridge;
    void handle(const QString& event, const QJsonObject& payload);
    void call(const QString& method, const QJsonArray& args);

    MarkdownBridge* m_bridge;
    bool m_ready = false;
    QStringList m_pending;
};

}  // namespace studylab::ui
