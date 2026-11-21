#include "ui/study/RevisionsPage.h"
#include "ui/shell/Shortcuts.h"

#include "services/study/StudyService.h"
#include "ui/components/PanelSplitter.h"
#include "ui/components/Widgets.h"
#include "ui/pages/PageScaffold.h"
#include "ui/reader/MarkdownView.h"
#include "ui/study/CardEditorDialog.h"
#include "ui/study/DeckSettingsDialog.h"
#include "ui/study/RevisionSheetDialog.h"
#include "services/annotations/AnnotationService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonObject>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace studylab::ui {

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kDueRole = Qt::UserRole + 1;

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

/// Texte d'aperçu d'une carte dans la liste : une ligne, trous révélés, Markdown léger retiré.
QString preview(const Card& card)
{
    QString text = card.type == CardType::Reversed ? card.back : card.front;
    static const QRegularExpression cloze(QStringLiteral("\\{\\{c\\d+::(.*?)(?:::[^}]*)?\\}\\}"));
    text.replace(cloze, QStringLiteral("\\1"));
    text.remove(QRegularExpression(QStringLiteral("[*`#>]")));
    text = text.simplified();
    return text.size() > 140 ? text.left(140) + QStringLiteral("…") : text;
}

QString typeLabel(CardType type)
{
    switch (type) {
    case CardType::Basic:
        return RevisionsPage::tr("Basique");
    case CardType::Reversed:
        return RevisionsPage::tr("Inversée");
    case CardType::Cloze:
        return RevisionsPage::tr("Texte à trous");
    case CardType::Formula:
        return RevisionsPage::tr("Formule");
    }
    return {};
}

QString stateLabel(const db::CardRow& row)
{
    if (row.card.suspended)
        return RevisionsPage::tr("Suspendue");
    switch (row.state.state) {
    case CardState::New:
        return RevisionsPage::tr("Nouvelle");
    case CardState::Learning:
        return RevisionsPage::tr("En apprentissage");
    case CardState::Review:
        return RevisionsPage::tr("En révision");
    case CardState::Relearning:
        return RevisionsPage::tr("À réapprendre");
    }
    return {};
}

/// Ligne de la liste des paquets : nom, nombre de cartes à voir aujourd'hui dans une pastille.
class DeckDelegate : public QStyledItemDelegate {
public:
    DeckDelegate(ThemeManager& theme, QObject* parent) : QStyledItemDelegate(parent), m_theme(theme) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        return {option.rect.width(), m_theme.px(40)};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const Palette& p = m_theme.palette();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(option.rect).adjusted(0, 2, 0, -2);
        const bool selected = option.state & QStyle::State_Selected;
        if (selected || (option.state & QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? p.primarySoft : p.sunken);
            painter->drawRoundedRect(r, 8, 8);
        }
        const int due = index.data(kDueRole).toInt();
        const QString count = QString::number(due);
        QFont f = option.font;
        QFontMetrics fm(f);
        const int pad = m_theme.px(10);
        const int pillW = std::max(fm.horizontalAdvance(count) + m_theme.px(14), m_theme.px(24));
        const QRectF pill(r.right() - pad - pillW, r.center().y() - m_theme.px(10), pillW, m_theme.px(20));
        if (due > 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? p.surface : p.lavenderSoft);
            painter->drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        }
        QFont bold = f;
        bold.setWeight(QFont::DemiBold);
        painter->setFont(due > 0 ? bold : f);
        painter->setPen(due > 0 ? (selected ? p.primaryStrong : (p.dark ? p.lavender : p.lavender.darker(150))) : p.text3);
        painter->drawText(pill, Qt::AlignCenter, count);
        painter->setFont(selected ? bold : f);
        painter->setPen(selected ? p.primaryStrong : p.text);
        const QRectF textRect(r.left() + pad + m_theme.px(4), r.top(), pill.left() - r.left() - 2 * pad, r.height());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          fm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, int(textRect.width())));
        painter->restore();
    }

private:
    ThemeManager& m_theme;
};

