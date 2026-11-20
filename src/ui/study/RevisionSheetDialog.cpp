#include "ui/study/RevisionSheetDialog.h"

#include "services/organize/OrganizeService.h"
#include "ui/components/Widgets.h"
#include "ui/theme/ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace studylab::ui {

RevisionSheetDialog::RevisionSheetDialog(AppContext& ctx, qint64 subjectId, QWidget* parent)
    : QDialog(parent), m_ctx(ctx)
{
    setWindowTitle(tr("Générer une fiche de révision"));
    setObjectName(QStringLiteral("RevisionSheetDialog"));
    setMinimumWidth(ctx.theme.px(480));
    auto* v = new QVBoxLayout(this);
    v->setSpacing(12);
    auto* intro = makeLabel(tr("StudyLab rassemble dans une note les passages annotés, les définitions, les formules et "
                               "les questions de la matière. C'est un brouillon : à vous de le relire et de le "
                               "reformuler."),
                            "muted", this);
    intro->setWordWrap(true);
    v->addWidget(intro);

    auto* form = new QFormLayout;
    m_subject = new QComboBox(this);
    m_subject->setObjectName(QStringLiteral("SheetSubject"));
    if (auto all = ctx.organize.subjects()) {
        for (const SubjectRecord& s : all.value()) {
            if (!s.archived || s.id == subjectId)
                m_subject->addItem(ctx.organize.subjectPath(s.id, all.value()), s.id);
        }
    }
    m_subject->setCurrentIndex(std::max(0, m_subject->findData(subjectId)));
    form->addRow(tr("Matière"), m_subject);
    v->addLayout(form);

    const QStringList labels = {tr("Passages annotés et leurs commentaires"), tr("Définitions (notes « Concept »)"),
                                tr("Formules relevées dans les notes et les commentaires"),
                                tr("Questions : commentaires « Question » non résolus et cartes mémoire")};
    for (int i = 0; i < labels.size(); ++i) {
        auto* box = new QCheckBox(labels[i], this);
        box->setObjectName(QStringLiteral("SheetOption%1").arg(i));
        box->setChecked(true);
        m_options << box;
        v->addWidget(box);
    }
    m_summary = makeLabel(QString(), "faint", this);
    m_summary->setObjectName(QStringLiteral("SheetSummary"));
    m_summary->setWordWrap(true);
    v->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_generate = buttons->addButton(tr("Générer le brouillon"), QDialogButtonBox::AcceptRole);
    m_generate->setProperty("variant", QStringLiteral("primary"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);

    connect(m_subject, &QComboBox::currentIndexChanged, this, &RevisionSheetDialog::refreshSummary);
    refreshSummary();
}

qint64 RevisionSheetDialog::subjectId() const { return m_subject->currentData().toLongLong(); }

NoteDrafts::SheetOptions RevisionSheetDialog::options() const
{
    NoteDrafts::SheetOptions o;
    o.passages = m_options[0]->isChecked();
    o.concepts = m_options[1]->isChecked();
    o.formulas = m_options[2]->isChecked();
    o.questions = m_options[3]->isChecked();
    return o;
}

void RevisionSheetDialog::refreshSummary()
{
    const qint64 id = subjectId();
    m_generate->setEnabled(id > 0);
    if (id <= 0) {
        m_summary->setText(tr("Aucune matière : créez-en une dans l'espace Matières et rattachez-y documents et notes."));
        return;
    }
    auto stats = m_ctx.organize.subjectStats(id);
    if (!stats)
        return;
    const SubjectStats& s = stats.value();
    m_summary->setText(tr("Matière et sous-matières : %1, %2, %3.")
                           .arg(countLabel(s.documents, tr("document"), tr("documents")),
                                countLabel(s.annotations, tr("annotation"), tr("annotations")),
                                countLabel(s.notes, tr("note"), tr("notes"))));
}

}  // namespace studylab::ui
