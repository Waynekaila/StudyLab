#include "ui/pages/ActivityPage.h"

#include "services/settings/SettingsService.h"
#include "ui/components/Charts.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/Widgets.h"
#include "ui/pages/PageScaffold.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {

constexpr int kMaxRows = 120;
constexpr int kHeatmapWeeks = 26;

QLocale french() { return QLocale(QLocale::French, QLocale::France); }

QString capitalized(QString s)
{
    if (!s.isEmpty())
        s[0] = s[0].toUpper();
    return s;
}

/// Bouton de ligne dont la taille suit son contenu.
class JournalRow : public QPushButton {
public:
    using QPushButton::QPushButton;
    QSize sizeHint() const override { return layout() ? layout()->sizeHint() : QPushButton::sizeHint(); }
    QSize minimumSizeHint() const override
    {
        return layout() ? QSize(0, layout()->minimumSize().height()) : QPushButton::minimumSizeHint();
    }
};

int heatLevel(const DayActivity& d)
{
    if (!d.any())
        return 0;
    const double score = d.seconds / 60.0 + d.annotations + d.comments + 2.0 * d.notes + d.reviews / 5.0 + d.pages / 3.0;
    return score < 5 ? 1 : score < 15 ? 2 : score < 40 ? 3 : 4;
}

QString dayTooltip(const DayActivity& d)
{
    QString text = capitalized(french().toString(d.day, QStringLiteral("dddd d MMMM yyyy")));
    QStringList parts;
    if (d.seconds > 0)
        parts << QObject::tr("%1 de lecture").arg(formatDuration(d.seconds));
    if (d.pages > 0)
        parts << countLabel(d.pages, QObject::tr("page"), QObject::tr("pages"));
    if (d.annotations > 0)
        parts << countLabel(d.annotations, QObject::tr("annotation"), QObject::tr("annotations"));
    if (d.comments > 0)
        parts << countLabel(d.comments, QObject::tr("commentaire"), QObject::tr("commentaires"));
    if (d.notes > 0)
        parts << countLabel(d.notes, QObject::tr("note créée"), QObject::tr("notes créées"));
    if (d.reviews > 0)
        parts << countLabel(d.reviews, QObject::tr("carte révisée"), QObject::tr("cartes révisées"));
    if (d.imported > 0)
        parts << countLabel(d.imported, QObject::tr("document importé"), QObject::tr("documents importés"));
    return text + QStringLiteral("\n") + (parts.isEmpty() ? QObject::tr("Aucune activité") : parts.join(QStringLiteral(" · ")));
}

}  // namespace