/// Bouton de réponse : libellé coloré et intervalle suivant, avec la touche.
QPushButton* makeRateButton(const QString& title, int rating, QLabel** interval, int t_minHeight, QWidget* parent)
{
    auto* b = new QPushButton(parent);
    b->setObjectName(QStringLiteral("RateButton"));
    b->setProperty("rating", rating);
    b->setAccessibleName(title);
    b->setCursor(Qt::PointingHandCursor);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* v = new QVBoxLayout(b);
    b->setMinimumHeight(t_minHeight);
    v->setContentsMargins(10, 8, 10, 8);
    v->setSpacing(2);
    auto* t = new QLabel(title, b);
    t->setObjectName(QStringLiteral("RateTitle"));
    t->setProperty("rating", rating);
    t->setAlignment(Qt::AlignCenter);
    t->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* i = makeLabel(QString(), "faint", b);
    i->setAlignment(Qt::AlignCenter);
    i->setAttribute(Qt::WA_TransparentForMouseEvents);
    v->addWidget(t);
    v->addWidget(i);
    *interval = i;
    return b;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------

DayChart::DayChart(AppContext& ctx, QWidget* parent) : QWidget(parent), m_ctx(ctx)
{
    setObjectName(QStringLiteral("DayChart"));
    setMinimumHeight(ctx.theme.px(56));
}

void DayChart::setValues(const QList<int>& values)
{
    m_values = values;
    QStringList parts;
    for (int v : values)
        parts << QString::number(v);
    setAccessibleDescription(tr("Révisions des 7 derniers jours : %1").arg(parts.join(QStringLiteral(", "))));
    update();
}

QSize DayChart::sizeHint() const { return {m_ctx.theme.px(260), m_ctx.theme.px(56)}; }

void DayChart::paintEvent(QPaintEvent*)
{
    if (m_values.isEmpty())
        return;
    const Palette& p = m_ctx.theme.palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int n = int(m_values.size());
    const int max = std::max(1, *std::max_element(m_values.begin(), m_values.end()));
    const qreal gap = m_ctx.theme.px(6);
    const qreal w = (width() - gap * (n - 1)) / n;
    const qreal h = height();
    const qreal minH = m_ctx.theme.px(4);
    for (int i = 0; i < n; ++i) {
        const qreal bh = std::max(minH, h * m_values[i] / max);
        const QRectF bar(i * (w + gap), h - bh, w, bh);
        painter.setPen(Qt::NoPen);
        painter.setBrush(i == n - 1 ? p.lavender : (p.dark ? p.surface2.lighter(130) : p.sunken));
        painter.drawRoundedRect(bar, 3, 3);
    }
}

// ---------------------------------------------------------------------------------------------------------------

RevisionsPage::RevisionsPage(AppContext& ctx, StudyService& study, QWidget* parent)
    : QWidget(parent), m_ctx(ctx), m_study(study)
{
    setObjectName(QStringLiteral("RevisionsPage"));
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    m_panels = new PanelSplitter(ctx.settings, ctx.theme, QStringLiteral("revisions"), this);
    h->addWidget(m_panels, 1);
    buildNav();
    m_center = new QStackedWidget(this);
    m_panels->addWorkArea(m_center);

    // Aucun paquet
    m_empty = new QWidget(m_center);
    auto* ev = new QVBoxLayout(m_empty);
    ev->addStretch(1);
    auto* et = makeLabel(tr("Retenez ce que vous lisez"), "h2", m_empty);
    et->setAlignment(Qt::AlignCenter);
    ev->addWidget(et);
    auto* ed = makeLabel(tr("Créez des cartes depuis vos annotations (menu d'une annotation), vos notes ou ici. "
                            "StudyLab vous les représente au bon moment : souvent au début, puis de plus en plus "
                            "espacées."),
                         "muted", m_empty);
    ed->setWordWrap(true);
    ed->setAlignment(Qt::AlignCenter);
    ed->setMaximumWidth(ctx.theme.px(480));
    // Centré par une rangée, pas par un alignement : un texte replié aligné perd sa hauteur et sa fin est coupée.
    auto* edRow = new QHBoxLayout;
    edRow->addStretch(1);
    edRow->addWidget(ed, 6);
    edRow->addStretch(1);
    ev->addLayout(edRow);
    auto* eb = new QPushButton(tr("Créer un paquet"), m_empty);
    eb->setObjectName(QStringLiteral("CreateFirstDeck"));
    eb->setProperty("variant", QStringLiteral("primary"));
    connect(eb, &QPushButton::clicked, this, &RevisionsPage::chooseNewDeck);
    ev->addWidget(eb, 0, Qt::AlignHCenter);
    ev->addStretch(2);
    m_center->addWidget(m_empty);

    buildOverview();
    buildSession();
    buildFinished();

    connect(&m_study, &StudyService::changed, this, [this] {
        if (m_inSession)
            return;  // la séance rafraîchit elle-même à sa fin
        m_stale = true;
        if (isVisible())
            refresh();
    });
    connect(&m_ctx.theme, &ThemeManager::themeChanged, this, [this] {
        pushTheme();
        m_deckList->viewport()->update();
        m_chart->update();
    });
    m_stale = true;  // calculé à chaque affichage (showEvent) : rien à faire au démarrage
}

void RevisionsPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Le jour de révision a pu changer depuis le dernier affichage : compteurs toujours recalculés.
    if (!m_inSession)
        refresh();
}

void RevisionsPage::buildNav()
{
    ThemeManager& theme = m_ctx.theme;
    auto* nav = new QFrame(this);
    nav->setObjectName(QStringLiteral("NotesNav"));
    auto* v = new QVBoxLayout(nav);
    v->setContentsMargins(12, 14, 12, 12);
    v->setSpacing(10);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(6, 0, 0, 0);
    head->addWidget(makeLabel(tr("Paquets"), "h2", nav), 1);
    auto* add = new QToolButton(nav);
    add->setObjectName(QStringLiteral("NewDeckButton"));
    add->setProperty("readerTool", true);
    add->setIcon(Icons::get(QStringLiteral("plus"), theme.palette().text2, 18));
    add->setToolTip(tr("Nouveau paquet"));
    add->setAccessibleName(add->toolTip());
    connect(add, &QToolButton::clicked, this, &RevisionsPage::chooseNewDeck);
    head->addWidget(add);
    m_navHead = head;
    v->addLayout(head);

    m_deckList = new QListWidget(nav);
    m_deckList->setObjectName(QStringLiteral("DeckList"));
    m_deckList->setAccessibleName(tr("Paquets"));
    m_deckList->setItemDelegate(new DeckDelegate(theme, m_deckList));
    m_deckList->setMouseTracking(true);
    m_deckList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_deckList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_deckList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (!m_filling && item && item->data(kIdRole).toLongLong() != m_deckId)
            showDeck(item->data(kIdRole).toLongLong());
    });
    connect(m_deckList, &QListWidget::customContextMenuRequested, this, &RevisionsPage::showDeckMenu);
    v->addWidget(m_deckList, 1);

    // Aujourd'hui (tous paquets)
    auto* today = makeCard(nav);
    today->setObjectName(QStringLiteral("TodayCard"));
    auto* tv = qobject_cast<QVBoxLayout*>(today->layout());
    tv->setContentsMargins(16, 14, 16, 14);
    tv->setSpacing(8);
    tv->addWidget(makeLabel(tr("Aujourd'hui"), "h3", today));
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);
    const QStringList names = {tr("Révisées"), tr("Restantes"), tr("Nouvelles"), tr("Réussite")};
    for (int i = 0; i < names.size(); ++i) {
        grid->addWidget(makeLabel(names[i], "muted", today), i, 0);
        auto* value = makeLabel(QStringLiteral("0"), "h3", today);
        value->setObjectName(QStringLiteral("TodayStat%1").arg(i));
        grid->addWidget(value, i, 1);
        m_stats << value;
    }
    grid->setColumnStretch(1, 1);
    tv->addLayout(grid);
    tv->addSpacing(6);
    m_chart = new DayChart(m_ctx, today);
    tv->addWidget(m_chart);
    tv->addWidget(makeLabel(tr("7 derniers jours"), "faint", today));
    v->addWidget(today);

    m_sheet = new QPushButton(Icons::get(QStringLiteral("note"), theme.palette().text2, 16),
                              tr("Générer une fiche de révision"), nav);
    m_sheet->setObjectName(QStringLiteral("GenerateSheet"));
    m_sheet->setToolTip(tr("Brouillon de note tiré des annotations et des notes d'une matière, à relire"));
    connect(m_sheet, &QPushButton::clicked, this, [this] { openSheetDialog(); });
    v->addWidget(m_sheet);
    m_panels->addPanel(nav, tr("Paquets"), theme.px(260), theme.px(200), theme.px(440));
    if (m_navHead)
        m_navHead->addWidget(m_panels->makeCollapseButton(nav, nav));
}

