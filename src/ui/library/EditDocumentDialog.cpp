#include "ui/library/EditDocumentDialog.h"

#include "ui/components/Widgets.h"

#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStringListModel>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {

/// Complète le dernier élément d'une liste séparée par des virgules.
class TagCompleter : public QCompleter {
public:
    TagCompleter(const QStringList& tags, QObject* parent) : QCompleter(tags, parent)
    {
        setCaseSensitivity(Qt::CaseInsensitive);
        setFilterMode(Qt::MatchStartsWith);
    }
    QStringList splitPath(const QString& path) const override
    {
        return {path.section(QLatin1Char(','), -1).trimmed()};
    }
    QString pathFromIndex(const QModelIndex& index) const override
    {
        const QString current = static_cast<QLineEdit*>(widget())->text();
        const int cut = int(current.lastIndexOf(QLatin1Char(',')));
        const QString head = cut < 0 ? QString() : current.left(cut + 1) + QLatin1Char(' ');
        return head + index.data().toString();
    }
};

}  // namespace

QStringList EditDocumentDialog::splitList(const QString& text, QChar separator)
{
    QStringList out;
    for (const QString& part : text.split(separator, Qt::SkipEmptyParts))
        if (const QString t = part.simplified(); !t.isEmpty())
            out << t;
    return out;
}

EditDocumentDialog::EditDocumentDialog(const DocumentRecord& record, const QStringList& knownTags, QWidget* parent)
    : QDialog(parent), m_record(record)
{
    setWindowTitle(tr("Modifier le document"));
    setMinimumWidth(520);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(22, 20, 22, 18);
    v->setSpacing(14);
    v->addWidget(makeLabel(tr("Modifier les informations"), "h2"));

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_title = new QLineEdit(record.title);
    m_subtitle = new QLineEdit(record.subtitle);
    m_authors = new QLineEdit(record.authors.join(QStringLiteral("; ")));
    m_authors->setPlaceholderText(tr("Séparés par des points-virgules"));
    m_publisher = new QLineEdit(record.publisher);
    m_year = new QSpinBox;
    m_year->setRange(0, 2200);
    m_year->setSpecialValueText(tr("Inconnue"));
    m_year->setValue(record.year);
    m_isbn = new QLineEdit(record.isbn);
    m_doi = new QLineEdit(record.doi);
    m_language = new QLineEdit(record.language);
    m_language->setPlaceholderText(tr("fr, en…"));
    m_language->setMaximumWidth(120);
    m_tags = new QLineEdit(record.tags.join(QStringLiteral(", ")));
    m_tags->setPlaceholderText(tr("Séparés par des virgules"));
    m_tags->setCompleter(new TagCompleter(knownTags, m_tags));
    m_description = new QPlainTextEdit(record.description);
    m_description->setMinimumHeight(90);

    form->addRow(tr("Titre"), m_title);
    form->addRow(tr("Sous-titre"), m_subtitle);
    form->addRow(tr("Auteurs"), m_authors);
    form->addRow(tr("Éditeur"), m_publisher);
    form->addRow(tr("Année"), m_year);
    form->addRow(tr("ISBN"), m_isbn);
    form->addRow(tr("DOI"), m_doi);
    form->addRow(tr("Langue"), m_language);
    form->addRow(tr("Tags"), m_tags);
    form->addRow(tr("Description"), m_description);
    v->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Enregistrer"));
    buttons->button(QDialogButtonBox::Save)->setProperty("variant", QStringLiteral("primary"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);

    auto updateSave = [this, buttons] {
        buttons->button(QDialogButtonBox::Save)->setEnabled(!m_title->text().trimmed().isEmpty());
    };
    connect(m_title, &QLineEdit::textChanged, this, updateSave);
    updateSave();
}

DocumentRecord EditDocumentDialog::result() const
{
    DocumentRecord r = m_record;
    r.title = m_title->text().simplified();
    r.subtitle = m_subtitle->text().simplified();
    r.authors = splitList(m_authors->text(), QLatin1Char(';'));  // « Çengel, Yunus » reste un seul nom
    r.publisher = m_publisher->text().simplified();
    r.year = m_year->value();
    r.isbn = m_isbn->text().simplified();
    r.doi = m_doi->text().simplified();
    r.language = m_language->text().simplified().toLower();
    QStringList tags;
    for (QString t : splitList(m_tags->text(), QLatin1Char(','))) {
        if (t.startsWith(QLatin1Char('#')))
            t.remove(0, 1);
        if (!t.isEmpty())
            tags << t;
    }
    r.tags = tags;
    r.description = m_description->toPlainText().trimmed();
    return r;
}

}  // namespace studylab::ui
