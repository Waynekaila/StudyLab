#include "ui/pages/HomePage.h"

#include "data/repositories/DocumentRepository.h"
#include "services/activity/ActivityService.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/settings/SettingsService.h"
#include "services/study/StudyService.h"
#include "ui/components/Charts.h"
#include "ui/components/Widgets.h"
#include "ui/library/CoverArt.h"
#include "ui/organize/OrganizeWidgets.h"
#include "ui/pages/ActivityPage.h"
#include "ui/pages/PageScaffold.h"
#include "ui/reader/AnnotationStyle.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLocale>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QToolButton>
#include <QRegularExpression>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>

namespace studylab::ui {

/// Couverture du document à reprendre (vraie couverture ou couverture générée).
class ResumeCover : public QWidget {
public:
    ResumeCover(ThemeManager& theme, QWidget* parent)
        : QWidget(parent)
        , m_theme(theme)
    {
        setFixedSize(theme.px(96), theme.px(136));
    }
    void setDocument(const DocumentRecord& doc, const QImage& cover)
    {
        m_doc = doc;
        m_image = cover;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        CoverArt::paintCover(p, QRectF(rect()), m_doc, m_image, m_theme.palette(), m_theme.px(100) / 100.0);
    }

private:
    ThemeManager& m_theme;
    DocumentRecord m_doc;
    QImage m_image;
};

namespace {
/// Couverture enregistrée, image vide sinon (sans tenter d'ouvrir un chemin vide).
QImage coverImage(const LibraryService& library, const DocumentRecord& doc)
{
    return doc.coverPath.isEmpty() ? QImage() : QImage(library.coverFile(doc));
}
}  // namespace

namespace {

/// Bouton de ligne : sa taille est celle de son contenu (icône, texte, légende).
class RowButton : public QPushButton {
public:
    using QPushButton::QPushButton;
    QSize sizeHint() const override { return layout() ? layout()->sizeHint() : QPushButton::sizeHint(); }
    QSize minimumSizeHint() const override
    {
        return layout() ? QSize(0, layout()->minimumSize().height()) : QPushButton::minimumSizeHint();
    }
};

QPixmap dot(const QColor& color, int size)
{
    QPixmap pm(size * 2, size * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(size * 0.2, size * 0.2, size * 0.6, size * 0.6));
    return pm;
}

/// Première ligne lisible d'un texte Markdown (titres, emphase et liens simplifiés).
QString plainSummary(const QString& markdown)
{
    QString text = markdown;
    text.replace(QRegularExpression(QStringLiteral("\\[\\[([^\\]|]+)(\\|([^\\]]+))?\\]\\]")), QStringLiteral("\\1"));
    text.replace(QRegularExpression(QStringLiteral("\\[([^\\]]+)\\]\\([^)]*\\)")), QStringLiteral("\\1"));
    text.remove(QRegularExpression(QStringLiteral("^#+\\s*|[*_`>$]"), QRegularExpression::MultilineOption));
    return text.simplified();
}

QString elided(const QString& text, int max)
{
    return text.size() <= max ? text : text.left(max - 1).trimmed() + QStringLiteral("…");
}

}  // namespace

HomePage::HomePage(AppContext& ctx, StudyService* study, QWidget* parent)
    : QWidget(parent)
    , m_ctx(ctx)
    , m_study(study)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scaffold = new PageScaffold(this);
    outer->addWidget(scaffold);
    QVBoxLayout* body = scaffold->body();

    auto* headerRow = new QHBoxLayout;
    auto* header = new QVBoxLayout;
    header->setSpacing(2);
    m_greeting = makeLabel(QString(), "h1");
    header->addWidget(m_greeting);
    m_date = makeLabel(QString(), "subtitle");
    m_date->setWordWrap(true);
    header->addWidget(m_date);
    headerRow->addLayout(header, 1);
    auto* importBtn = new QPushButton(Icons::get(QStringLiteral("download"), m_ctx.theme.palette().text2, 16),
                                      tr("Importer"));
    importBtn->setCursor(Qt::PointingHandCursor);
    connect(importBtn, &QPushButton::clicked, this, &HomePage::importRequested);
    auto* newNote = new QPushButton(Icons::get(QStringLiteral("plus"), m_ctx.theme.palette().onPrimary, 16),
                                    tr("Nouvelle note"));
    newNote->setProperty("variant", QStringLiteral("primary"));
    newNote->setCursor(Qt::PointingHandCursor);
    connect(newNote, &QPushButton::clicked, this, &HomePage::newNoteRequested);
    headerRow->addWidget(importBtn, 0, Qt::AlignTop);
    headerRow->addWidget(newNote, 0, Qt::AlignTop);
    body->addLayout(headerRow);