void RevisionsPage::buildOverview()
{
    auto* scaffold = new PageScaffold(m_center, 860);
    m_overview = scaffold;
    QVBoxLayout* body = scaffold->body();
    body->setSpacing(14);

    body->addWidget(makeLabel(tr("PAQUET"), "caption"));
    m_deckName = makeLabel(QString(), "h1");
    m_deckName->setObjectName(QStringLiteral("DeckTitle"));
    m_deckName->setWordWrap(true);
    body->addWidget(m_deckName);
    m_deckInfo = makeLabel(QString(), "muted");
    body->addWidget(m_deckInfo);

    auto* tiles = new QHBoxLayout;
    tiles->setSpacing(12);
    const QStringList names = {tr("Nouvelles"), tr("En apprentissage"), tr("À réviser")};
    for (const QString& name : names) {
        auto* card = makeCard();
        auto* cv = qobject_cast<QVBoxLayout*>(card->layout());
        cv->setContentsMargins(18, 14, 18, 14);
        cv->setSpacing(2);
        auto* value = makeLabel(QStringLiteral("0"), "big", card);
        m_tiles << value;
        cv->addWidget(value);
        cv->addWidget(makeLabel(name, "muted", card));
        tiles->addWidget(card, 1);
    }
    body->addLayout(tiles);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    m_start = new QPushButton(tr("Commencer la révision"), m_overview);
    m_start->setObjectName(QStringLiteral("StartReview"));
    m_start->setProperty("variant", QStringLiteral("primary"));
    connect(m_start, &QPushButton::clicked, this, [this] { startSession(); });
    m_addCard = new QPushButton(Icons::get(QStringLiteral("plus"), m_ctx.theme.palette().text2, 16),
                                tr("Ajouter une carte"), m_overview);
    m_addCard->setObjectName(QStringLiteral("AddCard"));
    connect(m_addCard, &QPushButton::clicked, this, [this] { addCard(); });
    m_settings = new QPushButton(Icons::get(QStringLiteral("settings"), m_ctx.theme.palette().text2, 16),
                                 tr("Réglages du paquet"), m_overview);
    m_settings->setObjectName(QStringLiteral("DeckSettings"));
    m_settings->setProperty("variant", QStringLiteral("ghost"));
    connect(m_settings, &QPushButton::clicked, this, [this] { openDeckSettings(m_deckId); });
    actions->addWidget(m_start);
    actions->addWidget(m_addCard);
    actions->addStretch(1);
    actions->addWidget(m_settings);
    body->addLayout(actions);

    body->addSpacing(8);
    body->addWidget(makeLabel(tr("Cartes"), "h2"));
    m_cardsEmpty = makeLabel(tr("Aucune carte. Ajoutez-en une ici, ou depuis une annotation (clic droit › Créer une "
                                "carte) ou une note."),
                             "muted");
    m_cardsEmpty->setWordWrap(true);
    body->addWidget(m_cardsEmpty);
    m_cards = new QTreeWidget(m_overview);
    m_cards->setObjectName(QStringLiteral("CardList"));
    m_cards->setAccessibleName(tr("Cartes du paquet"));
    m_cards->setRootIsDecorated(false);
    m_cards->setUniformRowHeights(true);
    m_cards->setAlternatingRowColors(false);
    m_cards->setHeaderLabels({tr("Question"), tr("Type"), tr("État"), tr("Prochaine révision")});
    m_cards->header()->setStretchLastSection(false);
    m_cards->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        m_cards->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_cards->setMinimumHeight(m_ctx.theme.px(260));
    m_cards->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_cards, &QTreeWidget::customContextMenuRequested, this, &RevisionsPage::showCardMenu);
    connect(m_cards, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item) { editCard(item->data(0, kIdRole).toLongLong()); });
    body->addWidget(m_cards, 1);
    m_center->addWidget(m_overview);
}

