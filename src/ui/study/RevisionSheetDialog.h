#pragma once

#include "services/notes/NoteDrafts.h"
#include "ui/shell/AppContext.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace studylab::ui {

/// « Générer une fiche de révision » (V2-3) : choix de la matière et des rubriques ; aperçu de ce qui sera repris.
class RevisionSheetDialog : public QDialog {
    Q_OBJECT
public:
    RevisionSheetDialog(AppContext& ctx, qint64 subjectId, QWidget* parent = nullptr);

    qint64 subjectId() const;
    NoteDrafts::SheetOptions options() const;

    QComboBox* subjectBox() const { return m_subject; }
    QCheckBox* option(int index) const { return m_options.value(index); }
    QLabel* summary() const { return m_summary; }
    QPushButton* generateButton() const { return m_generate; }

private:
    void refreshSummary();

    AppContext& m_ctx;
    QComboBox* m_subject;
    QList<QCheckBox*> m_options;
    QLabel* m_summary;
    QPushButton* m_generate;
};

}  // namespace studylab::ui