    auto makeListCard = [this](const QString& title, const QString& link, const QString& pageId, QVBoxLayout*& list,
                               QLabel*& empty, const QString& emptyText) {
        auto* card = makeCard();
        auto* l = static_cast<QVBoxLayout*>(card->layout());
        l->addWidget(cardHeader(title, link, pageId));
        list = new QVBoxLayout;
        list->setSpacing(2);
        l->addLayout(list);
        empty = makeLabel(emptyText, "muted");
        empty->setWordWrap(true);
        l->addWidget(empty);
        l->addStretch(1);
        return card;
    };

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(18);
    grid->addWidget(buildResume(), 0, 0);
    grid->addWidget(buildRevisions(), 0, 1);
    grid->addWidget(buildGoal(), 0, 2);
    grid->addWidget(makeListCard(tr("Lectures en cours"), tr("Bibliothèque"), QStringLiteral("library"), m_readingList,
                                 m_readingEmpty, tr("Aucune autre lecture en cours.")),
                    1, 0);
    grid->addWidget(makeListCard(tr("Dernières annotations"), QString(), QString(), m_annotationList, m_annotationEmpty,
                                 tr("Vos surlignages et notes apparaîtront ici.")),
                    1, 1);
    grid->addWidget(makeListCard(tr("Progression par matière"), tr("Matières"), QStringLiteral("subjects"),
                                 m_subjectList, m_subjectEmpty,
                                 tr("Rangez vos documents par matière pour suivre votre progression.")),
                    1, 2);
    grid->addWidget(makeListCard(tr("Derniers commentaires"), QString(), QString(), m_commentList, m_commentEmpty,
                                 tr("Questions, explications et réflexions écrites pendant la lecture.")),
                    2, 0, 1, 2);
    grid->addWidget(buildFavorites(), 2, 2);
    for (int c = 0; c < 3; ++c)
        grid->setColumnStretch(c, c == 0 ? 5 : 4);
    body->addLayout(grid);

    // Ligne basse : notes récentes et sauvegarde.
    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(8);
    m_notesLabel = makeLabel(tr("Notes récentes :"), "faint");
    bottom->addWidget(m_notesLabel);
    m_noteRow = new QHBoxLayout;
    m_noteRow->setSpacing(6);
    bottom->addLayout(m_noteRow);
    bottom->addStretch(1);
    m_backup = makeLabel(QString(), "faint");
    m_backup->setObjectName(QStringLiteral("HomeBackup"));
    auto* backupLink = new QPushButton;
    backupLink->setObjectName(QStringLiteral("CardLink"));
    backupLink->setCursor(Qt::PointingHandCursor);
    backupLink->setText(tr("Sauvegardes"));
    connect(backupLink, &QPushButton::clicked, this, [this] { emit navigateRequested(QStringLiteral("settings")); });
    bottom->addWidget(m_backup);
    bottom->addWidget(backupLink);
    body->addLayout(bottom);
    body->addStretch(1);

    connect(&m_ctx.library, &LibraryService::documentsChanged, this, &HomePage::refreshIfVisible);
    if (m_study)
        connect(m_study, &StudyService::changed, this, &HomePage::refreshIfVisible);
    if (m_ctx.activity)
        connect(m_ctx.activity, &ActivityService::changed, this, &HomePage::refreshIfVisible);
    // Rempli à chaque affichage (showEvent) : pas de double calcul au démarrage.
}

void HomePage::refreshIfVisible()
{
    if (isVisible())
        refresh();
}

QWidget* HomePage::cardHeader(const QString& title, const QString& link, const QString& pageId, QLabel** count)
{
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 4);
    h->setSpacing(8);
    h->addWidget(makeLabel(title, "h3"));
    h->addStretch(1);
    if (count) {
        *count = makeLabel(QString(), "muted");
        h->addWidget(*count);
    }
    if (!link.isEmpty()) {
        auto* btn = new QPushButton(link);
        btn->setObjectName(QStringLiteral("CardLink"));
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, pageId] { emit navigateRequested(pageId); });
        h->addWidget(btn);
    }
    return w;
}

