#pragma once

#include "core/domain/Reference.h"
#include "services/references/ReferenceService.h"
#include "ui/shell/AppContext.h"

#include <QList>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace studylab::ui {

/// Références bibliographiques (V1-6) : liste filtrable, fiche modifiable (enregistrée au fil de la saisie),
/// entrée de bibliographie mise en forme, notes qui la citent, import BibTeX / CSL-JSON et export.
/// Une note cite une référence par [[@clé]].
class ReferencesPage : public QWidget {
    Q_OBJECT
public:
    explicit ReferencesPage(AppContext& ctx, QWidget* parent = nullptr);

    void refresh();
    /// Affiche la fiche ; false si la référence n'existe pas.
    bool showReference(qint64 id);
    bool showCitekey(const QString& citekey);
    qint64 currentId() const { return m_current.id; }
    /// Crée une référence vide et l'affiche.
    qint64 createReference();
    /// Référence du document (créée depuis ses métadonnées au besoin), affichée.
    qint64 showForDocument(qint64 documentId);
    /// Import d'un fichier .bib ou .json ; le bilan s'affiche sous la liste.
    bool importFrom(const QString& path);
    /// Export de toutes les références (.bib ou .json selon l'extension).
    bool exportTo(const QString& path);
    /// Enregistre tout de suite les modifications en attente.
    bool saveNow();

    // Accès pour les tests
    QListWidget* list() const { return m_list; }
    QLineEdit* filterField() const { return m_filter; }
    QLabel* navStatus() const { return m_navStatus; }
    QLabel* citationLabel() const { return m_citation; }
    QLabel* entryLabel() const { return m_entry; }
    QLabel* formStatus() const { return m_formStatus; }
    QLineEdit* keyField() const { return m_key; }
    QComboBox* typeBox() const { return m_type; }
    QLineEdit* titleField() const { return m_title; }
    QPlainTextEdit* authorsField() const { return m_authors; }
    QLineEdit* yearField() const { return m_year; }
    QLineEdit* containerField() const { return m_container; }
    QLineEdit* pagesField() const { return m_pages; }
    QComboBox* documentBox() const { return m_document; }
    QPushButton* openDocumentButton() const { return m_openDocument; }
    QPushButton* removeButton() const { return m_remove; }
    QToolButton* generateKeyButton() const { return m_generateKey; }
    QList<QPushButton*> citingRows() const { return m_citingRows; }

signals:
    void openDocumentRequested(qint64 documentId);
    void openNoteRequested(qint64 noteId);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildNav();
    void buildEditor();
    void fillList();
    void fillForm();
    void fillDocuments();
    void renderPreview();
    void renderCiting();
    Reference formValue() const;
    void markDirty();
    void commitKey();
    void confirmRemove();
    void chooseImport();
    void chooseExport();

    AppContext& m_ctx;
    ReferenceService m_refs;
    Reference m_current;
    QList<Reference> m_all;
    QTimer m_saveTimer;
    bool m_loading = false;
    bool m_dirty = false;
    bool m_stale = true;

    QListWidget* m_list = nullptr;
    QLineEdit* m_filter = nullptr;
    QLabel* m_navStatus = nullptr;
    QStackedWidget* m_center = nullptr;
    QWidget* m_empty = nullptr;
    QLabel* m_citation = nullptr;
    QLabel* m_kind = nullptr;
    QLabel* m_entry = nullptr;
    QLabel* m_formStatus = nullptr;
    QLineEdit* m_key = nullptr;
    QToolButton* m_generateKey = nullptr;
    QComboBox* m_type = nullptr;
    QLineEdit* m_title = nullptr;
    QPlainTextEdit* m_authors = nullptr;
    QPlainTextEdit* m_editors = nullptr;
    QLineEdit* m_year = nullptr;
    QLineEdit* m_container = nullptr;
    QLineEdit* m_publisher = nullptr;
    QLineEdit* m_place = nullptr;
    QLineEdit* m_volume = nullptr;
    QLineEdit* m_issue = nullptr;
    QLineEdit* m_pages = nullptr;
    QLineEdit* m_edition = nullptr;
    QLineEdit* m_doi = nullptr;
    QLineEdit* m_isbn = nullptr;
    QLineEdit* m_url = nullptr;
    QLineEdit* m_language = nullptr;
    QComboBox* m_document = nullptr;
    QPushButton* m_openDocument = nullptr;
    QPushButton* m_remove = nullptr;
    QLabel* m_citingEmpty = nullptr;
    QVBoxLayout* m_citingList = nullptr;
    QList<QPushButton*> m_citingRows;
};

}  // namespace studylab::ui
