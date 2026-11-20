#include "ui/study/CardEditorDialog.h"

#include "services/study/StudyService.h"
#include "ui/components/Widgets.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {
// Valeurs du sélecteur de type : basique, basique + inversée, inversée (modification seulement), à trous, formule.
enum TypeChoice { Basic, BasicAndReversed, Reversed, Cloze, Formula };
}  // namespace

CardEditorDialog::CardEditorDialog(AppContext& ctx, StudyService& study, QWidget* parent)
    : QDialog(parent), m_ctx(ctx), m_study(study)
{
    setWindowTitle(tr("Nouvelle carte"));
    setObjectName(QStringLiteral("CardEditorDialog"));
    resize(ctx.theme.px(620), ctx.theme.px(520));
    auto* v = new QVBoxLayout(this);
    v->setSpacing(10);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    m_deck = new QComboBox(this);
    m_deck->setObjectName(QStringLiteral("CardDeck"));
    m_deck->setMinimumWidth(ctx.theme.px(260));
    form->addRow(tr("Paquet"), m_deck);
    m_type = new QComboBox(this);
    m_type->setObjectName(QStringLiteral("CardType"));
    m_type->addItem(tr("Basique"), Basic);
    m_type->addItem(tr("Basique et inversée (2 cartes)"), BasicAndReversed);
    m_type->addItem(tr("Texte à trous"), Cloze);
    m_type->addItem(tr("Formule"), Formula);
    form->addRow(tr("Type"), m_type);
    v->addLayout(form);

    m_frontLabel = makeLabel(QString(), "h3", this);
    v->addWidget(m_frontLabel);
    m_front = new QPlainTextEdit(this);
    m_front->setObjectName(QStringLiteral("CardFront"));
    m_front->setTabChangesFocus(true);
    v->addWidget(m_front, 1);
    m_backLabel = makeLabel(QString(), "h3", this);
    v->addWidget(m_backLabel);
    m_back = new QPlainTextEdit(this);
    m_back->setObjectName(QStringLiteral("CardBack"));
    m_back->setTabChangesFocus(true);
    v->addWidget(m_back, 1);
    m_hint = makeLabel(QString(), "faint", this);
    m_hint->setWordWrap(true);
    v->addWidget(m_hint);
    m_sourceLabel = makeLabel(QString(), "muted", this);
    m_sourceLabel->setObjectName(QStringLiteral("CardSource"));
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->hide();
    v->addWidget(m_sourceLabel);
    m_error = makeLabel(QString(), "muted", this);
    m_error->setObjectName(QStringLiteral("CardError"));
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: %1;").arg(ctx.theme.palette().danger.name()));
    m_error->hide();
    v->addWidget(m_error);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Enregistrer"));
    buttons->button(QDialogButtonBox::Save)->setProperty("variant", QStringLiteral("primary"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (save())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);

    connect(m_type, &QComboBox::currentIndexChanged, this, &CardEditorDialog::refreshHints);
    fillDecks();
    refreshHints();
    m_front->setFocus();
}

void CardEditorDialog::fillDecks()
{
    m_deck->clear();
    if (auto decks = m_study.decks()) {
        for (const DeckSummary& d : decks.value())
            m_deck->addItem(d.deck.name, d.deck.id);
    }
    // Premier usage : un paquet est créé à l'enregistrement sous ce nom.
    if (m_deck->count() == 0) {
        m_deck->setEditable(true);
        m_deck->setEditText(tr("Mes cartes"));
    }
}

void CardEditorDialog::setDeck(qint64 deckId)
{
    const int i = m_deck->findData(deckId);
    if (i >= 0)
        m_deck->setCurrentIndex(i);
}

void CardEditorDialog::setContent(const QString& front, const QString& back)
{
    m_front->setPlainText(front);
    m_back->setPlainText(back);
}

void CardEditorDialog::setSource(CardSource source, qint64 sourceId, const QString& label)
{
    m_source = source;
    m_sourceId = sourceId;
    m_sourceLabel->setText(tr("Origine : %1").arg(label));
    m_sourceLabel->setVisible(!label.isEmpty());
}

void CardEditorDialog::editCard(const Card& card)
{
    m_editing = card;
    setWindowTitle(tr("Modifier la carte"));
    setDeck(card.deckId);
    m_type->clear();
    switch (card.type) {
    case CardType::Cloze:
        m_type->addItem(tr("Texte à trous"), Cloze);
        break;
    case CardType::Reversed:
        m_type->addItem(tr("Inversée (verso → recto)"), Reversed);
        break;
    case CardType::Basic:
    case CardType::Formula:
        m_type->addItem(tr("Basique"), Basic);
        m_type->addItem(tr("Formule"), Formula);
        m_type->setCurrentIndex(card.type == CardType::Formula ? 1 : 0);
        break;
    }
    setContent(card.front, card.back);
    refreshHints();
}

void CardEditorDialog::refreshHints()
{
    const int choice = m_type->currentData().toInt();
    m_frontLabel->setText(choice == Cloze ? tr("Texte") : tr("Recto (question)"));
    m_backLabel->setText(choice == Cloze ? tr("Remarque (facultative, montrée avec la réponse)")
                         : choice == Formula ? tr("Verso (formule LaTeX)")
                                             : tr("Verso (réponse)"));
    switch (choice) {
    case Cloze:
        m_hint->setText(tr("Entourez chaque passage à masquer : {{c1::Carnot}}, {{c2::texte::indice}}. Une carte par "
                           "numéro de trou. Markdown et formules $…$ acceptés."));
        break;
    case Formula:
        m_hint->setText(tr("La formule s'écrit en LaTeX, sans $ : \\eta = 1 - \\frac{T_F}{T_C}. Elle est affichée centrée."));
        break;
    case BasicAndReversed:
        m_hint->setText(tr("Deux cartes : recto → verso et verso → recto. Markdown et formules $…$ acceptés."));
        break;
    default:
        m_hint->setText(tr("Markdown et formules $…$ ou $$…$$ acceptés."));
        break;
    }
}

bool CardEditorDialog::save()
{
    m_error->hide();
    const int choice = m_type->currentData().toInt();
    const CardType type = choice == Cloze     ? CardType::Cloze
                          : choice == Formula ? CardType::Formula
                          : choice == Reversed ? CardType::Reversed
                                              : CardType::Basic;
    qint64 deckId = m_deck->currentData().toLongLong();
    const auto fail = [this](const QString& message) {
        m_error->setText(message);
        m_error->show();
        return false;
    };
    if (deckId <= 0) {
        // Aucun paquet : celui dont le nom est saisi est créé.
        auto created = m_study.createDeck(m_deck->currentText());
        if (!created)
            return fail(created.error().message);
        deckId = created.value().id;
        m_deck->setEditable(false);
        m_deck->clear();
        m_deck->addItem(created.value().name, deckId);
    }
    m_saved.clear();
    if (m_editing.id > 0) {
        Card c = m_editing;
        c.deckId = deckId;
        c.type = type;
        c.front = m_front->toPlainText();
        c.back = m_back->toPlainText();
        auto r = m_study.updateCard(c);
        if (!r)
            return fail(r.error().message);
        m_saved << r.value();
        return true;
    }
    auto r = m_study.addCards(deckId, type, m_front->toPlainText(), m_back->toPlainText(), m_source, m_sourceId,
                              choice == BasicAndReversed);
    if (!r)
        return fail(r.error().message);
    m_saved = r.value();
    return true;
}

}  // namespace studylab::ui