QWidget* HomePage::buildResume()
{
    auto* card = makeCard();
    card->setObjectName(QStringLiteral("ResumeCard"));
    auto* l = static_cast<QVBoxLayout*>(card->layout());

    m_resumeFilled = new QWidget(card);
    auto* row = new QHBoxLayout(m_resumeFilled);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(16);
    m_cover = new ResumeCover(m_ctx.theme, m_resumeFilled);
    row->addWidget(m_cover, 0, Qt::AlignTop);
    auto* info = new QVBoxLayout;
    info->setSpacing(4);
    info->addWidget(makeLabel(tr("REPRENDRE LA LECTURE"), "caption"));
    m_resumeTitle = makeLabel(QString(), "h2");
    m_resumeTitle->setWordWrap(true);
    m_resumeAuthor = makeLabel(QString(), "muted");
    m_resumeAuthor->setWordWrap(true);
    m_resumePosition = makeLabel(QString(), "faint");
    m_resumePosition->setWordWrap(true);
    m_resumeProgress = new QProgressBar;
    m_resumeProgress->setRange(0, 1000);
    m_resumeProgress->setTextVisible(false);
    m_resumeProgress->setFixedHeight(6);
    m_resumeQuote = makeLabel(QString(), "muted");
    m_resumeQuote->setObjectName(QStringLiteral("ResumeQuote"));
    m_resumeQuote->setWordWrap(true);
    m_continue = new QPushButton(Icons::get(QStringLiteral("book"), m_ctx.theme.palette().onPrimary, 16),
                                 tr("Continuer"));
    m_continue->setProperty("variant", QStringLiteral("primary"));
    m_continue->setCursor(Qt::PointingHandCursor);
    connect(m_continue, &QPushButton::clicked, this, [this] {
        if (m_resumeId > 0)
            emit openDocumentRequested(m_resumeId);
    });
    info->addWidget(m_resumeTitle);
    info->addWidget(m_resumeAuthor);
    info->addSpacing(4);
    info->addWidget(m_resumeProgress);
    info->addWidget(m_resumePosition);
    info->addSpacing(2);
    info->addWidget(m_resumeQuote);
    info->addStretch(1);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(m_continue);
    btnRow->addStretch(1);
    info->addLayout(btnRow);
    row->addLayout(info, 1);
    l->addWidget(m_resumeFilled);

    m_resumeEmpty = new QWidget(card);
    auto* empty = new QVBoxLayout(m_resumeEmpty);
    empty->setContentsMargins(0, 0, 0, 0);
    empty->addWidget(makeLabel(tr("REPRENDRE LA LECTURE"), "caption"));
    auto* emptyText = makeLabel(tr("Aucun document ouvert pour l'instant. Importez vos PDF, EPUB, textes et fichiers Markdown dans la "
                                   "bibliothèque (ou glissez-les dans la fenêtre), puis ouvrez-en un : vous le "
                                   "retrouverez ici, à la page où vous l'avez laissé."),
                                "muted");
    emptyText->setWordWrap(true);
    empty->addWidget(emptyText);
    auto* openLib = new QPushButton(tr("Ouvrir la bibliothèque"));
    openLib->setProperty("variant", QStringLiteral("primary"));
    openLib->setCursor(Qt::PointingHandCursor);
    connect(openLib, &QPushButton::clicked, this, [this] { emit navigateRequested(QStringLiteral("library")); });
    auto* emptyBtn = new QHBoxLayout;
    emptyBtn->addWidget(openLib);
    emptyBtn->addStretch(1);
    empty->addLayout(emptyBtn);
    empty->addStretch(1);
    l->addWidget(m_resumeEmpty);
    return card;
}

QWidget* HomePage::buildRevisions()
{
    auto* card = makeCard();
    m_revisionsCard = card;
    auto* l = static_cast<QVBoxLayout*>(card->layout());
    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    auto* icon = new QLabel;
    icon->setPixmap(Icons::get(QStringLiteral("cards"), m_ctx.theme.palette().primary, m_ctx.theme.px(16))
                        .pixmap(m_ctx.theme.px(16)));
    head->addWidget(icon);
    head->addWidget(makeLabel(tr("Révisions du jour"), "h3"));
    head->addStretch(1);
    m_startReview = new QPushButton(tr("Commencer"));
    m_startReview->setObjectName(QStringLiteral("CardLink"));
    m_startReview->setCursor(Qt::PointingHandCursor);
    connect(m_startReview, &QPushButton::clicked, this, [this] { emit navigateRequested(QStringLiteral("revisions")); });
    head->addWidget(m_startReview);
    l->addLayout(head);
    auto* countRow = new QHBoxLayout;
    countRow->setSpacing(8);
    m_due = makeLabel(QStringLiteral("0"), "big");
    m_due->setObjectName(QStringLiteral("HomeDue"));
    countRow->addWidget(m_due);
    m_dueCaption = makeLabel(QString(), "muted");
    countRow->addWidget(m_dueCaption, 0, Qt::AlignBottom);
    countRow->addStretch(1);
    l->addLayout(countRow);
    m_revisionList = new QVBoxLayout;
    m_revisionList->setSpacing(0);
    l->addLayout(m_revisionList);
    m_revisionEmpty = makeLabel(QString(), "muted");
    m_revisionEmpty->setWordWrap(true);
    l->addWidget(m_revisionEmpty);
    l->addStretch(1);
    card->setVisible(m_study != nullptr);
    return card;
}

