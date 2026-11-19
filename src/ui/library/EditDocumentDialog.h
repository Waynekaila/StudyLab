#pragma once

#include "core/domain/Document.h"

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace studylab::ui {

/// Modification des métadonnées d'un document (titre, auteurs, éditeur, année, identifiants, tags, description).
class EditDocumentDialog : public QDialog {
    Q_OBJECT
public:
    EditDocumentDialog(const DocumentRecord& record, const QStringList& knownTags, QWidget* parent = nullptr);

    /// Copie du document avec les valeurs saisies.
    DocumentRecord result() const;
    QLineEdit* titleEdit() const { return m_title; }
    QLineEdit* authorsEdit() const { return m_authors; }
    QLineEdit* tagsEdit() const { return m_tags; }

    /// Découpe une saisie en éléments non vides, espaces superflus retirés.
    static QStringList splitList(const QString& text, QChar separator);

private:
    DocumentRecord m_record;
    QLineEdit* m_title;
    QLineEdit* m_subtitle;
    QLineEdit* m_authors;
    QLineEdit* m_publisher;
    QSpinBox* m_year;
    QLineEdit* m_isbn;
    QLineEdit* m_doi;
    QLineEdit* m_language;
    QLineEdit* m_tags;
    QPlainTextEdit* m_description;
};

}  // namespace studylab::ui
