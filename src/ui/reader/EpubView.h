#pragma once

#include "ui/theme/Tokens.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QWebEngineView>

#include <functional>

namespace studylab::ui {

/// Réglages d'affichage du texte (panneau « Affichage du texte », 05-maquettes §4).
struct EpubSettings {
    enum class Font { Original, Serif, Sans };
    enum class Margins { Narrow, Normal, Wide };
    Font font = Font::Original;
    int fontSize = 100;        ///< en %
    double lineHeight = 1.6;
    Margins margins = Margins::Normal;
    bool scrolled = false;     ///< défilement continu plutôt que pages
    ReadingTheme theme = ReadingTheme::White;

    QJsonObject toJson() const;  ///< réglages du lecteur JS, couleurs comprises
    QJsonObject toStored() const;
    static EpubSettings fromStored(const QJsonObject& o);
    bool operator==(const EpubSettings&) const = default;
};

/// Position courante signalée par le lecteur.
struct EpubLocation {
    QString cfi;
    double fraction = 0;     ///< progression dans le livre (0..1)
    int section = 0;
    int sections = 0;
    QString tocLabel;
    QString tocHref;
    int page = 0;            ///< page dans le chapitre (mode paginé), 0 sinon
    int pages = 0;
    double sectionMinutes = 0;
};

struct EpubSelection {
    QString cfi;
    QString text;
    QString prefix;
    QString suffix;
    int section = 0;
    double fraction = 0;
    double progress = 0;
    QRect rect;  ///< coordonnées de la vue
};

struct EpubTocEntry {
    QString label;
    QString href;
    int depth = 0;
    int section = -1;
};

/// Rendu d'un EPUB par foliate-js dans Qt WebEngine (03-comparaison-bibliotheques §EPUB).
/// Les échanges passent par QWebChannel (lecteur → application) et runJavaScript (application → lecteur).
class EpubView : public QWebEngineView {
    Q_OBJECT
public:
    explicit EpubView(QWidget* parent = nullptr);
    ~EpubView() override;

    /// Ouvre un livre déjà enregistré auprès du gestionnaire de schéma sous `token`.
    void openBook(const QString& token, const QHash<QString, qint64>& sizes, const QString& lastCfi,
                  const EpubSettings& settings);
    bool isOpened() const { return m_opened; }
    const QList<EpubTocEntry>& toc() const { return m_toc; }
    const EpubLocation& location() const { return m_location; }
    int sectionCount() const { return m_sections; }

    void setSettings(const EpubSettings& settings);
    void goTo(const QString& target);  ///< CFI ou lien du sommaire
    void goToFraction(double fraction);
    void next();
    void previous();
    void back();
    void forward();
    /// Annotations à dessiner : tableau de { id, cfi, type, fill, stroke }.
    void setAnnotations(const QJsonArray& annotations);
    void reveal(const QString& cfi);
    void clearSelection();
    void search(const QString& query);
    void clearSearch();
    void showSearchResult(int index);
    int searchResultCount() const { return m_searchCount; }
    bool searchDone() const { return m_searchDone; }
    /// Évalue une expression sur l'API du lecteur (window.StudyLab), résultat JSON.
    void evaluate(const QString& expression, std::function<void(const QVariant&)> callback);

signals:
    void opened();
    void loadFailed(const QString& message);
    void relocated(const studylab::ui::EpubLocation& location);
    void selectionMade(const studylab::ui::EpubSelection& selection);
    void selectionCleared();
    void annotationClicked(qint64 id, const QRect& rect);
    void contextMenuRequested(qint64 annotationId, const QPoint& globalPos);
    void searchProgress(int count, bool done);
    void bookmarkStateChanged(qint64 bookmarkId);
    void escapePressed();
    void externalLinkRequested(const QString& href);

private:
    friend class EpubBridge;
    void handle(const QString& event, const QJsonObject& data);
    void call(const QString& method, const QJsonValue& arg = QJsonValue::Undefined);

    class EpubBridge* m_bridge;
    bool m_ready = false;
    bool m_opened = false;
    QJsonObject m_pendingOpen;
    QList<EpubTocEntry> m_toc;
    EpubLocation m_location;
    int m_sections = 0;
    int m_searchCount = 0;
    bool m_searchDone = true;
};

}  // namespace studylab::ui