QWidget* HomePage::buildGoal()
{
    auto* card = makeCard();
    m_goalCard = card;
    auto* l = static_cast<QVBoxLayout*>(card->layout());
    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    auto* icon = new QLabel;
    icon->setPixmap(Icons::get(QStringLiteral("target"), m_ctx.theme.palette().sage, m_ctx.theme.px(16))
                        .pixmap(m_ctx.theme.px(16)));
    head->addWidget(icon);
    head->addWidget(makeLabel(tr("Objectif de la semaine"), "h3"));
    head->addStretch(1);
    m_goalsButton = new QPushButton(tr("Objectifs…"));
    m_goalsButton->setObjectName(QStringLiteral("CardLink"));
    m_goalsButton->setCursor(Qt::PointingHandCursor);
    connect(m_goalsButton, &QPushButton::clicked, this, &HomePage::editGoals);
    head->addWidget(m_goalsButton);
    l->addLayout(head);
    auto* row = new QHBoxLayout;
    row->setSpacing(16);
    m_ring = new GoalRing(m_ctx.theme);
    row->addWidget(m_ring, 0, Qt::AlignTop);
    auto* text = new QVBoxLayout;
    text->setSpacing(3);
    m_weekDays = makeLabel(QString(), "muted");
    m_weekTime = makeLabel(QString());
    m_weekTime->setTextFormat(Qt::RichText);
    m_weekTime->setWordWrap(true);
    m_weekTrend = makeLabel(QString());
    m_weekTrend->setObjectName(QStringLiteral("WeekTrend"));
    m_weekTrend->setWordWrap(true);
    text->addWidget(m_weekDays);
    text->addWidget(m_weekTime);
    text->addWidget(m_weekTrend);
    text->addStretch(1);
    row->addLayout(text, 1);
    l->addLayout(row);
    m_dailyGoals = makeLabel(QString(), "faint");
    m_dailyGoals->setWordWrap(true);
    l->addWidget(m_dailyGoals);
    l->addStretch(1);
    card->setVisible(m_ctx.activity != nullptr);
    return card;
}

QWidget* HomePage::buildFavorites()
{
    auto* card = makeCard();
    auto* l = static_cast<QVBoxLayout*>(card->layout());
    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    auto* icon = new QLabel;
    icon->setPixmap(Icons::get(QStringLiteral("star"), m_ctx.theme.palette().amber, m_ctx.theme.px(16))
                        .pixmap(m_ctx.theme.px(16)));
    head->addWidget(icon);
    head->addWidget(makeLabel(tr("Favoris"), "h3"));
    head->addStretch(1);
    m_favoriteCount = makeLabel(QString(), "muted");
    head->addWidget(m_favoriteCount);
    l->addLayout(head);
    m_favoriteRow = new QHBoxLayout;
    m_favoriteRow->setSpacing(8);
    m_favoriteRow->addStretch(1);
    l->addLayout(m_favoriteRow);
    m_favoriteEmpty = makeLabel(tr("Marquez un document comme favori (étoile de la bibliothèque) pour le garder sous la main."),
                                "muted");
    m_favoriteEmpty->setWordWrap(true);
    l->addWidget(m_favoriteEmpty);
    l->addStretch(1);
    return card;
}

QPushButton* HomePage::makeRow(const QPixmap& icon, const QString& text, const QString& caption, double progress,
                               const QColor& progressColor)
{
    auto* row = new RowButton;
    row->setObjectName(QStringLiteral("HomeRow"));
    row->setCursor(Qt::PointingHandCursor);
    row->setAccessibleName(caption.isEmpty() ? text : text + QStringLiteral(" — ") + caption);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(8, 6, 8, 6);
    h->setSpacing(10);
    auto* iconLabel = new QLabel;
    iconLabel->setPixmap(icon);
    iconLabel->setFixedWidth(int(icon.deviceIndependentSize().width()));
    iconLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto* main = makeLabel(text);
    texts->addWidget(main);
    QList<QWidget*> passive{iconLabel, main};
    if (!caption.isEmpty()) {
        auto* sub = makeLabel(caption, "faint");
        texts->addWidget(sub);
        passive << sub;
    }
    if (progress >= 0) {
        auto* line = new ProgressLine(m_ctx.theme);
        line->setValue(progress);
        line->setColor(progressColor);
        texts->addSpacing(2);
        texts->addWidget(line);
        passive << line;
    }
    if (icon.isNull())
        iconLabel->hide();
    h->addWidget(iconLabel);
    h->addLayout(texts, 1);
    for (QWidget* w : passive) {
        w->setAttribute(Qt::WA_TransparentForMouseEvents);
        if (auto* lbl = qobject_cast<QLabel*>(w); lbl && lbl != iconLabel)
            lbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // texte long : coupé
    }
    iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    return row;
}

void HomePage::clearRows(QVBoxLayout* layout, QList<QPushButton*>& rows)
{
    for (QPushButton* r : std::as_const(rows)) {
        layout->removeWidget(r);
        r->deleteLater();
    }
    rows.clear();
}

QPixmap HomePage::smallCover(const DocumentRecord& doc, int width, int height) const
{
    const qreal dpr = 2;
    QPixmap pm(QSize(width, height) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    CoverArt::paintCover(p, QRectF(0, 0, width, height), doc, coverImage(m_ctx.library, doc),
                         m_ctx.theme.palette(), width / 160.0);
    return pm;
}

void HomePage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
}

