#pragma once

#include "core/domain/Document.h"

#include <QFrame>
#include <QImage>

class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace studylab {
class LibraryService;
}

namespace studylab::ui {

class ThemeManager;

/// Couverture dessinée à la taille du widget.
class CoverWidget : public QWidget {
    Q_OBJECT
public:
    CoverWidget(ThemeManager& theme, QWidget* parent = nullptr);
    void setDocument(const DocumentRecord& r, const QImage& cover);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    ThemeManager& m_theme;
    DocumentRecord m_record;
    QImage m_cover;
};

/// Panneau de droite : fiche du document sélectionné, statut, favori, modification et retrait.
class DocumentDetailPanel : public QFrame {
    Q_OBJECT
public:
    DocumentDetailPanel(LibraryService& library, ThemeManager& theme, QWidget* parent = nullptr);

    void showDocument(qint64 id);
    qint64 documentId() const { return m_record.id; }

    QComboBox* statusCombo() const { return m_status; }
    QToolButton* favoriteButton() const { return m_favorite; }
    QPushButton* editButton() const { return m_edit; }
    QPushButton* readButton() const { return m_read; }
    QPushButton* removeButton() const { return m_remove; }
    QLabel* titleLabel() const { return m_title; }

signals:
    /// Le document a été retiré de la bibliothèque.
    void removed(qint64 id);
    void readRequested(qint64 id);

private:
    void render();
    void editDocument();
    void removeDocument();
    void addField(const QString& label, const QString& value);

    LibraryService& m_library;
    ThemeManager& m_theme;
    DocumentRecord m_record;
    CoverWidget* m_cover;
    QLabel* m_title;
    QLabel* m_authors;
    QComboBox* m_status;
    QToolButton* m_favorite;
    QPushButton* m_read;
    QPushButton* m_edit;
    QPushButton* m_remove;
    QPushButton* m_reveal;
    QGridLayout* m_fields;
    QWidget* m_tagsBox;
    QLabel* m_tagsCaption;
    QLabel* m_descriptionCaption;
    QLabel* m_description;
    int m_fieldRow = 0;
};

}  // namespace studylab::ui
