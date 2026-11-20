#include "ui/study/DeckSettingsDialog.h"

#include "services/organize/OrganizeService.h"
#include "services/study/Scheduler.h"
#include "services/study/StudyService.h"
#include "ui/components/Widgets.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

namespace studylab::ui {

DeckSettingsDialog::DeckSettingsDialog(AppContext& ctx, StudyService& study, const Deck& deck, QWidget* parent)
    : QDialog(parent), m_study(study), m_deck(deck)
{
    setWindowTitle(tr("Réglages du paquet"));
    setObjectName(QStringLiteral("DeckSettingsDialog"));
    setMinimumWidth(ctx.theme.px(520));
    auto* v = new QVBoxLayout(this);
    v->setSpacing(12);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setVerticalSpacing(10);
    m_name = new QLineEdit(deck.name, this);
    m_name->setObjectName(QStringLiteral("DeckName"));
    form->addRow(tr("Nom"), m_name);
    m_subject = new QComboBox(this);
    m_subject->setObjectName(QStringLiteral("DeckSubject"));
    m_subject->addItem(tr("Aucune"), 0);
    if (auto subjects = ctx.organize.subjects()) {
        for (const SubjectRecord& s : subjects.value()) {
            if (!s.archived || s.id == deck.subjectId)
                m_subject->addItem(s.name, s.id);
        }
    }
    m_subject->setCurrentIndex(std::max(0, m_subject->findData(deck.subjectId)));
    form->addRow(tr("Matière"), m_subject);
    v->addLayout(form);

    v->addWidget(makeSeparator(this));
    v->addWidget(makeLabel(tr("Planification"), "h3", this));
    auto* plan = new QFormLayout;
    plan->setVerticalSpacing(10);
    m_scheduler = new QComboBox(this);
    m_scheduler->setObjectName(QStringLiteral("DeckScheduler"));
    m_scheduler->addItem(tr("SM-2 (classique)"), QStringLiteral("sm2"));
    m_scheduler->addItem(tr("FSRS (modèle de la mémoire)"), QStringLiteral("fsrs"));
    m_scheduler->setCurrentIndex(deck.scheduler == QLatin1String("fsrs") ? 1 : 0);
    plan->addRow(tr("Planificateur"), m_scheduler);
    m_retention = new QSpinBox(this);
    m_retention->setObjectName(QStringLiteral("DeckRetention"));
    m_retention->setRange(70, 99);
    m_retention->setSuffix(QStringLiteral(" %"));
    m_retention->setValue(int(std::lround(deck.retention * 100)));
    plan->addRow(tr("Rétention visée"), m_retention);
    v->addLayout(plan);
    m_schedulerHint = makeLabel(QString(), "faint", this);
    m_schedulerHint->setWordWrap(true);
    v->addWidget(m_schedulerHint);
    m_retentionHint = makeLabel(QString(), "faint", this);
    m_retentionHint->setWordWrap(true);
    v->addWidget(m_retentionHint);

    v->addWidget(makeSeparator(this));
    v->addWidget(makeLabel(tr("Limites et étapes"), "h3", this));
    auto* limits = new QFormLayout;
    limits->setVerticalSpacing(10);
    m_newPerDay = new QSpinBox(this);
    m_newPerDay->setObjectName(QStringLiteral("DeckNewPerDay"));
    m_newPerDay->setRange(0, 9999);
    m_newPerDay->setValue(deck.newPerDay);
    limits->addRow(tr("Nouvelles cartes par jour"), m_newPerDay);
    m_reviewsPerDay = new QSpinBox(this);
    m_reviewsPerDay->setObjectName(QStringLiteral("DeckReviewsPerDay"));
    m_reviewsPerDay->setRange(0, 99999);
    m_reviewsPerDay->setValue(deck.reviewsPerDay);
    limits->addRow(tr("Révisions par jour"), m_reviewsPerDay);
    m_learning = new QLineEdit(stepsText(deck.learningSteps), this);
    m_learning->setObjectName(QStringLiteral("DeckLearningSteps"));
    m_learning->setPlaceholderText(QStringLiteral("1 10"));
    m_learning->setToolTip(tr("En minutes, séparées par des espaces ; « 1h » pour une heure."));
    limits->addRow(tr("Étapes d'apprentissage"), m_learning);
    m_relearning = new QLineEdit(stepsText(deck.relearningSteps), this);
    m_relearning->setObjectName(QStringLiteral("DeckRelearningSteps"));
    m_relearning->setPlaceholderText(tr("aucune"));
    m_relearning->setToolTip(tr("Après un oubli. Vide : la carte revient le lendemain."));
    limits->addRow(tr("Étapes après un oubli"), m_relearning);
    m_maxInterval = new QSpinBox(this);
    m_maxInterval->setObjectName(QStringLiteral("DeckMaxInterval"));
    m_maxInterval->setRange(1, 36500);
    m_maxInterval->setSuffix(tr(" jours"));
    m_maxInterval->setValue(deck.maxIntervalDays);
    limits->addRow(tr("Intervalle maximal"), m_maxInterval);
    v->addLayout(limits);
    // Saisie au clavier (ou molette) : les petites flèches du style natif jurent avec le thème.
    for (QSpinBox* box : {m_retention, m_newPerDay, m_reviewsPerDay, m_maxInterval}) {
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setMinimumWidth(ctx.theme.px(120));
    }
    auto* stepsHint = makeLabel(tr("Étapes en minutes, séparées par des espaces (« 1 10 », « 10 1h »). Elles "
                                   "s'appliquent aux cartes nouvelles et oubliées, avant les intervalles en jours."),
                                "faint", this);
    stepsHint->setWordWrap(true);
    v->addWidget(stepsHint);

    m_error = makeLabel(QString(), "muted", this);
    m_error->setObjectName(QStringLiteral("DeckSettingsError"));
    m_error->setProperty("status", QStringLiteral("error"));
    m_error->setWordWrap(true);
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

    connect(m_scheduler, &QComboBox::currentIndexChanged, this, &DeckSettingsDialog::refreshScheduler);
    connect(m_retention, &QSpinBox::valueChanged, this, &DeckSettingsDialog::refreshScheduler);
    refreshScheduler();
}

void DeckSettingsDialog::refreshScheduler()
{
    const bool fsrs = m_scheduler->currentData().toString() == QLatin1String("fsrs");
    m_retention->setEnabled(fsrs);
    m_retentionHint->setVisible(fsrs);
    m_schedulerHint->setText(
        fsrs ? tr("FSRS estime pour chaque carte sa stabilité et sa difficulté, et la représente quand la probabilité "
                  "de s'en souvenir descend à la rétention visée. Les cartes déjà révisées gardent leurs échéances.")
             : tr("SM-2 multiplie l'intervalle par un facteur de facilité propre à chaque carte (2,5 au départ), "
                  "qui baisse quand la carte est difficile."));
    if (fsrs) {
        // Ordre de grandeur : intervalle d'une carte de stabilité 10 jours, et charge relative.
        FsrsScheduler::Config c;
        c.retention = m_retention->value() / 100.0;
        const double days = FsrsScheduler(c).interval(10);
        m_retentionHint->setText(tr("À %1 %, une carte « stable sur 10 jours » revient après %2 jours. Plus la "
                                    "rétention visée est haute, plus les révisions sont fréquentes.")
                                     .arg(m_retention->value())
                                     .arg(days));
    }
}

bool DeckSettingsDialog::save()
{
    const auto fail = [this](const QString& message) {
        m_error->setText(message);
        m_error->show();
        return false;
    };
    m_error->hide();
    const auto learning = parseSteps(m_learning->text());
    if (!learning || learning->isEmpty())
        return fail(tr("Étapes d'apprentissage : au moins une durée, en minutes (« 1 10 »), d'au plus un jour."));
    const auto relearning = parseSteps(m_relearning->text());
    if (!relearning)
        return fail(tr("Étapes après un oubli : des durées en minutes (« 10 »), d'au plus un jour, ou rien."));
    Deck d = m_deck;
    d.name = m_name->text();
    d.subjectId = m_subject->currentData().toLongLong();
    d.scheduler = m_scheduler->currentData().toString();
    d.retention = m_retention->value() / 100.0;
    d.newPerDay = m_newPerDay->value();
    d.reviewsPerDay = m_reviewsPerDay->value();
    d.learningSteps = *learning;
    d.relearningSteps = *relearning;
    d.maxIntervalDays = m_maxInterval->value();
    auto r = m_study.updateDeck(d);
    if (!r)
        return fail(r.error().message);
    m_deck = r.value();
    return true;
}

// ---------------------------------------------------------------------------------------------------------------

namespace {

QString ratingLabel(Rating r)
{
    switch (r) {
    case Rating::Again:
        return CardHistoryDialog::tr("À revoir");
    case Rating::Hard:
        return CardHistoryDialog::tr("Difficile");
    case Rating::Good:
        return CardHistoryDialog::tr("Bien");
    case Rating::Easy:
        return CardHistoryDialog::tr("Facile");
    }
    return {};
}

}  // namespace

CardHistoryDialog::CardHistoryDialog(AppContext& ctx, StudyService& study, qint64 cardId, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Historique de la carte"));
    setObjectName(QStringLiteral("CardHistoryDialog"));
    resize(ctx.theme.px(560), ctx.theme.px(440));
    auto* v = new QVBoxLayout(this);
    v->setSpacing(10);
    m_summary = makeLabel(QString(), "muted", this);
    m_summary->setObjectName(QStringLiteral("CardHistorySummary"));
    m_summary->setWordWrap(true);
    v->addWidget(m_summary);
    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("CardHistory"));
    m_list->setRootIsDecorated(false);
    m_list->setHeaderLabels({tr("Date"), tr("Réponse"), tr("Revient après"), tr("Temps")});
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    v->addWidget(m_list, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Fermer"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);

    const QLocale fr(QLocale::French, QLocale::France);
    auto row = study.card(cardId);
    auto history = study.history(cardId);
    if (!row || !history) {
        m_summary->setText(tr("Carte introuvable."));
        return;
    }
    const Palette& p = ctx.theme.palette();
    for (const db::ReviewEntry& e : history.value()) {
        auto* item = new QTreeWidgetItem(m_list);
        item->setText(0, fr.toString(e.at.toLocalTime(), QStringLiteral("d MMM yyyy, HH:mm")));
        item->setText(1, ratingLabel(e.rating));
        item->setForeground(1, e.rating == Rating::Again ? p.danger : p.text);
        const QDateTime due = QDateTime::fromString(e.after.value(QStringLiteral("due")).toString(), Qt::ISODate);
        item->setText(2, due.isValid() ? formatInterval(std::max<qint64>(0, e.at.secsTo(due))) : QStringLiteral("—"));
        item->setText(3, e.elapsedMs <= 0      ? QStringLiteral("—")
                         : e.elapsedMs < 1000 ? tr("< 1 s")
                                              : tr("%1 s").arg(qRound(e.elapsedMs / 1000.0)));
    }

    const ScheduleState& s = row.value().state;
    QStringList parts;
    parts << countLabel(history.value().size(), tr("réponse"), tr("réponses"));
    if (s.lapses > 0)
        parts << countLabel(s.lapses, tr("oubli"), tr("oublis"));
    if (s.state != CardState::New && s.due.isValid())
        parts << tr("prochaine révision le %1").arg(fr.toString(s.due.toLocalTime(), QStringLiteral("d MMM yyyy")));
    if (s.stability > 0) {
        const QDateTime now = study.now();
        const double elapsed =
            s.state == CardState::Review ? s.intervalDays + s.due.secsTo(now) / 86400.0 : 0.0;
        parts << tr("stabilité %1").arg(formatInterval(qint64(s.stability * 86400)))
              << tr("difficulté %1 / 10").arg(fr.toString(s.difficulty, 'f', 1))
              << tr("rappel estimé aujourd'hui %1 %")
                     .arg(qRound(100 * FsrsScheduler::retrievability(std::max(0.0, elapsed), s.stability)));
    } else if (s.state == CardState::Review || s.state == CardState::Relearning) {
        parts << tr("facilité %1 %").arg(qRound(s.ease * 100));
    }
    m_summary->setText(parts.join(QStringLiteral(" · ")));
}

}  // namespace studylab::ui