void HomePage::refresh()
{
    const QLocale fr(QLocale::French, QLocale::France);
    const int hour = QTime::currentTime().hour();
    m_greeting->setText(hour >= 18 || hour < 5 ? tr("Bonsoir") : tr("Bonjour"));
    QString date = fr.toString(QDate::currentDate(), QStringLiteral("dddd d MMMM"));
    if (!date.isEmpty())
        date[0] = date[0].toUpper();
    QStringList parts{date};
    const qint64 n = m_ctx.library.count();
    parts << countLabel(n, tr("document"), tr("documents"));
    if (m_study) {
        const int due = m_study->dueTotal();
        if (due > 0)
            parts << countLabel(due, tr("carte à réviser"), tr("cartes à réviser"));
    }
    db::DocumentQuery q;
    q.status = ReadingStatus::Reading;
    if (const auto reading = m_ctx.library.documents(q); reading && !reading.value().isEmpty())
        parts << countLabel(reading.value().size(), tr("lecture en cours"), tr("lectures en cours"));
    m_date->setText(parts.join(QStringLiteral(" · ")));

    if (const auto last = m_ctx.backups.latest()) {
        m_backup->setText(tr("Dernière sauvegarde : %1 (%2)")
                              .arg(fr.toString(last->createdAt, QStringLiteral("dddd d MMMM 'à' HH:mm")),
                                   backupKindLabel(last->kind).toLower()));
    } else {
        m_backup->setText(tr("Aucune sauvegarde pour l'instant : la première est faite au prochain démarrage."));
    }
    m_backup->setToolTip(QDir::toNativeSeparators(m_ctx.paths.dataDir()));

    refreshResume();
    refreshReading();
    refreshRevisions();
    refreshGoal();
    refreshSubjects();
    refreshActivity();
    refreshFavorites();
    refreshNotes();
}

void HomePage::refreshResume()
{
    db::DocumentQuery q;
    q.sort = db::DocumentQuery::Sort::RecentlyOpened;
    q.limit = 1;
    const auto docs = m_ctx.library.documents(q);
    const DocumentRecord* d = docs && !docs.value().isEmpty() && !docs.value().first().lastOpenedAt.isEmpty()
                                  ? &docs.value().first()
                                  : nullptr;
    m_resumeId = d ? d->id : 0;
    m_resumeFilled->setVisible(d);
    m_resumeEmpty->setVisible(!d);
    if (!d)
        return;
    m_cover->setDocument(*d, coverImage(m_ctx.library, *d));
    m_resumeTitle->setText(d->title);
    const auto pos = m_ctx.library.readingPosition(d->id);
    const double progress = pos ? pos->progress : d->progress;
    const int percent = int(progress * 100 + 0.5);
    QStringList byline;
    if (!d->authors.isEmpty())
        byline << d->authors.join(QStringLiteral(", "));
    if (isPaged(d->format) && pos && pos->locator.contains(QStringLiteral("page")) && d->pageCount > 0) {
        const int page = pos->locator.value(QStringLiteral("page")).toInt();
        m_resumePosition->setText(tr("Page %1 sur %2 · %3 %").arg(page + 1).arg(d->pageCount).arg(percent)
                                  + QStringLiteral(" · ") + tr("ouvert %1").arg(relativeTime(d->lastOpenedAt)));
    } else {
        m_resumePosition->setText(tr("%1 % lu").arg(percent) + QStringLiteral(" · ")
                                  + tr("ouvert %1").arg(relativeTime(d->lastOpenedAt)));
    }
    m_resumeAuthor->setText(byline.join(QStringLiteral(" · ")));
    m_resumeAuthor->setVisible(!byline.isEmpty());
    m_resumeProgress->setValue(int(progress * 1000));
    QString quote;
    if (const auto anns = m_ctx.annotations.list(d->id); anns) {
        const AnnotationRecord* latest = nullptr;
        for (const auto& a : anns.value())
            if (!a.selectedText.isEmpty() && (!latest || a.createdAt > latest->createdAt))
                latest = &a;
        if (latest) {
            QStringList where;
            if (const QString loc = annotationLocation(*latest); !loc.isEmpty())
                where << loc;
            where << tr("dernière annotation, %1").arg(relativeTime(latest->createdAt));
            quote = tr("« %1 »").arg(elided(normalizeQuote(latest->selectedText), 160).toHtmlEscaped()) + QStringLiteral("<br><small>")
                  + where.join(QStringLiteral(" · ")).toHtmlEscaped() + QStringLiteral("</small>");
        }
    }
    m_resumeQuote->setTextFormat(Qt::RichText);
    m_resumeQuote->setText(quote);
    m_resumeQuote->setVisible(!quote.isEmpty());
}