void RevisionsPage::buildSession()
{
    ThemeManager& theme = m_ctx.theme;
    m_session = new QWidget(m_center);
    m_session->setObjectName(QStringLiteral("StudySession"));
    m_session->setFocusPolicy(Qt::StrongFocus);
    auto* outer = new QHBoxLayout(m_session);
    outer->addStretch(1);
    auto* column = new QWidget(m_session);
    column->setMaximumWidth(theme.px(900));
    column->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    outer->addWidget(column, 8);
    outer->addStretch(1);
    auto* v = new QVBoxLayout(column);
    v->setContentsMargins(0, 28, 0, 20);
    v->setSpacing(16);
    v->addStretch(1);

    auto* head = new QHBoxLayout;
    head->setSpacing(14);
    m_sessionDeck = makeLabel(QString(), "muted", column);
    head->addWidget(m_sessionDeck);
    m_progress = new QProgressBar(column);
    m_progress->setObjectName(QStringLiteral("StudyProgress"));
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    head->addWidget(m_progress, 1);
    m_progressLabel = makeLabel(QString(), "muted", column);
    m_progressLabel->setObjectName(QStringLiteral("StudyProgressLabel"));
    head->addWidget(m_progressLabel);
    auto* close = new QToolButton(column);
    close->setObjectName(QStringLiteral("CloseSession"));
    close->setProperty("readerTool", true);
    close->setIcon(Icons::get(QStringLiteral("x"), theme.palette().text2, 16));
    close->setToolTip(tr("Terminer la séance (Échap)"));
    close->setAccessibleName(close->toolTip());
    connect(close, &QToolButton::clicked, this, &RevisionsPage::closeSession);
    head->addWidget(close);
    v->addLayout(head);

    auto* frame = makeCard(column);
    frame->setObjectName(QStringLiteral("FlashcardFrame"));
    m_cardHost = qobject_cast<QVBoxLayout*>(frame->layout());
    m_cardHost->setContentsMargins(6, 6, 6, 6);
    v->addWidget(frame);

    m_actions = new QStackedWidget(column);
    m_actions->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* revealPage = new QWidget(m_actions);
    auto* rh = new QHBoxLayout(revealPage);
    rh->setContentsMargins(0, 0, 0, 0);
    m_showAnswer = new QPushButton(tr("Afficher la réponse"), revealPage);
    m_showAnswer->setObjectName(QStringLiteral("ShowAnswer"));
    m_showAnswer->setProperty("variant", QStringLiteral("primary"));
    m_showAnswer->setMinimumWidth(theme.px(240));
    m_showAnswer->setMinimumHeight(theme.px(44));
    connect(m_showAnswer, &QPushButton::clicked, this, [this] { revealAnswer(); });
    rh->addStretch(1);
    rh->addWidget(m_showAnswer);
    rh->addStretch(1);
    m_actions->addWidget(revealPage);
    auto* ratePage = new QWidget(m_actions);
    auto* rr = new QHBoxLayout(ratePage);
    rr->setContentsMargins(theme.px(40), 0, theme.px(40), 0);
    rr->setSpacing(12);
    const QStringList titles = {tr("À revoir"), tr("Difficile"), tr("Bien"), tr("Facile")};
    for (int i = 0; i < 4; ++i) {
        QLabel* interval = nullptr;
        auto* b = makeRateButton(titles[i], i + 1, &interval, theme.px(56), ratePage);
        connect(b, &QPushButton::clicked, this, [this, i] { rate(Rating(i + 1)); });
        m_rateButtons << b;
        m_rateIntervals << interval;
        rr->addWidget(b);
    }
    m_actions->addWidget(ratePage);
    m_actions->setFixedHeight(theme.px(60));
    v->addWidget(m_actions);

    auto* hints = makeLabel(QString(), "faint", column);
    hints->setObjectName(QStringLiteral("SessionKeyHints"));
    Shortcuts::onChanged(m_ctx.settings, hints, [this, hints] {
        const auto k = [this](const char* id) {
            const QString t = Shortcuts::keyText(m_ctx.settings, QString::fromLatin1(id));
            return t.isEmpty() ? QStringLiteral("—") : t;
        };
        hints->setText(tr("%1 : retourner · %2 %3 %4 %5 : répondre · %6 : modifier la carte · %7 : suspendre · "
                          "Échap : terminer")
                           .arg(k("revisions.reveal"), k("revisions.again"), k("revisions.hard"), k("revisions.good"),
                                k("revisions.easy"), k("revisions.edit"), k("revisions.suspend")));
    });
    hints->setAlignment(Qt::AlignCenter);
    v->addWidget(hints);
    v->addStretch(2);

    const auto shortcut = [this](const QKeySequence& key, auto slot) {
        auto* s = new QShortcut(key, m_session);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, slot);
    };
    const auto configurable = [this](const char* id, auto slot) {
        auto* s = Shortcuts::create(m_ctx.settings, QString::fromLatin1(id), m_session);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, slot);
    };
    configurable("revisions.reveal", [this] { revealAnswer(); });
    shortcut(QKeySequence(Qt::Key_Return), [this] {
        if (!revealAnswer())
            rate(Rating::Good);
    });
    shortcut(QKeySequence(Qt::Key_Enter), [this] {
        if (!revealAnswer())
            rate(Rating::Good);
    });
    static const char* const rates[] = {"revisions.again", "revisions.hard", "revisions.good", "revisions.easy"};
    for (int i = 0; i < 4; ++i)
        configurable(rates[i], [this, i] { rate(Rating(i + 1)); });
    configurable("revisions.edit", [this] {
        if (m_inSession)
            editCard(m_row.card.id);
    });
    configurable("revisions.suspend", [this] {
        if (m_inSession && m_study.setSuspended(m_row.card.id, true))
            nextCard();
    });
    shortcut(QKeySequence(Qt::Key_Escape), [this] { closeSession(); });
    m_center->addWidget(m_session);
}

void RevisionsPage::buildFinished()
{
    m_finished = new QWidget(m_center);
    m_finished->setObjectName(QStringLiteral("StudyFinished"));
    auto* v = new QVBoxLayout(m_finished);
    v->addStretch(1);
    auto* t = makeLabel(tr("Séance terminée"), "h1", m_finished);
    t->setAlignment(Qt::AlignCenter);
    v->addWidget(t);
    m_finishedSummary = makeLabel(QString(), "muted", m_finished);
    m_finishedSummary->setObjectName(QStringLiteral("FinishedSummary"));
    m_finishedSummary->setAlignment(Qt::AlignCenter);
    v->addWidget(m_finishedSummary);
    m_finishedNext = makeLabel(QString(), "faint", m_finished);
    m_finishedNext->setAlignment(Qt::AlignCenter);
    m_finishedNext->setWordWrap(true);
    v->addWidget(m_finishedNext);
    v->addSpacing(10);
    auto* back = new QPushButton(tr("Retour au paquet"), m_finished);
    back->setObjectName(QStringLiteral("BackToDeck"));
    connect(back, &QPushButton::clicked, this, [this] {
        m_center->setCurrentWidget(m_overview);
        refresh();
    });
    v->addWidget(back, 0, Qt::AlignHCenter);
    v->addStretch(2);
    m_center->addWidget(m_finished);
}

// ---------------------------------------------------------------------------------------------------------------

