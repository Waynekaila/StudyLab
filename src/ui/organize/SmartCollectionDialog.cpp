#include "ui/organize/SmartCollectionDialog.h"

#include "services/organize/OrganizeService.h"
#include "ui/components/Widgets.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {

const QList<SmartRule::Field> kFields{SmartRule::Field::Subject,  SmartRule::Field::Status,
                                      SmartRule::Field::Format,   SmartRule::Field::Tag,
                                      SmartRule::Field::Favorite, SmartRule::Field::AddedWithin,
                                      SmartRule::Field::AnnotatedWithin, SmartRule::Field::OpenedWithin};

/// Valeurs possibles d'un champ à liste : (clé, libellé).
QList<QPair<QString, QString>> choices(SmartRule::Field f, const QList<SubjectRecord>& subjects,
                                       const OrganizeService& org)
{
    QList<QPair<QString, QString>> out;
    switch (f) {
    case SmartRule::Field::Subject:
        for (const SubjectRecord& s : subjects)
            out << qMakePair(s.uuid, org.subjectPath(s.id, subjects));
        break;
    case SmartRule::Field::Status:
        for (ReadingStatus s : allReadingStatuses())
            out << qMakePair(readingStatusKey(s), readingStatusLabel(s));
        break;
    case SmartRule::Field::Format:
        for (DocumentFormat d : {DocumentFormat::Pdf, DocumentFormat::Epub})
            out << qMakePair(documentFormatKey(d), documentFormatLabel(d));
        break;
    default:
        break;
    }
    return out;
}

bool isList(SmartRule::Field f)
{
    return f == SmartRule::Field::Subject || f == SmartRule::Field::Status || f == SmartRule::Field::Format;
}

QString valueLabel(SmartRule::Field f, const QString& key, const QList<SubjectRecord>& subjects)
{
    switch (f) {
    case SmartRule::Field::Subject:
        for (const SubjectRecord& s : subjects)
            if (s.uuid == key)
                return s.name;
        return QObject::tr("(matière supprimée)");
    case SmartRule::Field::Status:
        if (auto s = readingStatusFromKey(key))
            return readingStatusLabel(*s);
        return key;
    case SmartRule::Field::Format:
        if (auto d = documentFormatFromKey(key))
            return documentFormatLabel(*d);
        return key;
    default:
        return key;
    }
}

}  // namespace

QString smartOpLabel(SmartRule::Field field, SmartRule::Op op)
{
    const bool is = op == SmartRule::Op::Is;
    if (smartFieldIsPeriod(field))
        return is ? QObject::tr("depuis moins de") : QObject::tr("il y a plus de");
    if (smartFieldIsFlag(field))
        return is ? QObject::tr("oui") : QObject::tr("non");
    return is ? QObject::tr("est") : QObject::tr("n'est pas");
}

QString smartQuerySummary(const SmartQuery& query, const QList<SubjectRecord>& subjects)
{
    if (query.rules.isEmpty())
        return QObject::tr("Tous les documents de la bibliothèque");
    QStringList parts;
    for (const SmartRule& r : query.rules) {
        QString text = smartFieldLabel(r.field) + QLatin1Char(' ') + smartOpLabel(r.field, r.op);
        if (smartFieldIsPeriod(r.field)) {
            const int days = r.values.value(0).toInt();
            text += QLatin1Char(' ') + (days == 1 ? QObject::tr("1 jour") : QObject::tr("%1 jours").arg(days));
        } else if (!smartFieldIsFlag(r.field)) {
            QStringList labels;
            for (const QString& v : r.values)
                labels << valueLabel(r.field, v, subjects);
            text += QLatin1Char(' ') + (labels.isEmpty() ? QObject::tr("(rien)") : labels.join(QStringLiteral(", ")));
        }
        parts << text;
    }
    return parts.join(query.matchAll ? QStringLiteral(" · ") : QObject::tr(" · ou "));
}

struct SmartCollectionDialog::Row {
    QWidget* widget = nullptr;
    QComboBox* field = nullptr;
    QComboBox* op = nullptr;
    QHBoxLayout* valueHost = nullptr;
    QWidget* editor = nullptr;
    QToolButton* list = nullptr;
    QLineEdit* tags = nullptr;
    QSpinBox* days = nullptr;
    QStringList selected;