void HomePage::refreshReading()
{
    clearRows(m_readingList, m_readingRows);
    db::DocumentQuery q;
    q.status = ReadingStatus::Reading;
    q.sort = db::DocumentQuery::Sort::RecentlyOpened;
    q.limit = 4;
    const auto docs = m_ctx.library.documents(q);
    const Palette& pal = m_ctx.theme.palette();
    if (docs) {
        for (const DocumentRecord& d : docs.value()) {
            if (d.id == m_resumeId || m_readingRows.size() >= 3)
                continue;
            QStringList caption;
            if (!d.authors.isEmpty())
                caption << d.authors.first();
            const auto pos = m_ctx.library.readingPosition(d.id);
            const double progress = pos ? pos->progress : d.progress;
            if (isPaged(d.format) && pos && pos->locator.contains(QStringLiteral("page")) && d.pageCount > 0)
                caption << tr("p. %1 / %2").arg(pos->locator.value(QStringLiteral("page")).toInt() + 1).arg(d.pageCount);
            else
                caption << tr("%1 %").arg(int(progress * 100 + 0.5));
            auto* row = makeRow(smallCover(d, m_ctx.theme.px(28), m_ctx.theme.px(38)), elided(d.title, 70),
                                caption.join(QStringLiteral(" · ")), progress, pal.primary);
            const qint64 id = d.id;
            connect(row, &QPushButton::clicked, this, [this, id] { emit openDocumentRequested(id); });
            m_readingList->addWidget(row);
            m_readingRows << row;
        }
    }
    m_readingEmpty->setVisible(m_readingRows.isEmpty());
}

void HomePage::refreshRevisions()
{
    if (!m_study)
        return;
    clearRows(m_revisionList, m_revisionRows);
    const auto decks = m_study->decks();
    const auto subjects = m_ctx.organize.subjects();
    QHash<qint64, SubjectRecord> byId;
    if (subjects)
        for (const SubjectRecord& s : subjects.value())
            byId.insert(s.id, s);
    auto rootOf = [&byId](qint64 id) {
        int guard = 0;
        while (byId.contains(id) && byId.value(id).parentId > 0 && guard++ < 64)
            id = byId.value(id).parentId;
        return byId.contains(id) ? id : 0;
    };
    int due = 0;
    QList<qint64> order;
    QHash<qint64, int> perSubject;
    if (decks) {
        for (const DeckSummary& s : decks.value()) {
            if (s.due() == 0)
                continue;
            due += s.due();
            const qint64 root = rootOf(s.deck.subjectId);
            if (!perSubject.contains(root))
                order << root;
            perSubject[root] += s.due();
        }
    }
    std::stable_sort(order.begin(), order.end(),
                     [&perSubject](qint64 a, qint64 b) { return perSubject.value(a) > perSubject.value(b); });
    m_due->setText(QLocale(QLocale::French, QLocale::France).toString(due));
    int reviewed = 0;
    if (const auto stats = m_study->stats(); stats)
        reviewed = stats.value().reviewed;
    QString caption = due == 1 ? tr("carte") : tr("cartes");
    if (reviewed > 0)
        caption += QStringLiteral(" · ") + tr("%1 déjà révisée(s) aujourd'hui").arg(reviewed);
    m_dueCaption->setText(caption);
    const Palette& pal = m_ctx.theme.palette();
    const int size = m_ctx.theme.px(14);
    for (qint64 id : std::as_const(order)) {
        if (m_revisionRows.size() >= 4)
            break;
        const QString name = id > 0 ? byId.value(id).name : tr("Sans matière");
        const QColor color = id > 0 ? organizeColor(pal, byId.value(id).color) : pal.text3;
        auto* row = makeRow(dot(color, size), name, QString());
        auto* count = makeLabel(QString::number(perSubject.value(id)));
        count->setObjectName(QStringLiteral("HomeRowCount"));
        count->setAttribute(Qt::WA_TransparentForMouseEvents);
        static_cast<QHBoxLayout*>(row->layout())->addWidget(count);
        row->setAccessibleName(tr("%1 : %2").arg(name).arg(perSubject.value(id)));
        connect(row, &QPushButton::clicked, this, [this] { emit navigateRequested(QStringLiteral("revisions")); });
        m_revisionList->addWidget(row);
        m_revisionRows << row;
    }
    const bool anyDeck = decks && !decks.value().isEmpty();
    m_revisionEmpty->setText(!anyDeck ? tr("Créez des cartes depuis vos annotations ou vos notes : celles à revoir "
                                           "chaque jour s'afficheront ici.")
                             : reviewed > 0 ? tr("Tout est révisé pour aujourd'hui.")
                                            : tr("Rien à réviser aujourd'hui."));
    m_revisionEmpty->setVisible(due == 0);
    m_startReview->setVisible(due > 0);
}

