#include "ui/pages/HomePage.h"

#include "data/repositories/DocumentRepository.h"
#include "services/annotations/AnnotationService.h"
#include "services/backup/BackupService.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "ui/components/Widgets.h"
#include "ui/library/CoverArt.h"
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

HomePage::HomePage(AppContext& ctx, QWidget* parent)
    : QWidget(parent)
    , m_ctx(ctx)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scaffold = new PageScaffold(this);
    outer->addWidget(scaffold);
    QVBoxLayout* body = scaffold->body();

    auto* header = new QVBoxLayout;
    header->setSpacing(2);
    m_greeting = makeLabel(QString(), "h1");
    header->addWidget(m_greeting);
    m_date = makeLabel(QString(), "subtitle");
    header->addWidget(m_date);
    body->addLayout(header);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(18);
    grid->addWidget(buildResume(), 0, 0);
    grid->addWidget(buildLibrary(), 0, 1);

    auto makeListCard = [this](const QString& title, QVBoxLayout*& list, QLabel*& empty, const QString& emptyText) {
        auto* card = makeCard();
        auto* l = static_cast<QVBoxLayout*>(card->layout());
        l->addWidget(makeLabel(title, "caption"));
        list = new QVBoxLayout;
        list->setSpacing(2);
        l->addLayout(list);
        empty = makeLabel(emptyText, "muted");
        empty->setWordWrap(true);
        l->addWidget(empty);
        l->addStretch(1);
        return card;
    };
    grid->addWidget(makeListCard(tr("DERNIÈRES ANNOTATIONS"), m_annotationList, m_annotationEmpty,
                                 tr("Vos surlignages et notes apparaîtront ici.")),
                    1, 0);
    grid->addWidget(makeListCard(tr("DERNIERS COMMENTAIRES"), m_commentList, m_commentEmpty,
                                 tr("Questions, explications et réflexions écrites pendant la lecture.")),
                    1, 1);
    grid->setColumnStretch(0, 3);
    grid->setColumnStretch(1, 2);
    body->addLayout(grid);
    body->addStretch(1);

    connect(&m_ctx.library, &LibraryService::documentsChanged, this, [this] {
        if (isVisible())
            refresh();
    });
    refresh();
}

QWidget* HomePage::buildResume()
{
    auto* card = makeCard();
    card->setObjectName(QStringLiteral("ResumeCard"));
    auto* l = static_cast<QVBoxLayout*>(card->layout());
    l->addWidget(makeLabel(tr("REPRENDRE LA LECTURE"), "caption"));

    m_resumeFilled = new QWidget(card);
    auto* row = new QHBoxLayout(m_resumeFilled);
    row->setContentsMargins(0, 4, 0, 0);
    row->setSpacing(16);
    m_cover = new ResumeCover(m_ctx.theme, m_resumeFilled);
    row->addWidget(m_cover, 0, Qt::AlignTop);
    auto* info = new QVBoxLayout;
    info->setSpacing(4);
    m_resumeTitle = makeLabel(QString(), "h2");
    m_resumeTitle->setWordWrap(true);
    m_resumeAuthor = makeLabel(QString(), "muted");
    m_resumePosition = makeLabel(QString(), "faint");
    m_resumeProgress = new QProgressBar;
    m_resumeProgress->setRange(0, 1000);
    m_resumeProgress->setTextVisible(false);
    m_resumeProgress->setFixedHeight(6);
    m_resumeQuote = makeLabel(QString(), "muted");
    m_resumeQuote->setWordWrap(true);
    m_continue = new QPushButton(tr("Continuer la lecture"));
    m_continue->setProperty("variant", QStringLiteral("primary"));
    m_continue->setCursor(Qt::PointingHandCursor);
    connect(m_continue, &QPushButton::clicked, this, [this] {
        if (m_resumeId > 0)
            emit openDocumentRequested(m_resumeId);
    });
    info->addWidget(m_resumeTitle);
    info->addWidget(m_resumeAuthor);
    info->addSpacing(4);
    info->addWidget(m_resumePosition);
    info->addWidget(m_resumeProgress);
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
    empty->setContentsMargins(0, 4, 0, 0);
    auto* emptyText = makeLabel(tr("Aucun document ouvert pour l'instant. Importez vos PDF et EPUB dans la "
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
    l->addWidget(m_resumeEmpty);

    l->addSpacing(6);
    l->addWidget(makeSeparator());
    l->addWidget(makeLabel(tr("AUSSI EN COURS"), "caption"));
    m_readingList = new QVBoxLayout;
    m_readingList->setSpacing(2);
    l->addLayout(m_readingList);
    m_readingEmpty = makeLabel(tr("Aucune autre lecture en cours."), "faint");
    l->addWidget(m_readingEmpty);
    l->addStretch(1);
    return card;
}

QWidget* HomePage::buildLibrary()
{
    auto* card = makeCard();
    auto* l = static_cast<QVBoxLayout*>(card->layout());
    l->addWidget(makeLabel(tr("VOTRE BIBLIOTHÈQUE"), "caption"));
    auto* countRow = new QHBoxLayout;
    m_docCount = makeLabel(QStringLiteral("0"), "big");
    countRow->addWidget(m_docCount);
    countRow->addWidget(makeLabel(tr("documents"), "muted"), 0, Qt::AlignBottom);
    countRow->addStretch(1);
    l->addLayout(countRow);
    m_docHint = makeLabel(QString(), "muted");
    m_docHint->setWordWrap(true);
    l->addWidget(m_docHint);
    auto* openLib = new QPushButton(tr("Ouvrir la bibliothèque"));
    openLib->setCursor(Qt::PointingHandCursor);
    connect(openLib, &QPushButton::clicked, this, [this] { emit navigateRequested(QStringLiteral("library")); });
    auto* libBtn = new QHBoxLayout;
    libBtn->addWidget(openLib);
    libBtn->addStretch(1);
    l->addLayout(libBtn);

    l->addSpacing(6);
    l->addWidget(makeSeparator());
    l->addWidget(makeLabel(tr("DONNÉES LOCALES"), "caption"));
    m_backup = makeLabel(QString(), "muted");
    m_backup->setWordWrap(true);
    l->addWidget(m_backup);
    m_dataDir = makeLabel(QString(), "mono");
    m_dataDir->setWordWrap(true);
    m_dataDir->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(m_dataDir);
    auto* settings = new QPushButton(tr("Sauvegardes…"));
    settings->setCursor(Qt::PointingHandCursor);
    connect(settings, &QPushButton::clicked, this, [this] { emit navigateRequested(QStringLiteral("settings")); });
    auto* folder = new QPushButton(tr("Ouvrir le dossier"));
    folder->setCursor(Qt::PointingHandCursor);
    connect(folder, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctx.paths.dataDir())); });
    auto* dataBtn = new QHBoxLayout;
    dataBtn->addWidget(settings);
    dataBtn->addWidget(folder);
    dataBtn->addStretch(1);
    l->addLayout(dataBtn);
    l->addStretch(1);
    return card;
}