    SmartRule::Field currentField() const { return kFields.value(field->currentIndex()); }
    SmartRule rule() const
    {
        SmartRule r;
        r.field = currentField();
        r.op = op->currentIndex() == 1 ? SmartRule::Op::IsNot : SmartRule::Op::Is;
        if (isList(r.field)) {
            r.values = selected;
        } else if (r.field == SmartRule::Field::Tag && tags) {
            for (const QString& t : tags->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
                if (!t.trimmed().isEmpty())
                    r.values << t.trimmed().remove(QLatin1Char('#'));
        } else if (smartFieldIsPeriod(r.field) && days) {
            r.values << QString::number(days->value());
        }
        return r;
    }
};

SmartCollectionDialog::SmartCollectionDialog(AppContext& ctx, qint64 collectionId, QWidget* parent)
    : QDialog(parent), m_ctx(ctx), m_id(collectionId)
{
    setObjectName(QStringLiteral("SmartCollectionDialog"));
    setWindowTitle(collectionId > 0 ? tr("Modifier la collection intelligente") : tr("Nouvelle collection intelligente"));
    setMinimumWidth(m_ctx.theme.px(560));
    if (auto s = m_ctx.organize.subjects())
        m_subjects = s.value();

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(20, 18, 20, 16);
    v->setSpacing(12);
    auto* head = new QHBoxLayout;
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::get(QStringLiteral("sparkle"), m_ctx.theme.palette().accPurple, 18).pixmap(18, 18));
    head->addWidget(icon);
    head->addWidget(makeLabel(tr("Collection intelligente"), "h2", this), 1);
    v->addLayout(head);

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("SmartName"));
    m_name->setPlaceholderText(tr("Nom (« Thermo non terminés »)"));
    m_name->setAccessibleName(tr("Nom de la collection"));
    v->addWidget(m_name);

    auto* match = new QHBoxLayout;
    match->addWidget(makeLabel(tr("Documents qui respectent"), "muted", this));
    m_match = new QComboBox(this);
    m_match->addItems({tr("toutes les règles"), tr("au moins une règle")});
    m_match->setAccessibleName(tr("Règles à respecter"));
    connect(m_match, &QComboBox::currentIndexChanged, this, &SmartCollectionDialog::updateResult);
    match->addWidget(m_match);
    match->addStretch(1);
    v->addLayout(match);

    m_rules = new QVBoxLayout;
    m_rules->setSpacing(8);
    v->addLayout(m_rules);

    auto* add = new QPushButton(tr("+ Ajouter une règle"), this);
    add->setObjectName(QStringLiteral("AddRuleButton"));
    add->setProperty("variant", QStringLiteral("ghost"));
    add->setCursor(Qt::PointingHandCursor);
    connect(add, &QPushButton::clicked, this, [this] { addRule({SmartRule::Field::Status, SmartRule::Op::Is, {}}); });
    v->addWidget(add, 0, Qt::AlignLeft);
    v->addStretch(1);

    auto* foot = new QHBoxLayout;
    m_result = makeLabel(QString(), "muted", this);
    m_result->setObjectName(QStringLiteral("SmartResult"));
    foot->addWidget(m_result, 1);
    auto* cancel = new QPushButton(tr("Annuler"), this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    foot->addWidget(cancel);
    m_save = new QPushButton(tr("Enregistrer"), this);
    m_save->setProperty("variant", QStringLiteral("primary"));
    m_save->setDefault(true);
    connect(m_save, &QPushButton::clicked, this, [this] {
        if (save() > 0)
            accept();
    });
    foot->addWidget(m_save);
    v->addLayout(foot);
    connect(m_name, &QLineEdit::textChanged, this, [this] { m_save->setEnabled(!name().isEmpty()); });

    if (collectionId > 0) {
        if (auto c = m_ctx.organize.collection(collectionId)) {
            m_name->setText(c.value().name);
            m_match->setCurrentIndex(c.value().query.matchAll ? 0 : 1);
            for (const SmartRule& r : c.value().query.rules)
                addRule(r);
        }
    } else {
        // Point de départ de la maquette : documents pas encore terminés
        addRule({SmartRule::Field::Status, SmartRule::Op::IsNot,
                 {readingStatusKey(ReadingStatus::Finished), readingStatusKey(ReadingStatus::Archived)}});
    }
    m_save->setEnabled(!name().isEmpty());
    updateResult();
}

SmartCollectionDialog::~SmartCollectionDialog()
{
    qDeleteAll(m_rows);
}

QString SmartCollectionDialog::name() const
{
    return m_name->text().simplified();
}

SmartQuery SmartCollectionDialog::query() const
{
    SmartQuery q;
    q.matchAll = m_match->currentIndex() == 0;
    for (const Row* r : m_rows)
        q.rules << r->rule();
    return q;
}

void SmartCollectionDialog::addRule(const SmartRule& rule)
{
    auto* row = new Row;
    row->widget = new QWidget(this);
    row->widget->setObjectName(QStringLiteral("SmartRuleRow"));
    auto* h = new QHBoxLayout(row->widget);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    row->field = new QComboBox(row->widget);
    row->field->setAccessibleName(tr("Champ"));
    for (SmartRule::Field f : kFields)
        row->field->addItem(smartFieldLabel(f));
    row->field->setCurrentIndex(int(kFields.indexOf(rule.field)));
    h->addWidget(row->field);
    row->op = new QComboBox(row->widget);
    row->op->setAccessibleName(tr("Condition"));
    h->addWidget(row->op);
    row->valueHost = new QHBoxLayout;
    h->addLayout(row->valueHost, 1);
    auto* remove = new QToolButton(row->widget);
    remove->setProperty("readerTool", true);
    remove->setIcon(Icons::get(QStringLiteral("x"), m_ctx.theme.palette().text2, 16));
    remove->setToolTip(tr("Retirer la règle"));
    remove->setAccessibleName(tr("Retirer la règle"));
    h->addWidget(remove);
    row->selected = rule.values;
    m_rows << row;
    m_rules->addWidget(row->widget);

    rebuildValueEditor(row);
    row->op->setCurrentIndex(rule.op == SmartRule::Op::IsNot ? 1 : 0);
    if (row->tags)
        row->tags->setText(rule.values.join(QStringLiteral(", ")));
    if (row->days && !rule.values.isEmpty())
        row->days->setValue(rule.values.first().toInt());

    connect(row->field, &QComboBox::currentIndexChanged, this, [this, row] {
        row->selected.clear();
        rebuildValueEditor(row);
        updateResult();
    });
    connect(row->op, &QComboBox::currentIndexChanged, this, &SmartCollectionDialog::updateResult);
    connect(remove, &QToolButton::clicked, this, [this, row] { removeRule(int(m_rows.indexOf(row))); });
    updateResult();
}