ActivityPage::ActivityPage(AppContext& ctx, ActivityService& activity, QWidget* parent)
    : QWidget(parent), m_ctx(ctx), m_activity(activity)
{
    setObjectName(QStringLiteral("ActivityPage"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scaffold = new PageScaffold(this);
    outer->addWidget(scaffold);
    QVBoxLayout* body = scaffold->body();

    auto* headerRow = new QHBoxLayout;
    auto* header = new QVBoxLayout;
    header->setSpacing(2);
    header->addWidget(makeLabel(tr("Activité"), "h1"));
    auto* sub = makeLabel(tr("Ce que vous avez lu, annoté, écrit et révisé. Tout reste sur cet ordinateur."), "subtitle");
    sub->setWordWrap(true);
    header->addWidget(sub);
    headerRow->addLayout(header, 1);
    m_periodControl = new SegmentedControl({tr("7 jours"), tr("30 jours"), tr("12 mois")});
    m_periodControl->setCurrentIndex(0);
    connect(m_periodControl, &SegmentedControl::currentIndexChanged, this,
            [this](int i) { setPeriod(Period(i)); });
    headerRow->addWidget(m_periodControl, 0, Qt::AlignTop);
    auto* goals = new QPushButton(Icons::get(QStringLiteral("target"), m_ctx.theme.palette().text2, 16), tr("Objectifs…"));
    goals->setCursor(Qt::PointingHandCursor);
    connect(goals, &QPushButton::clicked, this, &ActivityPage::editGoals);
    headerRow->addWidget(goals, 0, Qt::AlignTop);
    body->addLayout(headerRow);

    // Chiffres de la période
    auto* tiles = new QGridLayout;
    tiles->setHorizontalSpacing(14);
    tiles->setVerticalSpacing(14);
    const QStringList titles{tr("TEMPS DE LECTURE"), tr("PAGES LUES"),   tr("ANNOTATIONS"),
                             tr("COMMENTAIRES"),     tr("NOTES CRÉÉES"), tr("CARTES RÉVISÉES")};
    for (int i = 0; i < titles.size(); ++i) {
        auto* tile = makeCard();
        tile->setObjectName(QStringLiteral("StatTile"));
        auto* l = static_cast<QVBoxLayout*>(tile->layout());
        l->setSpacing(2);
        l->addWidget(makeLabel(titles.at(i), "caption"));
        auto* value = makeLabel(QStringLiteral("0"), "big");
        value->setObjectName(QStringLiteral("ActivityTileValue"));
        l->addWidget(value);
        auto* caption = makeLabel(QString(), "faint");
        caption->setWordWrap(true);
        l->addWidget(caption);
        l->addStretch(1);
        m_tileValues << value;
        m_tileCaptions << caption;
        tiles->addWidget(tile, 0, i);
        tiles->setColumnStretch(i, 1);
    }
    body->addLayout(tiles);

    auto* row = new QHBoxLayout;
    row->setSpacing(18);
    auto* chartCard = makeCard();
    auto* cl = static_cast<QVBoxLayout*>(chartCard->layout());
    m_chartTitle = makeLabel(QString(), "h3");
    cl->addWidget(m_chartTitle);
    m_chartNote = makeLabel(QString(), "faint");
    cl->addWidget(m_chartNote);
    cl->addSpacing(6);
    m_chart = new BarChart(m_ctx.theme);
    cl->addWidget(m_chart, 1);
    row->addWidget(chartCard, 3);

    auto* heatCard = makeCard();
    auto* hl = static_cast<QVBoxLayout*>(heatCard->layout());
    hl->addWidget(makeLabel(tr("Régularité"), "h3"));
    m_activeDays = makeLabel(QString(), "faint");
    hl->addWidget(m_activeDays);
    hl->addSpacing(6);
    m_heatmap = new ActivityHeatmap(m_ctx.theme);
    hl->addWidget(m_heatmap);
    m_streak = makeLabel(QString(), "muted");
    m_streak->setObjectName(QStringLiteral("ActivityStreak"));
    hl->addWidget(m_streak);
    hl->addStretch(1);
    row->addWidget(heatCard, 2);
    body->addLayout(row);

    // Journal
    auto* journalCard = makeCard();
    auto* jl = static_cast<QVBoxLayout*>(journalCard->layout());
    auto* jh = new QHBoxLayout;
    jh->setSpacing(6);
    jh->addWidget(makeLabel(tr("Journal"), "h3"));
    jh->addSpacing(10);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    const QStringList filters{tr("Tout"), tr("Lecture"), tr("Annotations"), tr("Commentaires"), tr("Notes"),
                              tr("Révisions"), tr("Imports")};
    for (int i = 0; i < filters.size(); ++i) {
        auto* chip = new QToolButton;
        chip->setObjectName(QStringLiteral("FilterChip"));
        chip->setText(filters.at(i));
        chip->setCheckable(true);
        chip->setChecked(i == 0);
        chip->setCursor(Qt::PointingHandCursor);
        group->addButton(chip, i);
        jh->addWidget(chip);
        m_chips << chip;
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int id) { setFilter(Filter(id)); });
    jh->addStretch(1);
    jl->addLayout(jh);
    m_journal = new QVBoxLayout;
    m_journal->setSpacing(1);
    jl->addLayout(m_journal);
    m_empty = makeLabel(QString(), "muted");
    m_empty->setWordWrap(true);
    jl->addWidget(m_empty);
    m_more = makeLabel(QString(), "faint");
    jl->addWidget(m_more);
    body->addWidget(journalCard);
    body->addStretch(1);

    connect(&m_activity, &ActivityService::changed, this, [this] {
        if (isVisible())
            refresh();
    });
}

void ActivityPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
}

void ActivityPage::setPeriod(Period period)
{
    m_period = period;
    if (m_periodControl->currentIndex() != int(period))
        m_periodControl->setCurrentIndex(int(period));
    refresh();
}

void ActivityPage::setFilter(Filter filter)
{
    m_filter = filter;
    if (QToolButton* chip = m_chips.value(int(filter)))
        chip->setChecked(true);
    fillJournal();
}

QDate ActivityPage::periodStart() const
{
    const QDate today = m_activity.today();
    switch (m_period) {
    case Period::Week:
        return today.addDays(-6);
    case Period::Month:
        return today.addDays(-29);
    case Period::Year:
        return QDate(today.year(), today.month(), 1).addMonths(-11);
    }
    return today;
}

void ActivityPage::refresh()
{
    fillTiles();
    fillChart();
    fillHeatmap();
    fillJournal();
}