QPushButton* HomePage::makeRow(const QPixmap& icon, const QString& text, const QString& caption)
{
    auto* row = new RowButton;
    row->setObjectName(QStringLiteral("HomeRow"));
    row->setCursor(Qt::PointingHandCursor);
    row->setAccessibleName(text + QStringLiteral(" — ") + caption);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(8, 6, 8, 6);
    h->setSpacing(10);
    auto* iconLabel = new QLabel;
    iconLabel->setPixmap(icon);
    iconLabel->setFixedWidth(m_ctx.theme.px(16));
    iconLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto* texts = new QVBoxLayout;
    texts->setSpacing(1);
    auto* main = makeLabel(text);
    auto* sub = makeLabel(caption, "faint");
    texts->addWidget(main);
    texts->addWidget(sub);
    h->addWidget(iconLabel);
    h->addLayout(texts, 1);
    for (QLabel* lbl : {iconLabel, main, sub}) {
        lbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        lbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // texte long : coupé, pas d'élargissement
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
    QString date = fr.toString(QDate::currentDate(), QStringLiteral("dddd d MMMM yyyy"));
    if (!date.isEmpty())
        date[0] = date[0].toUpper();
    m_date->setText(date);

    const qint64 n = m_ctx.library.count();
    m_docCount->setText(fr.toString(n));
    if (n == 0) {
        m_docHint->setText(tr("Importez vos PDF et EPUB depuis la bibliothèque, ou glissez-déposez-les dans la fenêtre."));
    } else {
        db::DocumentQuery q;
        q.status = ReadingStatus::Reading;
        const auto reading = m_ctx.library.documents(q);
        const int r = reading ? int(reading.value().size()) : 0;
        m_docHint->setText(tr("%1 · %2").arg(countLabel(r, tr("en cours de lecture"), tr("en cours de lecture")),
                                             CoverArt::formatFileSize(m_ctx.library.totalFileSize())));
    }
    if (const auto last = m_ctx.backups.latest()) {
        m_backup->setText(tr("Dernière sauvegarde : %1 (%2).")
                              .arg(fr.toString(last->createdAt, QStringLiteral("dddd d MMMM 'à' HH:mm")),
                                   backupKindLabel(last->kind).toLower()));
    } else {
        m_backup->setText(tr("Aucune sauvegarde pour l'instant : la première est faite au prochain démarrage."));
    }
    m_dataDir->setText(QDir::toNativeSeparators(m_ctx.paths.dataDir()));

    refreshResume();
    refreshActivity();
}

void HomePage::refreshResume()
{
    db::DocumentQuery q;
    q.sort = db::DocumentQuery::Sort::RecentlyOpened;
    q.limit = 6;
    const auto docs = m_ctx.library.documents(q);
    QList<DocumentRecord> opened;
    if (docs)
        for (const auto& d : docs.value())
            if (!d.lastOpenedAt.isEmpty())
                opened << d;

    m_resumeId = opened.isEmpty() ? 0 : opened.first().id;
    m_resumeFilled->setVisible(m_resumeId > 0);
    m_resumeEmpty->setVisible(m_resumeId == 0);
    if (m_resumeId > 0) {
        const DocumentRecord& d = opened.first();
        m_cover->setDocument(d, QImage(m_ctx.library.coverFile(d)));
        m_resumeTitle->setText(d.title);
        m_resumeAuthor->setText(d.authors.join(QStringLiteral(", ")));
        m_resumeAuthor->setVisible(!d.authors.isEmpty());
        const auto pos = m_ctx.library.readingPosition(d.id);
        const double progress = pos ? pos->progress : d.progress;
        const int percent = int(progress * 100 + 0.5);
        QString where;
        if (d.format == DocumentFormat::Pdf && pos && pos->locator.contains(QStringLiteral("page")) && d.pageCount > 0)
            where = tr("Page %1 sur %2 · %3 %").arg(pos->locator.value(QStringLiteral("page")).toInt() + 1).arg(d.pageCount).arg(percent);
        else
            where = tr("%1 % lu").arg(percent);
        where += QStringLiteral(" · ") + tr("ouvert %1").arg(relativeTime(d.lastOpenedAt));
        m_resumePosition->setText(where);
        m_resumeProgress->setValue(int(progress * 1000));
        QString quote;
        if (const auto anns = m_ctx.annotations.list(d.id); anns) {
            const AnnotationRecord* latest = nullptr;
            for (const auto& a : anns.value())
                if (!a.selectedText.isEmpty() && (!latest || a.createdAt > latest->createdAt))
                    latest = &a;
            if (latest)
                quote = tr("Dernière annotation : « %1 »").arg(elided(normalizeQuote(latest->selectedText), 140));
        }
        m_resumeQuote->setText(quote);
        m_resumeQuote->setVisible(!quote.isEmpty());
    }

    clearRows(m_readingList, m_readingRows);
    const Palette& pal = m_ctx.theme.palette();
    for (int i = 1; i < opened.size() && m_readingRows.size() < 3; ++i) {
        const DocumentRecord& d = opened.at(i);
        QStringList caption;
        if (!d.authors.isEmpty())
            caption << d.authors.first();
        caption << tr("%1 % lu").arg(int(d.progress * 100 + 0.5));
        auto* row = makeRow(Icons::get(d.format == DocumentFormat::Epub ? QStringLiteral("book-open") : QStringLiteral("book"),
                                       pal.text3, m_ctx.theme.px(16))
                                .pixmap(m_ctx.theme.px(16)),
                            elided(d.title, 70), caption.join(QStringLiteral(" · ")));
        const qint64 id = d.id;
        connect(row, &QPushButton::clicked, this, [this, id] { emit openDocumentRequested(id); });
        m_readingList->addWidget(row);
        m_readingRows << row;
    }
    m_readingEmpty->setVisible(m_readingRows.isEmpty());
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
    if (const auto anns = m_ctx.annotations.recent(5); anns) {
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
    if (const auto coms = m_ctx.comments.recent(5); coms) {
        for (const CommentRecord& c : coms.value()) {
            QStringList caption{commentKindLabel(c.kind), elided(titleOf(c.documentId), 40)};
            if (const QString where = commentLocation(c); !where.isEmpty())
                caption << where;
            caption << relativeTime(c.updatedAt);
            const QColor tint = c.kind == CommentKind::Question ? pal.amber : pal.lavender;
            auto* row = makeRow(Icons::get(QStringLiteral("message-square"), tint, icon).pixmap(icon),
                                elided(plainSummary(c.body), 90), caption.join(QStringLiteral(" · ")));
            const qint64 doc = c.documentId;
            const qint64 id = c.id;
            connect(row, &QPushButton::clicked, this, [this, doc, id] { emit openItemRequested(doc, true, id); });
            m_commentList->addWidget(row);
            m_commentRows << row;
        }
    }
    m_commentEmpty->setVisible(m_commentRows.isEmpty());
}

}  // namespace studylab::ui