void SmartCollectionDialog::removeRule(int index)
{
    if (index < 0 || index >= m_rows.size())
        return;
    Row* row = m_rows.takeAt(index);
    row->widget->deleteLater();
    delete row;
    updateResult();
}

void SmartCollectionDialog::rebuildValueEditor(Row* row)
{
    const SmartRule::Field f = row->currentField();
    {
        const QSignalBlocker block(row->op);
        const int op = row->op->currentIndex();
        row->op->clear();
        row->op->addItems({smartOpLabel(f, SmartRule::Op::Is), smartOpLabel(f, SmartRule::Op::IsNot)});
        row->op->setCurrentIndex(qMax(0, op));
    }
    if (row->editor)
        row->editor->deleteLater();
    row->editor = nullptr;
    row->list = nullptr;
    row->tags = nullptr;
    row->days = nullptr;

    if (isList(f)) {
        row->list = new QToolButton(row->widget);
        row->list->setObjectName(QStringLiteral("SmartValues"));
        row->list->setPopupMode(QToolButton::InstantPopup);
        row->list->setToolButtonStyle(Qt::ToolButtonTextOnly);
        row->list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* menu = new QMenu(row->list);
        const auto opts = choices(f, m_subjects, m_ctx.organize);
        const auto refreshText = [this, row, f] {
            QStringList labels;
            for (const QString& k : row->selected)
                labels << valueLabel(f, k, m_subjects);
            row->list->setText(labels.isEmpty() ? tr("Choisir…") : labels.join(QStringLiteral(", ")));
        };
        for (const auto& [key, label] : opts) {
            QAction* a = menu->addAction(label);
            a->setCheckable(true);
            a->setChecked(row->selected.contains(key));
            a->setData(key);
            connect(a, &QAction::toggled, this, [this, row, key, refreshText](bool on) {
                if (on && !row->selected.contains(key))
                    row->selected << key;
                else if (!on)
                    row->selected.removeAll(key);
                refreshText();
                updateResult();
            });
        }
        if (opts.isEmpty())
            menu->addAction(f == SmartRule::Field::Subject ? tr("Aucune matière pour l'instant") : tr("Aucun choix"))->setEnabled(false);
        row->list->setMenu(menu);
        refreshText();
        row->editor = row->list;
    } else if (f == SmartRule::Field::Tag) {
        row->tags = new QLineEdit(row->widget);
        row->tags->setPlaceholderText(tr("examen, à-revoir"));
        row->tags->setAccessibleName(tr("Tags, séparés par des virgules"));
        connect(row->tags, &QLineEdit::textChanged, this, &SmartCollectionDialog::updateResult);
        row->editor = row->tags;
    } else if (smartFieldIsPeriod(f)) {
        row->days = new QSpinBox(row->widget);
        row->days->setRange(1, 3650);
        row->days->setValue(f == SmartRule::Field::AddedWithin ? 90 : 7);
        row->days->setSuffix(tr(" jours"));
        row->days->setAccessibleName(tr("Nombre de jours"));
        connect(row->days, &QSpinBox::valueChanged, this, &SmartCollectionDialog::updateResult);
        row->editor = row->days;
    }
    if (row->editor)
        row->valueHost->addWidget(row->editor);
    else
        row->valueHost->addStretch(1);
}

void SmartCollectionDialog::updateResult()
{
    auto docs = m_ctx.organize.evaluate(query());
    const int n = docs ? int(docs.value().size()) : 0;
    m_result->setText(n == 1 ? tr("1 résultat") : tr("%1 résultats").arg(n));
}

qint64 SmartCollectionDialog::save()
{
    if (name().isEmpty())
        return 0;
    if (m_id > 0) {
        auto renamed = m_ctx.organize.renameCollection(m_id, name());
        auto set = renamed ? m_ctx.organize.setQuery(m_id, query()) : renamed;
        if (!set) {
            QMessageBox::warning(this, windowTitle(), set.error().message);
            return 0;
        }
        return m_id;
    }
    auto created = m_ctx.organize.createCollection(name(), CollectionKind::Smart, query());
    if (!created) {
        QMessageBox::warning(this, windowTitle(), created.error().message);
        return 0;
    }
    m_id = created.value().id;
    return m_id;
}

}  // namespace studylab::ui