void RevisionsPage::refresh()
{
    m_stale = false;
    fillDecks();
    fillStats();
    if (m_inSession)
        return;
    if (m_deckId == 0) {
        m_center->setCurrentWidget(m_empty);
        return;
    }
    if (m_center->currentWidget() != m_finished)
        m_center->setCurrentWidget(m_overview);
    fillOverview();
}

void RevisionsPage::fillDecks()
{
    auto decks = m_study.decks();
    m_filling = true;
    m_deckList->clear();
    bool found = false;
    if (decks) {
        for (const DeckSummary& s : decks.value()) {
            auto* item = new QListWidgetItem(s.deck.name, m_deckList);
            item->setData(kIdRole, s.deck.id);
            item->setData(kDueRole, s.due());
            item->setToolTip(tr("%1 · %2 nouvelles, %3 en apprentissage, %4 à réviser")
                                 .arg(s.deck.name)
                                 .arg(s.newToday)
                                 .arg(s.learning)
                                 .arg(s.reviews));
            if (s.deck.id == m_deckId) {
                m_deckList->setCurrentItem(item);
                found = true;
            }
        }
    }
    if (!found) {
        m_deckId = m_deckList->count() > 0 ? m_deckList->item(0)->data(kIdRole).toLongLong() : 0;
        if (m_deckId > 0)
            m_deckList->setCurrentRow(0);
    }
    m_filling = false;
}

void RevisionsPage::fillStats()
{
    auto st = m_study.stats(0);
    if (!st)
        return;
    const DayStats& s = st.value();
    m_stats[0]->setText(QString::number(s.reviewed));
    m_stats[1]->setText(QString::number(s.remaining));
    m_stats[2]->setText(QStringLiteral("%1 / %2").arg(s.newDone).arg(s.newLimit));
    m_stats[3]->setText(s.successRate < 0 ? QStringLiteral("—") : QStringLiteral("%1 %").arg(qRound(s.successRate * 100)));
    m_stats[3]->setProperty("status", s.successRate < 0 ? QString() : QStringLiteral("ok"));
    m_stats[3]->style()->unpolish(m_stats[3]);
    m_stats[3]->style()->polish(m_stats[3]);
    m_chart->setValues(s.lastDays);
}

void RevisionsPage::fillOverview()
{
    auto summary = m_study.deckSummary(m_deckId);
    if (!summary)
        return;
    const DeckSummary& s = summary.value();
    m_deckName->setText(s.deck.name);
    QString info = countLabel(s.counts.total, tr("carte"), tr("cartes"));
    if (s.counts.suspended > 0)
        info += QStringLiteral(" · ") + countLabel(s.counts.suspended, tr("suspendue"), tr("suspendues"));
    info += QStringLiteral(" · ") + tr("jusqu'à %1 nouvelles par jour").arg(s.deck.newPerDay);
    info += QStringLiteral(" · ") + (s.deck.scheduler == QLatin1String("fsrs")
                                         ? tr("FSRS, rétention visée %1 %").arg(qRound(s.deck.retention * 100))
                                         : tr("SM-2"));
    m_deckInfo->setText(info);
    m_tiles[0]->setText(QString::number(s.newToday));
    m_tiles[1]->setText(QString::number(s.learning));
    m_tiles[2]->setText(QString::number(s.reviews));
    m_start->setEnabled(s.due() > 0);
    m_start->setText(s.due() > 0 ? tr("Commencer la révision (%1)").arg(s.due()) : tr("Rien à réviser pour l'instant"));

    m_cards->clear();
    auto cards = m_study.cards(m_deckId);
    const QDateTime now = m_study.now();
    if (cards) {
        for (const db::CardRow& row : cards.value()) {
            auto* item = new QTreeWidgetItem(m_cards);
            item->setData(0, kIdRole, row.card.id);
            QString text = preview(row.card);
            if (row.card.type == CardType::Cloze)
                text = QStringLiteral("[c%1] ").arg(row.card.ordinal) + text;
            item->setText(0, text);
            item->setToolTip(0, text);
            item->setText(1, typeLabel(row.card.type));
            item->setText(2, stateLabel(row));
            QString next = QStringLiteral("—");
            if (!row.card.suspended && row.state.state != CardState::New && row.state.due.isValid()) {
                const qint64 secs = now.secsTo(row.state.due);
                next = secs <= 0 ? tr("Maintenant") : tr("dans %1").arg(formatInterval(secs));
            }
            item->setText(3, next);
            if (row.card.suspended) {
                for (int c = 0; c < 4; ++c)
                    item->setForeground(c, m_ctx.theme.palette().text3);
            }
        }
    }
    m_cardsEmpty->setVisible(m_cards->topLevelItemCount() == 0);
    m_cards->setVisible(m_cards->topLevelItemCount() > 0);
}

bool RevisionsPage::showDeck(qint64 deckId)
{
    auto deck = m_study.deckSummary(deckId);
    if (!deck)
        return false;
    if (m_inSession)
        closeSession();
    m_deckId = deckId;
    m_center->setCurrentWidget(m_overview);
    refresh();
    return true;
}

void RevisionsPage::chooseNewDeck()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nouveau paquet"), tr("Nom du paquet :"), QLineEdit::Normal,
                                               QString(), &ok);
    if (ok)
        createDeck(name);
}

qint64 RevisionsPage::createDeck(const QString& name)
{
    auto d = m_study.createDeck(name);
    if (!d)
        return 0;
    showDeck(d.value().id);
    return d.value().id;
}

bool RevisionsPage::renameDeck(qint64 deckId, const QString& name)
{
    auto summary = m_study.deckSummary(deckId);
    if (!summary)
        return false;
    Deck d = summary.value().deck;
    d.name = name;
    return bool(m_study.updateDeck(d));
}

bool RevisionsPage::removeDeck(qint64 deckId)
{
    if (!m_study.removeDeck(deckId))
        return false;
    if (deckId == m_deckId)
        m_deckId = 0;
    refresh();
    return true;
}