void ActivityPage::fillTiles()
{
    const QLocale fr = french();
    const auto t = m_activity.totals(periodStart(), m_activity.today());
    if (!t)
        return;
    const PeriodTotals& v = t.value();
    m_tileValues[0]->setText(formatDuration(v.seconds));
    m_tileCaptions[0]->setText(countLabel(v.activeDays, tr("jour actif"), tr("jours actifs")));
    m_tileValues[1]->setText(fr.toString(v.pages));
    m_tileCaptions[1]->setText(countLabel(v.documents, tr("document lu"), tr("documents lus")));
    m_tileValues[2]->setText(fr.toString(v.annotations));
    m_tileCaptions[2]->setText(tr("surlignages et notes"));
    m_tileValues[3]->setText(fr.toString(v.comments));
    m_tileCaptions[3]->setText(tr("questions et réflexions"));
    m_tileValues[4]->setText(fr.toString(v.notes));
    m_tileCaptions[4]->setText(v.imported > 0 ? countLabel(v.imported, tr("document importé"), tr("documents importés"))
                                              : tr("dans Mes notes"));
    m_tileValues[5]->setText(fr.toString(v.reviews));
    m_tileCaptions[5]->setText(v.reviews > 0 ? tr("%1 % réussies").arg(qRound(100.0 * v.reviewsPassed / v.reviews))
                                             : tr("aucune révision"));
    if (!m_ctx.settings.value(SettingKeys::TrackReading, true).toBool())
        m_tileCaptions[0]->setText(tr("mesure désactivée (Paramètres)"));
}

void ActivityPage::fillChart()
{
    const QLocale fr = french();
    const QDate today = m_activity.today();
    const QDate from = periodStart();
    const auto days = m_activity.days(from, today);
    if (!days)
        return;
    QList<int> values;
    QStringList labels;
    QStringList tips;
    int activeDays = 0;
    int total = 0;
    if (m_period == Period::Year) {
        m_chartTitle->setText(tr("Temps de lecture par mois"));
        for (int m = 0; m < 12; ++m) {
            const QDate month = from.addMonths(m);
            int seconds = 0;
            for (const DayActivity& d : days.value())
                if (d.day.year() == month.year() && d.day.month() == month.month())
                    seconds += d.seconds;
            values << seconds / 60;
            labels << fr.monthName(month.month(), QLocale::NarrowFormat);
            tips << tr("%1 : %2").arg(capitalized(fr.toString(month, QStringLiteral("MMMM yyyy"))),
                                      formatDuration(seconds));
        }
    } else {
        m_chartTitle->setText(tr("Temps de lecture par jour"));
        for (const DayActivity& d : days.value()) {
            values << d.seconds / 60;
            labels << (m_period == Period::Week ? fr.dayName(d.day.dayOfWeek(), QLocale::ShortFormat)
                                                : QString::number(d.day.day()));
            tips << tr("%1 : %2").arg(capitalized(fr.toString(d.day, QStringLiteral("dddd d MMMM"))),
                                      formatDuration(d.seconds));
        }
    }
    for (const DayActivity& d : days.value()) {
        if (d.seconds > 0) {
            ++activeDays;
            total += d.seconds;
        }
    }
    m_chart->setData(values, labels, tips, int(values.size()) - 1);
    m_chartNote->setText(activeDays > 0 ? tr("Moyenne : %1 par jour de lecture").arg(formatDuration(total / activeDays))
                                        : tr("Le temps passé dans un document ouvert (au premier plan, sans pause de "
                                             "plus de deux minutes) s'affichera ici."));
}

void ActivityPage::fillHeatmap()
{
    const QDate today = m_activity.today();
    const QDate monday = today.addDays(1 - today.dayOfWeek());
    const QDate first = monday.addDays(-7 * (kHeatmapWeeks - 1));
    const auto days = m_activity.days(first, today);
    if (!days)
        return;
    QList<int> levels;
    QStringList tips;
    int active = 0;
    for (const DayActivity& d : days.value()) {
        levels << heatLevel(d);
        tips << dayTooltip(d);
        if (d.any())
            ++active;
    }
    m_heatmap->setData(first, levels, tips);
    m_activeDays->setText(tr("%1 sur les %2 dernières semaines")
                              .arg(countLabel(active, tr("jour actif"), tr("jours actifs")))
                              .arg(kHeatmapWeeks));
    const auto streak = m_activity.streak();
    const int n = streak ? streak.value() : 0;
    m_streak->setText(n > 0 ? tr("Série en cours : %1").arg(countLabel(n, tr("jour"), tr("jours")))
                            : tr("Pas de série en cours : un peu de lecture aujourd'hui suffit à en commencer une."));
}