void HomePage::refreshGoal()
{
    if (!m_ctx.activity)
        return;
    const auto w = m_ctx.activity->week();
    if (!w)
        return;
    const WeekProgress& wp = w.value();
    const QLocale fr(QLocale::French, QLocale::France);
    m_ring->setValues(wp.pages, wp.goals.pagesPerWeek,
                      wp.goals.pagesPerWeek > 0 ? tr("/ %1 p.").arg(wp.goals.pagesPerWeek) : tr("pages"));
    auto dayName = [&fr](const QDate& d) {
        QString s = fr.dayName(d.dayOfWeek());
        if (!s.isEmpty())
            s[0] = s[0].toUpper();
        return s;
    };
    m_weekDays->setText(wp.today == wp.monday ? dayName(wp.monday)
                                              : tr("%1 → %2").arg(dayName(wp.monday), fr.dayName(wp.today.dayOfWeek())));
    m_weekTime->setText(tr("<b>%1</b> de lecture").arg(formatDuration(wp.seconds).toHtmlEscaped()));
    QString trend;
    const Palette& pal = m_ctx.theme.palette();
    if (wp.previousSeconds > 0) {
        const int pct = qRound(100.0 * (wp.seconds - wp.previousSeconds) / wp.previousSeconds);
        trend = pct >= 0 ? tr("+%1 % vs semaine dernière").arg(pct) : tr("%1 % vs semaine dernière").arg(pct);
        m_weekTrend->setStyleSheet(QStringLiteral("color: %1;").arg((pct >= 0 ? pal.sage : pal.text2).name()));
    } else if (wp.seconds > 0) {
        trend = tr("Rien de mesuré la semaine dernière");
        m_weekTrend->setStyleSheet(QStringLiteral("color: %1;").arg(pal.text3.name()));
    }
    m_weekTrend->setText(trend);
    m_weekTrend->setVisible(!trend.isEmpty());

    QStringList daily;
    if (wp.goals.minutesPerDay > 0)
        daily << tr("Lecture aujourd'hui : %1 / %2 min").arg(wp.todaySeconds / 60).arg(wp.goals.minutesPerDay);
    if (wp.goals.reviewsPerDay > 0)
        daily << tr("Révisions aujourd'hui : %1 / %2").arg(wp.todayReviews).arg(wp.goals.reviewsPerDay);
    if (!wp.goals.any())
        daily << tr("Aucun objectif fixé : « Objectifs… » pour en choisir un (pages par semaine, minutes ou révisions "
                    "par jour).");
    if (!m_ctx.settings.value(SettingKeys::TrackReading, true).toBool())
        daily << tr("La mesure du temps de lecture est désactivée (Paramètres).");
    m_dailyGoals->setText(daily.join(QLatin1Char('\n')));
    m_dailyGoals->setVisible(!daily.isEmpty());
}

void HomePage::refreshSubjects()
{
    clearRows(m_subjectList, m_subjectRows);
    const auto subjects = m_ctx.organize.subjects();
    const Palette& pal = m_ctx.theme.palette();
    if (subjects) {
        QList<SubjectRecord> roots;
        for (const SubjectRecord& s : subjects.value())
            if (s.parentId == 0 && !s.archived)
                roots << s;
        std::stable_sort(roots.begin(), roots.end(),
                         [](const SubjectRecord& a, const SubjectRecord& b) { return a.position < b.position; });
        for (const SubjectRecord& s : std::as_const(roots)) {
            if (m_subjectRows.size() >= 5)
                break;
            const auto stats = m_ctx.organize.subjectStats(s.id);
            if (!stats || stats.value().documents == 0)
                continue;
            const double progress = stats.value().progress;
            auto* row = makeRow(QPixmap(), s.name,
                                countLabel(stats.value().documents, tr("document"), tr("documents")), progress,
                                organizeColor(pal, s.color));
            auto* pct = makeLabel(tr("%1 %").arg(qRound(progress * 100)), "muted");
            pct->setAttribute(Qt::WA_TransparentForMouseEvents);
            static_cast<QHBoxLayout*>(row->layout())->addWidget(pct, 0, Qt::AlignTop);
            row->setAccessibleName(tr("%1 : %2 %").arg(s.name).arg(qRound(progress * 100)));
            connect(row, &QPushButton::clicked, this, [this] { emit navigateRequested(QStringLiteral("subjects")); });
            m_subjectList->addWidget(row);
            m_subjectRows << row;
        }
    }
    m_subjectEmpty->setVisible(m_subjectRows.isEmpty());
}