void RevisionsPage::showDeckMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_deckList->itemAt(pos);
    QMenu menu(this);
    if (!item) {
        menu.addAction(tr("Nouveau paquet…"), this, &RevisionsPage::chooseNewDeck);
        menu.exec(m_deckList->viewport()->mapToGlobal(pos));
        return;
    }
    const qint64 id = item->data(kIdRole).toLongLong();
    const QString name = item->text();
    menu.addAction(Icons::get(QStringLiteral("settings"), m_ctx.theme.palette().text2, 16), tr("Réglages…"), this,
                   [this, id] { openDeckSettings(id); });
    menu.addAction(tr("Renommer…"), this, [this, id, name] {
        bool ok = false;
        const QString n = QInputDialog::getText(this, tr("Renommer le paquet"), tr("Nom du paquet :"),
                                                QLineEdit::Normal, name, &ok);
        if (ok)
            renameDeck(id, n);
    });
    menu.addSeparator();
    menu.addAction(Icons::get(QStringLiteral("trash"), m_ctx.theme.palette().danger, 16), tr("Supprimer le paquet…"),
                   this, [this, id, name] {
                       if (QMessageBox::question(this, tr("Supprimer le paquet"),
                                                 tr("Supprimer « %1 » et toutes ses cartes ? Leur historique de "
                                                    "révision est perdu.")
                                                     .arg(name))
                           == QMessageBox::Yes)
                           removeDeck(id);
                   });
    menu.exec(m_deckList->viewport()->mapToGlobal(pos));
}

void RevisionsPage::showCardMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_cards->itemAt(pos);
    if (!item)
        return;
    const qint64 id = item->data(0, kIdRole).toLongLong();
    auto row = m_study.card(id);
    if (!row)
        return;
    const bool suspended = row.value().card.suspended;
    const Palette& pal = m_ctx.theme.palette();
    QMenu menu(this);
    menu.addAction(tr("Modifier…"), this, [this, id] { editCard(id); });
    menu.addAction(suspended ? tr("Reprendre") : tr("Suspendre"), this,
                   [this, id, suspended] { setCardSuspended(id, !suspended); });
    menu.addAction(Icons::get(QStringLiteral("activity"), pal.text2, 16), tr("Historique…"), this,
                   [this, id] { showHistory(id); });
    if (row.value().card.sourceType != CardSource::None)
        menu.addAction(Icons::get(QStringLiteral("external"), pal.text2, 16), tr("Voir l'origine"), this, [this, id] {
            if (auto r = m_study.card(id)) {
                m_row = r.value();
                openSource();
            }
        });
    menu.addSeparator();
    menu.addAction(Icons::get(QStringLiteral("trash"), pal.danger, 16), tr("Supprimer"), this, [this, id] {
        if (QMessageBox::question(this, tr("Supprimer la carte"), tr("Supprimer cette carte et son historique ?"))
            == QMessageBox::Yes)
            removeCard(id);
    });
    menu.exec(m_cards->viewport()->mapToGlobal(pos));
}

