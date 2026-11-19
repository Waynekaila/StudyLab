#pragma once

#include "core/domain/Annotation.h"

#include <QAbstractListModel>
#include <QFrame>
#include <QList>
#include <QTimer>

#include <functional>
#include <optional>

class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QToolButton;
class QComboBox;

namespace studylab::ui {

class SegmentedControl;
class ThemeManager;

/// Lignes du volet : titres de groupe (chapitre ou page) et annotations.
class AnnotationListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { IsHeaderRole = Qt::UserRole + 1, RecordRole, IdRole };

    explicit AnnotationListModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void setRows(const QList<AnnotationRecord>& records, const std::function<QString(int)>& groupOf);
    int rowOf(qint64 id) const;
    int annotationCount() const { return m_count; }

private:
    struct Row {
        bool header = false;
        QString title;
        AnnotationRecord record;
    };
    QList<Row> m_rows;
    int m_count = 0;
};

/// Volet Annotations du lecteur : recherche, portée (document / page courante), filtres par couleur
/// (avec compteurs) et par type, regroupement par chapitre, navigation vers le passage, export.
class AnnotationsPane : public QFrame {
    Q_OBJECT
public:
    explicit AnnotationsPane(ThemeManager& theme, QWidget* parent = nullptr);

    void setAnnotations(const QList<AnnotationRecord>& records);
    void setCurrentPage(int page);
    /// Titre de groupe d'une page (chapitre du sommaire), vide → « Page N ».
    void setGroupResolver(std::function<QString(int)> resolver);
    /// Annotation mise en évidence (dernière créée ou cliquée dans le lecteur).
    void setActive(qint64 id, bool scrollTo = true);
    qint64 active() const { return m_active; }
    /// Barre « Annotation supprimée · Annuler », visible quelques secondes.
    void showUndo(const QString& message);
    void hideUndo();

    // Accès pour les tests
    QListView* list() const { return m_list; }
    AnnotationListModel* model() const { return m_model; }
    QLineEdit* searchEdit() const { return m_search; }
    SegmentedControl* scope() const { return m_scope; }
    QToolButton* colorFilter(std::optional<AnnotationColor> color) const;
    QComboBox* typeFilter() const { return m_type; }
    QLabel* countLabel() const { return m_count; }
    QPushButton* undoButton() const { return m_undo; }
    QToolButton* exportButton() const { return m_export; }
    int visibleAnnotationCount() const { return m_model->annotationCount(); }

signals:
    void annotationActivated(qint64 id);
    void contextMenuRequested(qint64 id, QPoint globalPos);
    void exportRequested();
    void closeRequested();
    void undoRequested();

private:
    void rebuild();
    void refreshIcons();
    void updateColorCounts();

    ThemeManager& m_theme;
    QList<AnnotationRecord> m_all;
    std::function<QString(int)> m_groupOf;
    int m_currentPage = 0;
    qint64 m_active = 0;
    std::optional<AnnotationColor> m_colorFilter;

    QLabel* m_title;
    QLabel* m_count;
    QToolButton* m_export;
    QToolButton* m_close;
    QLineEdit* m_search;
    SegmentedControl* m_scope;
    QList<QToolButton*> m_colorButtons;  ///< « Tout » puis les 5 couleurs
    QComboBox* m_type;
    QListView* m_list;
    AnnotationListModel* m_model;
    QLabel* m_empty;
    QFrame* m_undoBar;
    QLabel* m_undoText;
    QPushButton* m_undo;
    QTimer m_undoTimer;
    QTimer m_searchDebounce;
};

}  // namespace studylab::ui