void HomePage::refreshActivity()
{
    const Palette& pal = m_ctx.theme.palette();
    const int icon = m_ctx.theme.px(16);
    QHash<qint64, QString> titles;
    auto titleOf = [this, &titles](qint64 id) {
        if (!titles.contains(id)) {
            const auto d = m_ctx.library.document(id);
            titles.insert(id, d ? d.value().title : QString());
        }
        return titles.value(id);
    };

    clearRows(m_annotationList, m_annotationRows);
    if (const auto anns = m_ctx.annotations.recent(4); anns) {
        for (const AnnotationRecord& a : anns.value()) {
            QString text = !a.selectedText.isEmpty() ? QStringLiteral("« %1 »").arg(elided(normalizeQuote(a.selectedText), 90))
                         : !a.note.isEmpty()         ? elided(plainSummary(a.note), 90)
                                                     : annotationTypeLabel(a.type);
            QStringList caption{elided(titleOf(a.documentId), 48)};
            if (const QString where = annotationLocation(a); !where.isEmpty())
                caption << where;
            caption << relativeTime(a.createdAt);
            auto* row = makeRow(dot(annotationAccent(pal, a.color), icon), text, caption.join(QStringLiteral(" · ")));
            const qint64 doc = a.documentId;
            const qint64 id = a.id;
            connect(row, &QPushButton::clicked, this, [this, doc, id] { emit openItemRequested(doc, false, id); });
            m_annotationList->addWidget(row);
            m_annotationRows << row;
        }
    }
    m_annotationEmpty->setVisible(m_annotationRows.isEmpty());

    clearRows(m_commentList, m_commentRows);
    if (const auto coms = m_ctx.comments.recent(4); coms) {
        for (const CommentRecord& c : coms.value()) {
            QStringList caption{commentKindLabel(c.kind), elided(titleOf(c.documentId), 40)};
            if (const QString where = commentLocation(c); !where.isEmpty())
                caption << where;
            caption << relativeTime(c.updatedAt);
            const QColor tint = c.kind == CommentKind::Question ? pal.amber : pal.lavender;
            auto* row = makeRow(Icons::get(QStringLiteral("message-square"), tint, icon).pixmap(icon),
                                elided(plainSummary(c.body), 110), caption.join(QStringLiteral(" · ")));
            const qint64 doc = c.documentId;
            const qint64 id = c.id;
            connect(row, &QPushButton::clicked, this, [this, doc, id] { emit openItemRequested(doc, true, id); });
            m_commentList->addWidget(row);
            m_commentRows << row;
        }
    }
    m_commentEmpty->setVisible(m_commentRows.isEmpty());
}

void HomePage::refreshFavorites()
{
    for (QToolButton* b : std::as_const(m_favoriteButtons)) {
        m_favoriteRow->removeWidget(b);
        b->deleteLater();
    }
    m_favoriteButtons.clear();
    db::DocumentQuery q;
    q.favoritesOnly = true;
    q.sort = db::DocumentQuery::Sort::RecentlyOpened;
    const auto docs = m_ctx.library.documents(q);
    const int total = docs ? int(docs.value().size()) : 0;
    m_favoriteCount->setText(total > 0 ? QString::number(total) : QString());
    const int w = m_ctx.theme.px(44);
    const int h = m_ctx.theme.px(60);
    if (docs) {
        for (const DocumentRecord& d : docs.value()) {
            if (m_favoriteButtons.size() >= 5)
                break;
            auto* b = new QToolButton;
            b->setObjectName(QStringLiteral("FavoriteCover"));
            b->setCursor(Qt::PointingHandCursor);
            b->setIcon(QIcon(smallCover(d, w, h)));
            b->setIconSize(QSize(w, h));
            b->setToolTip(d.title);
            b->setAccessibleName(d.title);
            b->setAutoRaise(true);
            const qint64 id = d.id;
            connect(b, &QToolButton::clicked, this, [this, id] { emit openDocumentRequested(id); });
            m_favoriteRow->insertWidget(int(m_favoriteButtons.size()), b);
            m_favoriteButtons << b;
        }
    }
    m_favoriteEmpty->setVisible(m_favoriteButtons.isEmpty());
}

void HomePage::refreshNotes()
{
    for (QPushButton* b : std::as_const(m_noteChips)) {
        m_noteRow->removeWidget(b);
        b->deleteLater();
    }
    m_noteChips.clear();
    auto notes = m_ctx.notes.list();
    if (notes) {
        QList<NoteRecord> list = notes.value();
        std::sort(list.begin(), list.end(),
                  [](const NoteRecord& a, const NoteRecord& b) { return a.updatedAt > b.updatedAt; });
        for (const NoteRecord& n : std::as_const(list)) {
            if (m_noteChips.size() >= 4)
                break;
            auto* chip = new QPushButton(elided(n.title, 32));
            chip->setObjectName(QStringLiteral("NoteChip"));
            chip->setCursor(Qt::PointingHandCursor);
            chip->setToolTip(n.title);
            const qint64 id = n.id;
            connect(chip, &QPushButton::clicked, this, [this, id] { emit openNoteRequested(id); });
            m_noteRow->addWidget(chip);
            m_noteChips << chip;
        }
    }
    m_notesLabel->setVisible(!m_noteChips.isEmpty());
}

GoalsDialog* HomePage::editGoals()
{
    if (!m_ctx.activity)
        return nullptr;
    auto* dlg = new GoalsDialog(m_ctx, *m_ctx.activity, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
    return dlg;
}

}  // namespace studylab::ui