CardEditorDialog* RevisionsPage::addCard()
{
    auto* dlg = new CardEditorDialog(m_ctx, m_study, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    if (m_deckId > 0)
        dlg->setDeck(m_deckId);
    connect(dlg, &QDialog::accepted, this, [this, dlg] {
        if (!dlg->saved().isEmpty())
            showDeck(dlg->saved().first().deckId);
    });
    dlg->open();
    return dlg;
}

CardEditorDialog* RevisionsPage::editCard(qint64 cardId)
{
    auto row = m_study.card(cardId);
    if (!row)
        return nullptr;
    auto* dlg = new CardEditorDialog(m_ctx, m_study, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->editCard(row.value().card);
    if (row.value().card.sourceType != CardSource::None) {
        if (auto src = m_study.source(row.value().card))
            dlg->setSource(row.value().card.sourceType, row.value().card.sourceId, src.value().label);
    }
    connect(dlg, &QDialog::accepted, this, [this, cardId] {
        if (m_inSession && m_row.card.id == cardId) {
            if (auto r = m_study.card(cardId)) {
                m_row = r.value();
                renderCard();
            }
            m_session->setFocus();
        }
    });
    dlg->open();
    return dlg;
}

DeckSettingsDialog* RevisionsPage::openDeckSettings(qint64 deckId)
{
    auto summary = m_study.deckSummary(deckId);
    if (!summary)
        return nullptr;
    auto* dlg = new DeckSettingsDialog(m_ctx, m_study, summary.value().deck, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
    return dlg;
}

CardHistoryDialog* RevisionsPage::showHistory(qint64 cardId)
{
    auto* dlg = new CardHistoryDialog(m_ctx, m_study, cardId, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
    return dlg;
}

RevisionSheetDialog* RevisionsPage::openSheetDialog()
{
    qint64 subject = 0;
    if (auto summary = m_study.deckSummary(m_deckId))
        subject = summary.value().deck.subjectId;
    auto* dlg = new RevisionSheetDialog(m_ctx, subject, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, this, [this, dlg] {
        if (generateSheet(dlg->subjectId(), dlg->options()) == 0)
            QMessageBox::warning(this, tr("Fiche de révision"), tr("La fiche n'a pas pu être créée."));
    });
    dlg->open();
    return dlg;
}

NoteDrafts::SheetMaterial RevisionsPage::sheetMaterial(qint64 subjectId)
{
    NoteDrafts::SheetMaterial m;
    m.date = m_study.now().toLocalTime().date();
    auto subjects = m_ctx.organize.subjects();
    if (!subjects)
        return m;
    m.subject = m_ctx.organize.subjectPath(subjectId, subjects.value());
    if (auto docs = m_ctx.organize.subjectItems(subjectId, OrganizeService::Entity::Document)) {
        for (qint64 id : docs.value()) {
            auto doc = m_ctx.library.document(id);
            if (!doc)
                continue;
            NoteDrafts::SheetDocument sd;
            sd.doc = doc.value();
            if (auto a = m_ctx.annotations.list(id))
                sd.annotations = a.value();
            if (auto c = m_ctx.comments.list(id))
                sd.comments = c.value();
            m.documents << sd;
        }
        std::sort(m.documents.begin(), m.documents.end(), [](const auto& a, const auto& b) {
            return QString::localeAwareCompare(a.doc.title, b.doc.title) < 0;
        });
    }
    if (auto notes = m_ctx.organize.subjectItems(subjectId, OrganizeService::Entity::Note)) {
        for (qint64 id : notes.value()) {
            if (auto n = m_ctx.notes.find(id))
                m.notes << n.value();
        }
        std::sort(m.notes.begin(), m.notes.end(), [](const NoteRecord& a, const NoteRecord& b) {
            return QString::localeAwareCompare(a.title, b.title) < 0;
        });
    }
    // Questions des cartes des paquets de la matière (et de ses sous-matières) ; trous masqués.
    const QList<qint64> scope = m_ctx.organize.withDescendants(subjectId, subjects.value());
    static const QRegularExpression cloze(QStringLiteral("\\{\\{c\\d+::.*?\\}\\}"));
    if (auto decks = m_study.decks()) {
        for (const DeckSummary& d : decks.value()) {
            if (!scope.contains(d.deck.subjectId))
                continue;
            auto cards = m_study.cards(d.deck.id);
            if (!cards)
                continue;
            for (const db::CardRow& row : cards.value()) {
                const Card& c = row.card;
                if (c.suspended || c.type == CardType::Reversed || m.cardQuestions.size() >= 40)
                    continue;
                QString q = c.front;
                if (c.type == CardType::Cloze)
                    q.replace(cloze, QStringLiteral("[…]"));
                if (!m.cardQuestions.contains(q.simplified()))
                    m.cardQuestions << q.simplified();
            }
        }
    }
    return m;
}

qint64 RevisionsPage::generateSheet(qint64 subjectId, const NoteDrafts::SheetOptions& options)
{
    if (subjectId <= 0)
        return 0;
    const NoteDrafts::SheetMaterial material = sheetMaterial(subjectId);
    if (material.subject.isEmpty())
        return 0;
    const NoteDraft draft = NoteDrafts::revisionSheet(material, options);
    QList<NoteSourceRef> sources;
    for (const NoteDrafts::SheetDocument& sd : material.documents)
        sources << NoteSourceRef{QStringLiteral("document"), sd.doc.id};
    auto note = m_ctx.notes.createDerived(draft.title, draft.type, draft.folder, draft.body, sources);
    if (!note)
        return 0;
    // La fiche appartient à la matière : elle apparaît sur sa page et dans ses collections.
    (void)m_ctx.organize.attach(subjectId, OrganizeService::Entity::Note, note.value().id);
    emit noteCreated(note.value().id);
    return note.value().id;
}

bool RevisionsPage::setCardSuspended(qint64 cardId, bool suspended)
{
    return bool(m_study.setSuspended(cardId, suspended));
}

bool RevisionsPage::removeCard(qint64 cardId) { return bool(m_study.removeCard(cardId)); }

// ---------------------------------------------------------------------------------------------------------------
// Séance

bool RevisionsPage::startSession()
{
    if (m_deckId <= 0)
        return false;
    auto next = m_study.nextCard(m_deckId);
    if (!next || !next.value())
        return false;
    if (!m_view) {
        m_view = new MarkdownView(m_session);
        m_view->setObjectName(QStringLiteral("FlashcardView"));
        m_view->setAccessibleName(tr("Carte"));
        m_view->setFixedHeight(m_ctx.theme.px(240));
        m_cardHost->addWidget(m_view);
        pushTheme();
        connect(m_view, &MarkdownView::ready, this, &RevisionsPage::fitCard);
        connect(m_view, &MarkdownView::linkActivated, this, [this](const QUrl& url) {
            if (url.scheme() == QLatin1String("studylab-card-source"))
                openSource();
        });
    }
    if (auto summary = m_study.deckSummary(m_deckId))
        m_sessionDeck->setText(summary.value().deck.name);
    m_inSession = true;
    m_sessionReviewed = 0;
    m_sessionPassed = 0;
    m_row = *next.value();
    m_center->setCurrentWidget(m_session);
    m_session->setFocus();
    renderCard();
    return true;
}

void RevisionsPage::closeSession()
{
    if (!m_inSession)
        return;
    m_inSession = false;
    m_answerShown = false;
    m_center->setCurrentWidget(m_overview);
    refresh();
}

void RevisionsPage::nextCard()
{
    auto next = m_study.nextCard(m_deckId);
    if (next && next.value()) {
        m_row = *next.value();
        renderCard();
        return;
    }
    // Séance finie : bilan.
    m_inSession = false;
    m_answerShown = false;
    m_finishedSummary->setText(
        m_sessionReviewed == 0
            ? tr("Aucune carte révisée.")
            : tr("%1 · %2 % de bonnes réponses")
                  .arg(countLabel(m_sessionReviewed, tr("réponse"), tr("réponses")))
                  .arg(qRound(100.0 * m_sessionPassed / m_sessionReviewed)));
    QString later;
    if (auto summary = m_study.deckSummary(m_deckId)) {
        const db::DeckCounts& c = summary.value().counts;
        if (c.fresh > 0 && summary.value().newToday == 0)
            later = tr("Limite de nouvelles cartes atteinte pour aujourd'hui ; la suite demain.");
    }
    if (auto cards = m_study.cards(m_deckId)) {
        const QDateTime end = StudyService::dayStart(m_study.now()).addDays(1);
        for (const db::CardRow& r : cards.value()) {
            if (!r.card.suspended
                && (r.state.state == CardState::Learning || r.state.state == CardState::Relearning)
                && r.state.due.isValid() && r.state.due < end) {
                later = tr("Des cartes en cours d'apprentissage reviendront plus tard aujourd'hui.");
                break;
            }
        }
    }
    m_finishedNext->setText(later);
    m_finishedNext->setVisible(!later.isEmpty());
    m_center->setCurrentWidget(m_finished);
    refresh();
}

void RevisionsPage::renderCard()
{
    m_answerShown = false;
    m_source = {};
    const Card& c = m_row.card;
    if (c.sourceType != CardSource::None) {
        if (auto src = m_study.source(c))
            m_source = src.value();
    }
    const QList<qint64> secs = m_study.preview(m_row);
    for (int i = 0; i < 4 && i < secs.size(); ++i)
        m_rateIntervals[i]->setText(QStringLiteral("%1 · %2").arg(formatInterval(secs[i])).arg(i + 1));
    m_actions->setCurrentIndex(0);
    m_shownAt.start();
    updateProgress();

    QJsonObject card;
    switch (c.type) {
    case CardType::Basic:
        card[QStringLiteral("question")] = c.front;
        card[QStringLiteral("answer")] = c.back;
        break;
    case CardType::Reversed:
        card[QStringLiteral("question")] = c.back;
        card[QStringLiteral("answer")] = c.front;
        break;
    case CardType::Formula:
        card[QStringLiteral("question")] = c.front;
        card[QStringLiteral("answer")] = QStringLiteral("$$\n%1\n$$").arg(c.back);
        break;
    case CardType::Cloze:
        card[QStringLiteral("question")] = c.front;
        card[QStringLiteral("answer")] = c.back;
        card[QStringLiteral("cloze")] = c.ordinal;
        break;
    }
    card[QStringLiteral("questionLabel")] = tr("Question");
    card[QStringLiteral("answerLabel")] = tr("Réponse");
    card[QStringLiteral("sourceLabel")] = tr("Source :");
    if (!m_source.label.isEmpty())
        card[QStringLiteral("source")] = QJsonObject{{QStringLiteral("label"), m_source.label},
                                                     {QStringLiteral("quote"), m_source.quote}};
    card[QStringLiteral("showAnswer")] = false;
    m_view->setProperty("flashcard", card);
    m_view->setFlashcard(card);
    fitCard();
}

void RevisionsPage::fitCard()
{
    // Hauteur de la carte ajustée à son contenu (bornée par la place disponible ; au-delà, la carte défile).
    if (!m_view || !m_view->isReady())
        return;
    QPointer<MarkdownView> view = m_view;
    m_view->evaluate(QStringLiteral("(() => { const c = document.querySelector('.flashcard');"
                                    " return c ? Math.ceil(c.getBoundingClientRect().bottom + window.scrollY) : 0 })()"),
                     [this, view](const QVariant& v) {
                         if (!view)
                             return;
                         const int content = int(v.toDouble() * view->zoomFactor()) + m_ctx.theme.px(30);
                         const int available = std::max(m_ctx.theme.px(240), m_session->height() - m_ctx.theme.px(240));
                         view->setFixedHeight(std::clamp(content, m_ctx.theme.px(240), available));
                     });
}

bool RevisionsPage::revealAnswer()
{
    if (!m_inSession || m_answerShown)
        return false;
    m_answerShown = true;
    QJsonObject card = m_view->property("flashcard").toJsonObject();
    card[QStringLiteral("showAnswer")] = true;
    m_view->setFlashcard(card);
    m_actions->setCurrentIndex(1);
    m_session->setFocus();
    fitCard();
    return true;
}

bool RevisionsPage::rate(Rating rating)
{
    if (!m_inSession || !m_answerShown)
        return false;
    auto r = m_study.answer(m_row.card.id, rating, std::min<qint64>(m_shownAt.elapsed(), 10 * 60 * 1000));
    if (!r)
        return false;
    ++m_sessionReviewed;
    if (rating != Rating::Again)
        ++m_sessionPassed;
    nextCard();
    return true;
}

void RevisionsPage::updateProgress()
{
    int remaining = 1;
    if (auto summary = m_study.deckSummary(m_deckId))
        remaining = std::max(1, summary.value().due());
    const int total = m_sessionReviewed + remaining;
    m_progress->setRange(0, total);
    m_progress->setValue(m_sessionReviewed);
    m_progressLabel->setText(QStringLiteral("%1 / %2").arg(m_sessionReviewed + 1).arg(total));
}

void RevisionsPage::openSource()
{
    const Card& c = m_row.card;
    switch (c.sourceType) {
    case CardSource::Annotation:
        if (auto src = m_study.source(c))
            emit openItemRequested(src.value().documentId, false, c.sourceId);
        break;
    case CardSource::Comment:
        if (auto src = m_study.source(c))
            emit openItemRequested(src.value().documentId, true, c.sourceId);
        break;
    case CardSource::Note:
        emit openNoteRequested(c.sourceId);
        break;
    case CardSource::Document:
        emit openDocumentRequested(c.sourceId);
        break;
    case CardSource::None:
        break;
    }
}

void RevisionsPage::pushTheme()
{
    if (!m_view)
        return;
    const Palette& p = m_ctx.theme.palette();
    QJsonObject vars{
        {QStringLiteral("bg"), hex(p.surface)},
        {QStringLiteral("card"), hex(p.dark ? p.surface2 : p.bg)},
        {QStringLiteral("card-border"), hex(p.borderSoft)},
        {QStringLiteral("text"), hex(p.text)},
        {QStringLiteral("muted"), hex(p.text2)},
        {QStringLiteral("border"), hex(p.borderSoft)},
        {QStringLiteral("link"), hex(p.primaryStrong)},
        {QStringLiteral("accent"), hex(p.primaryStrong)},
        {QStringLiteral("wiki"), hex(p.primaryStrong)},
        {QStringLiteral("quote-bar"), hex(p.accYellow)},
        {QStringLiteral("code-bg"), p.dark ? QStringLiteral("rgba(255,255,255,.08)") : QStringLiteral("rgba(0,0,0,.05)")},
        {QStringLiteral("font"), QStringLiteral("\"%1\", system-ui, sans-serif").arg(font().family())},
        {QStringLiteral("size"), QStringLiteral("%1px").arg(m_ctx.theme.px(14))},
    };
    m_view->setThemeVars(vars);
}

}  // namespace studylab::ui
