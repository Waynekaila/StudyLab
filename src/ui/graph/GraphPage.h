#pragma once

#include "core/domain/Graph.h"
#include "services/knowledge/KnowledgeService.h"
#include "ui/shell/AppContext.h"

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QPointF>
#include <QSet>
#include <QWidget>

#include <atomic>
#include <memory>

class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QHBoxLayout;
class QVBoxLayout;

namespace studylab::ui {

class PanelSplitter;

class GraphView;
class SegmentedControl;
class ToggleSwitch;

/// Graphe de connaissances (05-maquettes §7) : filtres (types, matières), profondeur autour de la sélection,
/// étiquettes, regroupement par matière ; fiche du nœud sélectionné (extrait, chiffres, Ouvrir / Centrer / Isoler).
/// La disposition est calculée en arrière-plan (QtConcurrent) puis appliquée en douceur.
class GraphPage : public QWidget {
    Q_OBJECT
public:
    explicit GraphPage(AppContext& ctx, QWidget* parent = nullptr);
    ~GraphPage() override;

    /// Recharge le graphe et relance la disposition si son contenu a changé.
    void refresh();
    /// Sélectionne un nœud par sa clé (« n:5 », « d:12 ») ; false s'il n'est pas affiché.
    bool selectNode(const QString& key);
    QString selectedKey() const { return m_selectedKey; }
    /// Isole le voisinage du nœud sélectionné (profondeur choisie) ; chaîne vide : tout afficher.
    void isolate(const QString& key);
    QString isolatedKey() const { return m_isolateKey; }
    /// Trouve un nœud affiché par son titre (insensible à la casse), le sélectionne et centre la vue dessus.
    bool findNode(const QString& title);
    bool layoutRunning() const { return m_running; }

    // Accès pour les tests
    GraphView* view() const { return m_view; }
    QFrame* card() const { return m_card; }
    QLabel* cardTitle() const { return m_cardTitle; }
    QLabel* cardKind() const { return m_cardKind; }
    QLabel* cardExcerpt() const { return m_cardExcerpt; }
    QList<QLabel*> cardStats() const { return m_statValues; }
    QPushButton* openButton() const { return m_open; }
    QPushButton* centerButton() const { return m_center; }
    QPushButton* isolateButton() const { return m_isolate; }
    SegmentedControl* depthControl() const { return m_depth; }
    ToggleSwitch* typeToggle(GraphNodeKind kind) const;
    ToggleSwitch* labelsToggle() const { return m_labels; }
    ToggleSwitch* groupToggle() const { return m_group; }
    QList<QCheckBox*> subjectBoxes() const { return m_subjectBoxes; }
    QLineEdit* findField() const { return m_find; }
    QLabel* statusLabel() const { return m_status; }
    QToolButton* showAllButton() const { return m_showAll; }

signals:
    void openDocumentRequested(qint64 documentId);
    void openNoteRequested(qint64 noteId);
    /// Lien non résolu : créer la note qui porte ce titre.
    void createNoteRequested(const QString& title);
    void notesRequested();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildFilters();
    void buildCenter();
    void refreshSubjects();
    void rebuildShown();
    void startLayout();
    void onLayoutFinished();
    void applySelection();
    void updateCard();
    void updateStatus();
    void openSelected();
    void refreshIcons();
    GraphFilter currentFilter() const;
    int depth() const;

    AppContext& m_ctx;
    PanelSplitter* m_panels = nullptr;
    QHBoxLayout* m_navHead = nullptr;
    KnowledgeService m_knowledge;
    KnowledgeGraph m_full;
    KnowledgeGraph m_shown;
    QHash<QString, QPointF> m_positions;  ///< dernières positions connues, par clé
    QString m_selectedKey;
    QString m_isolateKey;
    QString m_shownSignature;             ///< nœuds et arêtes affichés : relancer la disposition s'il change
    QSet<qint64> m_hiddenSubjects;
    bool m_hideUnfiled = false;
    bool m_dirty = true;
    bool m_fitPending = true;
    bool m_running = false;
    bool m_relayoutPending = false;
    int m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QFutureWatcher<QList<QPointF>>* m_watcher = nullptr;

    QLineEdit* m_find = nullptr;
    ToggleSwitch* m_typeDocs = nullptr;
    ToggleSwitch* m_typeNotes = nullptr;
    ToggleSwitch* m_typeConcepts = nullptr;
    ToggleSwitch* m_typeUnresolved = nullptr;
    QVBoxLayout* m_subjectsLayout = nullptr;
    QList<QCheckBox*> m_subjectBoxes;
    QLabel* m_noSubjects = nullptr;
    SegmentedControl* m_depth = nullptr;
    ToggleSwitch* m_labels = nullptr;
    ToggleSwitch* m_group = nullptr;

    QStackedWidget* m_stack = nullptr;
    QWidget* m_empty = nullptr;
    QWidget* m_canvas = nullptr;
    GraphView* m_view = nullptr;
    QFrame* m_card = nullptr;
    QLabel* m_cardDot = nullptr;
    QLabel* m_cardKind = nullptr;
    QLabel* m_cardTitle = nullptr;
    QLabel* m_cardExcerpt = nullptr;
    QList<QLabel*> m_statValues;
    QList<QLabel*> m_statLabels;
    QPushButton* m_open = nullptr;
    QPushButton* m_center = nullptr;
    QPushButton* m_isolate = nullptr;
    QFrame* m_toolbar = nullptr;
    QLabel* m_status = nullptr;
    QToolButton* m_showAll = nullptr;
    QList<QLabel*> m_typeIcons;
};

}  // namespace studylab::ui