bool ActivityPage::matches(const JournalEntry& e) const
{
    using K = JournalEntry::Kind;
    switch (m_filter) {
    case Filter::All:
        return true;
    case Filter::Reading:
        return e.kind == K::Reading || e.kind == K::Opened;
    case Filter::Annotations:
        return e.kind == K::Annotations;
    case Filter::Comments:
        return e.kind == K::Comments;
    case Filter::Notes:
        return e.kind == K::Note;
    case Filter::Reviews:
        return e.kind == K::Reviews;
    case Filter::Imports:
        return e.kind == K::Imported;
    }
    return true;
}

QPushButton* ActivityPage::journalRow(const JournalEntry& e)
{
    using K = JournalEntry::Kind;
    const Palette& pal = m_ctx.theme.palette();
    const QLocale fr = french();
    QString icon;
    QColor tint = pal.text3;
    QString text;
    QStringList caption;
    const QString title = QStringLiteral("« %1 »").arg(e.title);
    switch (e.kind) {
    case K::Reading:
        icon = QStringLiteral("book-open");
        tint = pal.primary;
        text = tr("Lu %1").arg(title);
        caption << formatDuration(e.seconds);
        if (e.pages > 0)
            caption << countLabel(e.pages, tr("page"), tr("pages"));
        break;
    case K::Opened:
        icon = QStringLiteral("book");
        text = tr("Ouvert %1").arg(title);
        break;
    case K::Annotations:
        icon = QStringLiteral("highlighter");
        tint = pal.amber;
        text = e.count == 1 ? tr("Annoté %1").arg(title)
                            : tr("%1 dans %2").arg(countLabel(e.count, tr("annotation"), tr("annotations")), title);
        break;
    case K::Comments:
        icon = QStringLiteral("message-square");
        tint = pal.lavender;
        text = e.count == 1 ? tr("Commenté %1").arg(title)
                            : tr("%1 dans %2").arg(countLabel(e.count, tr("commentaire"), tr("commentaires")), title);
        break;
    case K::Note:
        icon = QStringLiteral("note");
        tint = pal.sage;
        text = tr("Note créée : %1").arg(title);
        break;
    case K::Reviews:
        icon = QStringLiteral("cards");
        tint = pal.lavender;
        text = countLabel(e.count, tr("carte révisée"), tr("cartes révisées"));
        caption << tr("%1 réussie(s), %2 %").arg(e.passed).arg(qRound(100.0 * e.passed / std::max(1, e.count)));
        break;
    case K::Imported:
        icon = QStringLiteral("download");
        text = e.count == 1 && !e.title.isEmpty()
                   ? tr("Importé %1").arg(title)
                   : countLabel(e.count, tr("document importé"), tr("documents importés"));
        break;
    }
    caption << fr.toString(e.at.time(), QStringLiteral("HH:mm"));

    auto* row = new JournalRow;
    row->setObjectName(QStringLiteral("HomeRow"));
    row->setCursor(Qt::PointingHandCursor);
    row->setAccessibleName(text + QStringLiteral(" — ") + caption.join(QStringLiteral(" · ")));
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(8, 5, 8, 5);
    h->setSpacing(10);
    const int px = m_ctx.theme.px(16);
    auto* iconLabel = new QLabel;
    iconLabel->setPixmap(Icons::get(icon, tint, px).pixmap(px));
    iconLabel->setFixedWidth(px);
    auto* main = makeLabel(text);
    auto* sub = makeLabel(caption.join(QStringLiteral(" · ")), "faint");
    h->addWidget(iconLabel);
    h->addWidget(main, 1);
    h->addWidget(sub);
    for (QLabel* l : {iconLabel, main, sub})
        l->setAttribute(Qt::WA_TransparentForMouseEvents);
    main->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    const JournalEntry entry = e;
    connect(row, &QPushButton::clicked, this, [this, entry] {
        switch (entry.kind) {
        case K::Reading:
        case K::Opened:
            emit openDocumentRequested(entry.documentId);
            break;
        case K::Annotations:
            emit openItemRequested(entry.documentId, false, entry.itemId);
            break;
        case K::Comments:
            emit openItemRequested(entry.documentId, true, entry.itemId);
            break;
        case K::Note:
            emit openNoteRequested(entry.noteId);
            break;
        case K::Reviews:
            emit navigateRequested(QStringLiteral("revisions"));
            break;
        case K::Imported:
            if (entry.documentId > 0)
                emit openDocumentRequested(entry.documentId);
            else
                emit navigateRequested(QStringLiteral("library"));
            break;
        }
    });
    return row;
}

void ActivityPage::fillJournal()
{
    for (QPushButton* r : std::as_const(m_rows))
        r->deleteLater();
    for (QLabel* l : std::as_const(m_dayHeaders))
        l->deleteLater();
    m_rows.clear();
    m_dayHeaders.clear();
    while (m_journal->count() > 0)
        delete m_journal->takeAt(0);

    const QDate today = m_activity.today();
    const auto entries = m_activity.journal(periodStart(), today);
    const QLocale fr = french();
    QDate current;
    int hidden = 0;
    if (entries) {
        for (const JournalEntry& e : entries.value()) {
            if (!matches(e))
                continue;
            if (m_rows.size() >= kMaxRows) {
                ++hidden;
                continue;
            }
            if (e.day != current) {
                current = e.day;
                const QString label = e.day == today            ? tr("AUJOURD'HUI")
                                    : e.day == today.addDays(-1) ? tr("HIER")
                                                                 : fr.toString(e.day, QStringLiteral("dddd d MMMM")).toUpper();
                auto* header = makeLabel(label, "caption");
                header->setContentsMargins(8, m_dayHeaders.isEmpty() ? 4 : 12, 0, 2);
                m_journal->addWidget(header);
                m_dayHeaders << header;
            }
            auto* row = journalRow(e);
            m_journal->addWidget(row);
            m_rows << row;
        }
    }
    m_more->setText(hidden > 0 ? tr("… et %1 de plus sur la période.").arg(countLabel(hidden, tr("entrée"), tr("entrées")))
                               : QString());
    m_more->setVisible(hidden > 0);
    m_empty->setText(m_filter == Filter::All
                         ? tr("Rien sur cette période. Ouvrez un document, annotez, écrivez une note ou révisez des "
                              "cartes : chaque action s'inscrit ici, avec un lien pour y revenir.")
                         : tr("Rien de ce type sur cette période."));
    m_empty->setVisible(m_rows.isEmpty());
}

GoalsDialog* ActivityPage::editGoals()
{
    auto* dlg = new GoalsDialog(m_ctx, m_activity, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
    return dlg;
}

// ---------------------------------------------------------------------------------------------------------------

GoalsDialog::GoalsDialog(AppContext& ctx, ActivityService& activity, QWidget* parent)
    : QDialog(parent), m_activity(activity)
{
    setWindowTitle(tr("Objectifs"));
    setObjectName(QStringLiteral("GoalsDialog"));
    setMinimumWidth(ctx.theme.px(440));
    auto* v = new QVBoxLayout(this);
    v->setSpacing(12);
    auto* intro = makeLabel(tr("Des repères pour garder le rythme, affichés sur l'accueil. Laissez 0 pour ne pas "
                               "fixer d'objectif."),
                            "muted", this);
    intro->setWordWrap(true);
    v->addWidget(intro);
    auto* form = new QFormLayout;
    form->setVerticalSpacing(10);
    auto makeBox = [this](const char* name, int max, const QString& suffix) {
        auto* box = new QSpinBox(this);
        box->setObjectName(QString::fromLatin1(name));
        box->setRange(0, max);
        box->setSuffix(suffix);
        box->setSpecialValueText(tr("Aucun"));
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        return box;
    };
    m_pages = makeBox("GoalPages", 10000, tr(" pages"));
    m_minutes = makeBox("GoalMinutes", 900, tr(" min"));
    m_reviews = makeBox("GoalReviews", 5000, tr(" cartes"));
    form->addRow(tr("Pages lues par semaine"), m_pages);
    form->addRow(tr("Temps de lecture par jour"), m_minutes);
    form->addRow(tr("Cartes révisées par jour"), m_reviews);
    v->addLayout(form);
    auto* hint = makeLabel(tr("Une page compte une fois par semaine, même relue. Pour un EPUB ou un texte, une page "
                              "équivaut à environ 1 500 caractères."),
                           "faint", this);
    hint->setWordWrap(true);
    v->addWidget(hint);
    if (const auto g = activity.goals()) {
        m_pages->setValue(g.value().pagesPerWeek);
        m_minutes->setValue(g.value().minutesPerDay);
        m_reviews->setValue(g.value().reviewsPerDay);
    }
    m_error = makeLabel(QString(), "muted", this);
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
}

bool GoalsDialog::save()
{
    Goals g;
    g.pagesPerWeek = m_pages->value();
    g.minutesPerDay = m_minutes->value();
    g.reviewsPerDay = m_reviews->value();
    if (auto r = m_activity.setGoals(g); !r) {
        m_error->setText(r.error().message);
        m_error->show();
        return false;
    }
    return true;
}

}  // namespace studylab::ui
