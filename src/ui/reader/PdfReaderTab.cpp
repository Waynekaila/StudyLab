#include "ui/reader/PdfReaderTab.h"

#include "ui/reader/ReadingClock.h"

#include "engines/pdf/PdfRenderQueue.h"
#include "services/annotations/AnnotationService.h"
#include "services/export/MarkdownExporter.h"
#include "services/comments/CommentService.h"
#include "services/library/LibraryService.h"
#include "services/search/SearchService.h"
#include "services/settings/SettingsService.h"
#include "ui/reader/AnnotationPopup.h"
#include "ui/reader/AnnotationStyle.h"
#include "ui/reader/AnnotationsPane.h"
#include "ui/shell/AppContext.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/Widgets.h"
#include "ui/reader/ThumbnailModel.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStandardPaths>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QIntValidator>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPdfSearchModel>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace studylab::ui {

namespace {

/// Miniature : image de la page, cadre de la page courante, numéro.
class ThumbDelegate : public QStyledItemDelegate {
public:
    ThumbDelegate(ThemeManager& theme, PdfView* view, PdfRenderQueue* queue, QObject* parent)
        : QStyledItemDelegate(parent), m_theme(theme), m_view(view), m_queue(queue)
    {
    }
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        const QSizeF pt = m_queue ? m_queue->pagePointSize(index.row()) : QSizeF(1, 1.4);
        const qreal ratio = pt.width() > 0 ? pt.height() / pt.width() : 1.4;
        return {m_theme.px(160), int(m_theme.px(112) * qMin(ratio, 2.0)) + m_theme.px(30)};
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const override
    {
        const Palette& pal = m_theme.palette();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        const QRect r = opt.rect;
        const int w = m_theme.px(112);
        const int h = r.height() - m_theme.px(30);
        const QRect img(r.center().x() - w / 2, r.top() + m_theme.px(6), w, h);
        const bool current = index.row() == m_view->currentPage();
        if (opt.state & QStyle::State_MouseOver) {
            p->setPen(Qt::NoPen);
            p->setBrush(pal.sunken);
            p->drawRoundedRect(img.adjusted(-5, -5, 5, 5), 6, 6);
        }
        p->fillRect(img, Qt::white);
        const QImage image = index.data(ThumbnailModel::ImageRole).value<QImage>();
        if (!image.isNull())
            p->drawImage(img, image);
        p->setPen(QPen(current ? pal.primary : pal.border, current ? 2.5 : 1));
        p->setBrush(Qt::NoBrush);
        p->drawRect(QRectF(img).adjusted(-0.5, -0.5, 0.5, 0.5));
        const QVariant mark = index.data(ThumbnailModel::MarkRole);
        if (mark.isValid()) {
            const int d = m_theme.px(8);
            p->setPen(QPen(Qt::white, 1.5));
            p->setBrush(mark.value<QColor>());
            p->drawEllipse(QRectF(img.right() - d - m_theme.px(5), img.top() + m_theme.px(5), d, d));
        }
        QFont f = opt.font;
        f.setPixelSize(m_theme.px(11));
        f.setWeight(current ? QFont::DemiBold : QFont::Normal);
        p->setFont(f);
        p->setPen(current ? pal.primaryStrong : pal.text3);
        p->drawText(QRect(r.left(), img.bottom() + m_theme.px(4), r.width(), m_theme.px(18)), Qt::AlignCenter,
                    index.data(ThumbnailModel::LabelRole).toString());
        p->restore();
    }

private:
    ThemeManager& m_theme;
    PdfView* m_view;
    PdfRenderQueue* m_queue;
};

QToolButton* toolButton(const QString& tooltip, QWidget* parent, bool checkable = false)
{
    return ReaderTab::makeToolButton(tooltip, parent, checkable);
}

}  // namespace

PdfReaderTab::PdfReaderTab(AppContext& ctx, qint64 documentId, QWidget* parent)
    : ReaderTab(ctx, documentId, parent)
{
    ThemeManager& theme = m_theme;

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_toolbar = buildToolbar();
    v->addWidget(m_toolbar);
    v->addWidget(buildFileBanner());
    m_searchBar = buildSearchBar();
    v->addWidget(m_searchBar);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    m_view = new PdfView(theme, this);
    body->addWidget(buildSidePanel());
    body->addWidget(m_view, 1);
    body->addWidget(createSidePanes(false));
    v->addLayout(body, 1);
    m_popup = new AnnotationPopup(theme, m_view->viewport());

    m_errorBox = new QWidget(this);
    auto* ev = new QVBoxLayout(m_errorBox);
    ev->addStretch(1);
    auto* errLabel = makeLabel(QString(), "muted");
    errLabel->setObjectName(QStringLiteral("ReaderError"));
    errLabel->setAlignment(Qt::AlignCenter);
    errLabel->setWordWrap(true);
    ev->addWidget(errLabel);
    ev->addStretch(1);
    m_errorBox->hide();
    body->addWidget(m_errorBox, 1);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);
    connect(&m_saveTimer, &QTimer::timeout, this, &PdfReaderTab::savePosition);
    m_ocrReload.setSingleShot(true);
    m_ocrReload.setInterval(1500);
    connect(&m_ocrReload, &QTimer::timeout, this, &PdfReaderTab::loadOcrText);
    m_searchDebounce.setSingleShot(true);
    m_searchDebounce.setInterval(250);
    connect(&m_searchDebounce, &QTimer::timeout, this, [this] {
        // Un « suivant » demandé avant la fin de la saisie (passage ouvert depuis la recherche) reste en attente.
        const bool pending = m_findPending;
        if (m_searchModel)
            m_searchModel->setSearchString(m_searchEdit->text().trimmed());
        m_view->setOcrSearchString(m_searchEdit->text().trimmed());
        m_view->setCurrentSearchResult(-1);
        m_findPending = pending;
        if (pending && m_view->searchResultCount() > 0)
            findNext();
        else
            updateSearchCount();
    });

    connect(m_view, &PdfView::currentPageChanged, this, [this] {
        updatePageIndicator();
        if (m_thumbs && m_thumbModel) {
            m_thumbs->viewport()->update();
            m_thumbs->scrollTo(m_thumbModel->index(m_view->currentPage()), QAbstractItemView::EnsureVisible);
        }
    });
    connect(m_view, &PdfView::positionChanged, this, [this] { m_saveTimer.start(); });
    connect(m_view, &PdfView::zoomChanged, this, [this] {
        updateZoomCombo();
        m_saveTimer.start();
    });
    connect(m_view, &PdfView::historyChanged, this, &PdfReaderTab::updateHistoryButtons);
    connect(&theme, &ThemeManager::themeChanged, this, [this] {
        refreshIcons();
        pushMarks();
    });

    // Annotations : lecteur, barre flottante, volet
    connect(m_view, &PdfView::currentPageChanged, this, [this](int page) {
        m_pane->setCurrentPage(page);
        m_comments->setCurrentUnit(page);
        updateBookmarkButton();
    });
    connect(m_view, &PdfView::positionChanged, m_popup, &QWidget::hide);
    connect(m_view, &PdfView::zoomChanged, m_popup, &QWidget::hide);
    connect(m_view, &PdfView::selectionChanged, this, [this] {
        if (!m_view->hasSelection() && m_popup->mode() == AnnotationPopup::Mode::Create)
            m_popup->hide();
    });
    connect(m_view, &PdfView::selectionFinished, this, &PdfReaderTab::onSelectionFinished);
    connect(m_view, &PdfView::annotationClicked, this, [this](qint64 id) {
        m_pane->setActive(id);
        showEditPopup(id);
    });
    connect(m_view, &PdfView::annotationContextMenuRequested, this, &PdfReaderTab::showAnnotationMenu);
    connect(m_view, &PdfView::areaDrawn, this, [this](int page, const QRectF& rect) {
        if (auto r = addArea(page, rect); !r)
            reportError(r.error());
    });
    connect(m_view, &PdfView::pointClicked, this, [this](int page, const QPointF& pt) {
        const auto text = askNote(QString(), QString());
        m_view->setTool(PdfView::Tool::Select);
        if (text && !text->trimmed().isEmpty()) {
            if (auto r = addNote(page, pt, *text); !r)
                reportError(r.error());
        }
    });
    connect(m_view, &PdfView::toolChanged, this, [this](PdfView::Tool tool) {
        for (auto it = m_toolButtons.cbegin(); it != m_toolButtons.cend(); ++it) {
            const QSignalBlocker block(it.value());
            it.value()->setChecked(PdfView::Tool(it.key()) == tool);
        }
        m_popup->hide();
    });

    initAnnotations();

    auto* find = new QShortcut(QKeySequence::Find, this, this, &PdfReaderTab::showSearch);
    find->setContext(Qt::WidgetWithChildrenShortcut);
    auto* back = new QShortcut(QKeySequence::Back, this, m_view, &PdfView::goBack);
    back->setContext(Qt::WidgetWithChildrenShortcut);
    auto* fwd = new QShortcut(QKeySequence::Forward, this, m_view, &PdfView::goForward);
    fwd->setContext(Qt::WidgetWithChildrenShortcut);
    auto* goTo = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this, [this] {
        m_pageEdit->setFocus();
        m_pageEdit->selectAll();
    });
    goTo->setContext(Qt::WidgetWithChildrenShortcut);

    refreshIcons();
    updateHistoryButtons();
}

PdfReaderTab::~PdfReaderTab()
{
    if (m_loaded)
        savePosition();
    // La vue et les modèles référencent le document : on les détache avant de le détruire.
    m_view->setDocument(nullptr, nullptr);
    m_view->setSearchModel(nullptr);
    if (m_thumbs)
        m_thumbs->setModel(nullptr);
    if (m_outline)
        m_outline->setModel(nullptr);
    delete m_searchModel;
    delete m_bookmarks;
    delete m_thumbModel;
    m_queue.reset();
    m_doc.reset();
}

QWidget* PdfReaderTab::buildToolbar()
{
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("ReaderToolbar"));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(10, 6, 10, 6);
    h->setSpacing(4);

    m_sideToggle = toolButton(tr("Miniatures et sommaire"), bar, true);
    m_sideToggle->setChecked(true);
    connect(m_sideToggle, &QToolButton::toggled, this, [this](bool on) { m_side->setVisible(on); });
    h->addWidget(m_sideToggle);
    h->addSpacing(6);

    m_back = toolButton(tr("Précédent (Alt+←)"), bar);
    m_forward = toolButton(tr("Suivant (Alt+→)"), bar);
    connect(m_back, &QToolButton::clicked, this, [this] { m_view->goBack(); });
    connect(m_forward, &QToolButton::clicked, this, [this] { m_view->goForward(); });
    h->addWidget(m_back);
    h->addWidget(m_forward);
    h->addSpacing(10);

    m_pageEdit = new QLineEdit(bar);
    m_pageEdit->setObjectName(QStringLiteral("PageEdit"));
    m_pageEdit->setAlignment(Qt::AlignCenter);
    m_pageEdit->setFixedWidth(m_theme.px(56));
    m_pageEdit->setAccessibleName(tr("Numéro de page"));
    m_pageEdit->setToolTip(tr("Aller à la page (Ctrl+G)"));
    connect(m_pageEdit, &QLineEdit::returnPressed, this, [this] {
        bool ok = false;
        const int page = m_pageEdit->text().trimmed().toInt(&ok);
        if (ok && page >= 1 && page <= m_view->pageCount())
            m_view->goToPage(page - 1, -1, true);
        updatePageIndicator();
        m_view->setFocus();
    });
    m_pageTotal = makeLabel(QString(), "muted");
    // R16 : le texte reconnu est signalé, il peut contenir des erreurs.
    m_ocrBadge = makeLabel(tr("Texte OCR"), "faint");
    m_ocrBadge->setObjectName(QStringLiteral("OcrBadge"));
    m_ocrBadge->setToolTip(tr("Le texte de cette page numérisée a été reconnu automatiquement (OCR) : "
                              "il peut contenir des erreurs."));
    m_ocrBadge->hide();
    h->addWidget(m_pageEdit);
    h->addWidget(m_pageTotal);
    h->addWidget(m_ocrBadge);
    h->addSpacing(12);

    m_zoomOut = toolButton(tr("Zoom arrière (Ctrl+−)"), bar);
    connect(m_zoomOut, &QToolButton::clicked, this, [this] { m_view->zoomOut(); });
    m_zoomCombo = new QComboBox(bar);
    m_zoomCombo->setAccessibleName(tr("Zoom"));
    m_zoomCombo->addItem(tr("Ajuster à la largeur"), -1);
    m_zoomCombo->addItem(tr("Page entière"), -2);
    for (int z : {50, 75, 100, 125, 150, 200, 300, 400})
        m_zoomCombo->addItem(QStringLiteral("%1 %").arg(z), z);
    m_zoomCombo->setMinimumContentsLength(14);
    m_zoomCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_zoomCombo, &QComboBox::activated, this, [this](int i) {
        const int v = m_zoomCombo->itemData(i).toInt();
        if (v == -1)
            m_view->setZoomMode(PdfView::ZoomMode::FitWidth);
        else if (v == -2)
            m_view->setZoomMode(PdfView::ZoomMode::FitPage);
        else if (v > 0)
            m_view->setZoom(v / 100.0);
        m_view->setFocus();
    });
    m_zoomIn = toolButton(tr("Zoom avant (Ctrl++)"), bar);
    connect(m_zoomIn, &QToolButton::clicked, this, [this] { m_view->zoomIn(); });
    h->addWidget(m_zoomOut);
    h->addWidget(m_zoomCombo);
    h->addWidget(m_zoomIn);
    h->addSpacing(8);

    m_rotate = toolButton(tr("Pivoter de 90°"), bar);
    connect(m_rotate, &QToolButton::clicked, this, [this] {
        m_view->rotateClockwise();
        m_saveTimer.start();
    });
    m_double = toolButton(tr("Double page"), bar, true);
    connect(m_double, &QToolButton::toggled, this, [this](bool on) {
        m_view->setDoublePage(on);
        m_saveTimer.start();
    });
    h->addWidget(m_rotate);
    h->addWidget(m_double);
    h->addStretch(1);

    // Outils d'annotation : un clic active l'outil, un second clic revient à la sélection simple.
    const struct {
        PdfView::Tool tool;
        const char* tip;
    } tools[] = {
        {PdfView::Tool::Highlight, QT_TR_NOOP("Surligner : sélectionnez un passage")},
        {PdfView::Tool::Underline, QT_TR_NOOP("Souligner : sélectionnez un passage")},
        {PdfView::Tool::Strikeout, QT_TR_NOOP("Barrer : sélectionnez un passage")},
        {PdfView::Tool::Marker, QT_TR_NOOP("Zone : tracez un rectangle (figure, formule, tableau)")},
        {PdfView::Tool::Note, QT_TR_NOOP("Note : cliquez à l'endroit voulu")},
    };
    auto* toolGroup = new QFrame(bar);
    toolGroup->setObjectName(QStringLiteral("ToolGroup"));
    auto* th = new QHBoxLayout(toolGroup);
    th->setContentsMargins(3, 2, 3, 2);
    th->setSpacing(1);
    for (const auto& t : tools) {
        auto* b = toolButton(tr(t.tip), toolGroup, true);
        const PdfView::Tool tool = t.tool;
        connect(b, &QToolButton::clicked, this, [this, tool](bool on) {
            m_view->setTool(on ? tool : PdfView::Tool::Select);
            m_view->setFocus();
        });
        th->addWidget(b);
        m_toolButtons.insert(int(tool), b);
    }
    m_bookmarkButton = toolButton(tr("Signet sur cette page (Ctrl+D)"), toolGroup, true);
    connect(m_bookmarkButton, &QToolButton::clicked, this, [this] { toggleBookmark(); });
    th->addWidget(m_bookmarkButton);
    h->addWidget(toolGroup);
    h->addSpacing(6);
    addColorButtons(h, bar);
    h->addSpacing(10);

    m_searchButton = toolButton(tr("Rechercher dans le document (Ctrl+F)"), bar);
    connect(m_searchButton, &QToolButton::clicked, this, &PdfReaderTab::showSearch);
    m_focusButton = toolButton(tr("Lecture sans distraction (Échap pour quitter)"), bar, true);
    connect(m_focusButton, &QToolButton::toggled, this, &PdfReaderTab::setFocusMode);
    m_fullButton = toolButton(tr("Plein écran (F11)"), bar);
    connect(m_fullButton, &QToolButton::clicked, this, &PdfReaderTab::fullScreenToggled);
    h->addWidget(m_searchButton);
    h->addWidget(m_focusButton);
    h->addWidget(m_fullButton);
    m_moreButton = toolButton(tr("Autres actions"), bar);
    m_moreButton->setObjectName(QStringLiteral("ReaderMoreButton"));
    m_moreButton->setPopupMode(QToolButton::InstantPopup);
    m_moreMenu = new QMenu(m_moreButton);
    m_moreMenu->addAction(tr("Importer les annotations du PDF"), this, [this] { (void)importFileAnnotations(); });
    m_moreMenu->addAction(tr("Exporter une copie annotée du PDF…"), this, &PdfReaderTab::exportAnnotatedCopyWithDialog);
    m_moreMenu->addSeparator();
    m_fileAnnotationsAction = m_moreMenu->addAction(tr("Afficher les annotations enregistrées dans le fichier"));
    m_fileAnnotationsAction->setCheckable(true);
    m_fileAnnotationsAction->setChecked(true);
    connect(m_fileAnnotationsAction, &QAction::toggled, this, &PdfReaderTab::setFileAnnotationsVisible);
    m_moreButton->setMenu(m_moreMenu);
    // Image : pas de fichier PDF à échanger, le menu n'a rien à proposer.
    m_moreButton->setVisible(m_record.format == DocumentFormat::Pdf);
    h->addWidget(m_moreButton);
    h->addSpacing(6);
    addPaneToggle(h, bar);
    return bar;
}

QWidget* PdfReaderTab::buildSidePanel()
{
    m_side = new QFrame(this);
    m_side->setObjectName(QStringLiteral("ReaderSide"));
    m_side->setFixedWidth(m_theme.px(224));
    auto* v = new QVBoxLayout(m_side);
    v->setContentsMargins(10, 10, 10, 0);
    v->setSpacing(8);
    m_sideTabs = new SegmentedControl({tr("Miniatures"), tr("Sommaire")}, m_side);
    m_sideTabs->setCurrentIndex(0);
    v->addWidget(m_sideTabs);
    m_sideStack = new QStackedWidget(m_side);
    m_thumbs = new QListView(m_sideStack);
    m_thumbs->setObjectName(QStringLiteral("Thumbnails"));
    m_thumbs->setFrameShape(QFrame::NoFrame);
    m_thumbs->setUniformItemSizes(false);
    m_thumbs->setMouseTracking(true);
    m_thumbs->viewport()->setAttribute(Qt::WA_Hover);
    m_thumbs->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_thumbs->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbs->setAccessibleName(tr("Miniatures des pages"));
    m_sideStack->addWidget(m_thumbs);

    auto* outlineBox = new QWidget(m_sideStack);
    auto* ov = new QVBoxLayout(outlineBox);
    ov->setContentsMargins(0, 0, 0, 0);
    m_outline = new QTreeView(outlineBox);
    m_outline->setObjectName(QStringLiteral("Outline"));
    m_outline->setHeaderHidden(true);
    m_outline->setFrameShape(QFrame::NoFrame);
    m_outline->setAccessibleName(tr("Sommaire"));
    m_outline->setUniformRowHeights(true);
    m_noOutline = makeLabel(tr("Ce document n'a pas de sommaire."), "faint");
    m_noOutline->setWordWrap(true);
    m_noOutline->setAlignment(Qt::AlignCenter);
    ov->addWidget(m_outline, 1);
    ov->addWidget(m_noOutline);
    ov->addStretch(0);
    m_sideStack->addWidget(outlineBox);
    v->addWidget(m_sideStack, 1);
    connect(m_sideTabs, &SegmentedControl::currentIndexChanged, m_sideStack, &QStackedWidget::setCurrentIndex);
    return m_side;
}

QWidget* PdfReaderTab::buildSearchBar()
{
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("ReaderSearchBar"));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 6, 12, 6);
    h->setSpacing(6);
    m_searchEdit = new QLineEdit(bar);
    m_searchEdit->setPlaceholderText(tr("Rechercher dans le document"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setAccessibleName(tr("Rechercher dans le document"));
    m_searchEdit->setMaximumWidth(m_theme.px(360));
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { m_searchDebounce.start(); });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] {
        if (m_searchDebounce.isActive()) {
            m_searchDebounce.stop();
            m_searchModel->setSearchString(m_searchEdit->text().trimmed());
            m_view->setOcrSearchString(m_searchEdit->text().trimmed());
        }
        findNext();
    });
    m_searchCount = makeLabel(QString(), "faint");
    auto* prev = new QPushButton(tr("Précédent"), bar);
    auto* next = new QPushButton(tr("Suivant"), bar);
    prev->setProperty("variant", QStringLiteral("ghost"));
    next->setProperty("variant", QStringLiteral("ghost"));
    connect(prev, &QPushButton::clicked, this, &PdfReaderTab::findPrevious);
    connect(next, &QPushButton::clicked, this, &PdfReaderTab::findNext);
    auto* close = new QPushButton(tr("Fermer"), bar);
    close->setProperty("variant", QStringLiteral("ghost"));
    connect(close, &QPushButton::clicked, this, [this] {
        m_searchEdit->clear();
        m_searchBar->hide();
        m_view->setFocus();
    });
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), m_searchEdit, [close] { close->click(); });
    esc->setContext(Qt::WidgetShortcut);
    h->addWidget(m_searchEdit, 1);
    h->addWidget(m_searchCount);
    h->addWidget(prev);
    h->addWidget(next);
    h->addStretch(1);
    h->addWidget(close);
    bar->hide();
    return bar;
}

void PdfReaderTab::refreshIcons()
{
    const Palette& pal = m_theme.palette();
    auto set = [&](QToolButton* b, const char* icon) {
        if (b)
            b->setIcon(Icons::stateful(QString::fromLatin1(icon), pal.text2, pal.primaryStrong, 18));
    };
    set(m_sideToggle, "panel-left");
    set(m_back, "arrow-left");
    set(m_forward, "arrow-right");
    set(m_zoomOut, "minus");
    set(m_zoomIn, "plus");
    set(m_rotate, "rotate-cw");
    set(m_double, "book-open");
    set(m_searchButton, "search");
    set(m_focusButton, "eye");
    set(m_fullButton, "maximize");
    set(m_moreButton, "more");
    set(m_bookmarkButton, "bookmark");
    const char* toolIcons[] = {"", "highlighter", "underline", "strikethrough", "square-dashed", "message-square"};
    for (auto it = m_toolButtons.cbegin(); it != m_toolButtons.cend(); ++it)
        set(it.value(), toolIcons[it.key()]);
    refreshColorIcons();
}

void PdfReaderTab::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_loaded && !m_loadFailed)
        QTimer::singleShot(0, this, [this] { (void)ensureLoaded(); });
}

Result<void> PdfReaderTab::ensureLoaded()
{
    if (m_loaded)
        return {};
    auto fail = [this](const QString& message) -> Result<void> {
        m_loadFailed = true;
        m_view->hide();
        m_side->hide();
        m_toolbar->setEnabled(false);
        m_errorBox->findChild<QLabel*>(QStringLiteral("ReaderError"))->setText(message);
        m_errorBox->show();
        emit loadFailed(message);
        return Error::make(Error::Code::Io, message);
    };
    if (m_record.id == 0)
        return fail(tr("Ce document n'existe plus dans la bibliothèque."));
    const QString original = m_library.documentFile(m_record);
    if (!QFileInfo::exists(original))
        return fail(tr("Le fichier est introuvable :\n%1").arg(QDir::toNativeSeparators(original)));
    auto readable = m_library.readableFile(m_record);  // image : PDF d'affichage construit à la demande
    if (!readable)
        return fail(tr("Cette image ne peut pas être affichée : %1").arg(readable.error().message));
    const QString file = readable.value();

    m_doc = std::make_unique<QPdfDocument>();
    QPdfDocument::Error err = m_doc->load(file);
    for (int attempt = 1; err == QPdfDocument::Error::IncorrectPassword && attempt <= 3; ++attempt) {
        QString password;
        if (m_passwordProvider) {
            password = m_passwordProvider(attempt);
        } else {
            bool ok = false;
            password = QInputDialog::getText(window(), tr("Document protégé"),
                                             attempt == 1 ? tr("Mot de passe de « %1 » :").arg(m_record.title)
                                                          : tr("Mot de passe incorrect. Réessayez :"),
                                             QLineEdit::Password, QString(), &ok);
            if (!ok)
                break;
        }
        if (password.isEmpty())
            break;
        m_doc->setPassword(password);
        err = m_doc->load(file);
    }
    if (err == QPdfDocument::Error::IncorrectPassword)
        return fail(tr("Ce document est protégé par un mot de passe."));
    if (err != QPdfDocument::Error::None || m_doc->pageCount() <= 0)
        return fail(tr("Ce PDF ne peut pas être ouvert : il est peut-être endommagé."));

    m_queue = std::make_unique<PdfRenderQueue>(m_doc.get());
    if (m_settings.value(QStringLiteral("pdf/hideFileAnnotations/%1").arg(m_documentId)).toBool()) {
        m_queue->setFileAnnotationsVisible(false);
        const QSignalBlocker block(m_fileAnnotationsAction);
        m_fileAnnotationsAction->setChecked(false);
    }
    m_view->setDocument(m_doc.get(), m_queue.get());
    if (auto ocr = m_ctx.search.ocrPages(m_documentId))
        m_view->setOcrPages(ocr.value());

    m_searchModel = new QPdfSearchModel;
    m_searchModel->setDocument(m_doc.get());
    m_view->setSearchModel(m_searchModel);
    const auto onResults = [this] {
        if (m_findPending && m_view->searchResultCount() > 0)
            findNext();
        else
            updateSearchCount();
    };
    connect(m_searchModel, &QAbstractItemModel::rowsInserted, this, onResults);
    connect(m_searchModel, &QAbstractItemModel::modelReset, this, onResults);

    m_thumbModel = new ThumbnailModel(m_doc.get(), m_queue.get());
    m_thumbs->setModel(m_thumbModel);
    m_thumbs->setItemDelegate(new ThumbDelegate(m_theme, m_view, m_queue.get(), m_thumbs));
    connect(m_thumbs, &QListView::clicked, this, [this](const QModelIndex& idx) { m_view->goToPage(idx.row(), -1, true); });

    m_bookmarks = new QPdfBookmarkModel;
    m_bookmarks->setDocument(m_doc.get());
    m_outline->setModel(m_bookmarks);
    m_outline->expandToDepth(0);
    const bool hasOutline = m_bookmarks->rowCount({}) > 0;
    m_outline->setVisible(hasOutline);
    m_noOutline->setVisible(!hasOutline);
    connect(m_outline, &QTreeView::activated, this, [this](const QModelIndex& idx) {
        const int page = idx.data(int(QPdfBookmarkModel::Role::Page)).toInt();
        const QPointF loc = idx.data(int(QPdfBookmarkModel::Role::Location)).toPointF();
        m_view->goToPage(page, loc.isNull() ? -1 : loc.y(), true);
    });
    connect(m_outline, &QTreeView::clicked, m_outline, &QTreeView::activated);

    m_loaded = true;
    m_pageTotal->setText(QStringLiteral("/ %1").arg(m_view->pageCount()));
    buildChapters();
    reloadAnnotations();
    restoreState();
    updatePageIndicator();
    updateZoomCombo();
    (void)m_library.markOpened(m_documentId);
    flushPendingReveal();
    if (m_record.format == DocumentFormat::Pdf)
        startFileAnnotationScan();
    return {};
}

// ───────────────────────── Interopérabilité PDF (V2-5) ─────────────────────────

QWidget* PdfReaderTab::buildFileBanner()
{
    auto* wrap = new QWidget(this);
    auto* outer = new QVBoxLayout(wrap);
    outer->setContentsMargins(m_theme.px(12), m_theme.px(8), m_theme.px(12), 0);
    m_fileBanner = new QFrame(wrap);
    m_fileBanner->setObjectName(QStringLiteral("ImportBanner"));
    auto* h = new QHBoxLayout(m_fileBanner);
    h->setContentsMargins(m_theme.px(14), m_theme.px(8), m_theme.px(8), m_theme.px(8));
    h->setSpacing(m_theme.px(8));
    m_fileBannerText = makeLabel(QString());
    m_fileBannerText->setObjectName(QStringLiteral("FileAnnotationsBannerText"));
    m_fileBannerText->setWordWrap(true);
    h->addWidget(m_fileBannerText, 1);
    m_fileBannerImport = new QPushButton(tr("Importer"), m_fileBanner);
    m_fileBannerImport->setProperty("variant", QStringLiteral("primary"));
    connect(m_fileBannerImport, &QPushButton::clicked, this, [this] { (void)importFileAnnotations(); });
    h->addWidget(m_fileBannerImport);
    m_fileBannerIgnore = new QPushButton(tr("Ignorer"), m_fileBanner);
    m_fileBannerIgnore->setProperty("variant", QStringLiteral("ghost"));
    connect(m_fileBannerIgnore, &QPushButton::clicked, this, [this, wrap] {
        // « Ignorer » sur la proposition d'import : elle ne revient plus pour ce document.
        if (m_fileBannerImport->isVisible())
            m_settings.setValue(QStringLiteral("pdf/ignoreFileAnnotations/%1").arg(m_documentId), true);
        wrap->hide();
    });
    h->addWidget(m_fileBannerIgnore);
    outer->addWidget(m_fileBanner);
    wrap->hide();
    return wrap;
}

void PdfReaderTab::showFileBanner(const QString& text, const QString& state, bool offerImport)
{
    m_fileBannerText->setText(text);
    m_fileBanner->setProperty("state", state);
    m_fileBanner->style()->unpolish(m_fileBanner);
    m_fileBanner->style()->polish(m_fileBanner);
    m_fileBannerImport->setVisible(offerImport);
    m_fileBannerIgnore->setText(offerImport ? tr("Ignorer") : tr("Fermer"));
    m_fileBanner->parentWidget()->show();
}

PdfInteropService PdfReaderTab::interop()
{
    return PdfInteropService(m_library, m_annotations, m_ctx.comments);
}

void PdfReaderTab::startFileAnnotationScan()
{
    using Scan = std::optional<pdf::PdfAnnotationScan>;
    const QString path = m_library.documentFile(m_record);
    auto* watcher = new QFutureWatcher<Scan>(this);
    connect(watcher, &QFutureWatcher<Scan>::finished, this, [this, watcher] {
        watcher->deleteLater();
        const Scan scan = watcher->result();
        int fresh = 0;
        if (scan) {
            if (auto preview = interop().preview(m_documentId, *scan))
                fresh = preview.value().fresh;
        }
        const bool ignored = m_settings.value(QStringLiteral("pdf/ignoreFileAnnotations/%1").arg(m_documentId)).toBool();
        if (fresh > 0 && !ignored)
            showFileBanner(fresh == 1 ? tr("Ce PDF contient une annotation créée dans un autre logiciel (surlignage, note…). "
                                           "L'importer dans StudyLab ?")
                                      : tr("Ce PDF contient %1 annotations créées dans un autre logiciel (surlignages, notes…). "
                                           "Les importer dans StudyLab ?")
                                            .arg(fresh),
                           QString(), true);
        emit fileAnnotationsScanned(fresh);
    });
    watcher->setFuture(QtConcurrent::run([path]() -> Scan {
        auto r = PdfInteropService::readFile(path);
        return r ? Scan(r.value()) : Scan();
    }));
}

Result<PdfImportReport> PdfReaderTab::importFileAnnotations()
{
    if (!m_loaded)
        return Error::make(Error::Code::InvalidArgument, tr("Le document n'est pas ouvert."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto res = interop().importAnnotations(m_documentId, pdfColorScheme());
    QApplication::restoreOverrideCursor();
    if (!res) {
        showFileBanner(res.error().message, QStringLiteral("warning"), false);
        return res;
    }
    const PdfImportReport& r = res.value();
    QStringList parts;
    if (r.imported == 0)
        parts << tr("Aucune nouvelle annotation à importer depuis le fichier.");
    else
        parts << (r.imported == 1 ? tr("1 annotation importée depuis le fichier.")
                                  : tr("%1 annotations importées depuis le fichier.").arg(r.imported));
    if (r.duplicates > 0)
        parts << (r.duplicates == 1 ? tr("1 était déjà dans StudyLab.") : tr("%1 étaient déjà dans StudyLab.").arg(r.duplicates));
    if (r.unsupported > 0)
        parts << (r.unsupported == 1 ? tr("1 annotation sans équivalent (pièce jointe, page tournée…) a été laissée de côté.")
                                     : tr("%1 annotations sans équivalent (pièce jointe, page tournée…) ont été laissées de côté.")
                                           .arg(r.unsupported));
    if (r.fileFullyImported && fileAnnotationsVisible()) {
        setFileAnnotationsVisible(false);
        parts << tr("Celles du fichier ne sont plus dessinées, pour ne pas les voir en double.");
    }
    showFileBanner(parts.join(QLatin1Char(' ')), r.imported > 0 ? QStringLiteral("done") : QString(), false);
    return res;
}

Result<PdfExportReport> PdfReaderTab::exportAnnotatedCopy(const QString& path)
{
    auto res = interop().exportAnnotatedCopy(m_documentId, path, pdfColorScheme());
    if (!res) {
        showFileBanner(res.error().message, QStringLiteral("warning"), false);
        return res;
    }
    const PdfExportReport& r = res.value();
    QString text = tr("Copie annotée enregistrée : %1").arg(QDir::toNativeSeparators(path));
    QStringList what;
    if (r.annotations > 0)
        what << (r.annotations == 1 ? tr("1 annotation") : tr("%1 annotations").arg(r.annotations));
    if (r.comments > 0)
        what << (r.comments == 1 ? tr("1 commentaire") : tr("%1 commentaires").arg(r.comments));
    if (!what.isEmpty())
        text += QStringLiteral(" (%1)").arg(what.join(tr(" et ")));
    text += QLatin1Char('.');
    if (r.alreadyPresent > 0)
        text += QLatin1Char(' ') + (r.alreadyPresent == 1 ? tr("1 annotation déjà présente dans le fichier n'a pas été dupliquée.")
                                                          : tr("%1 annotations déjà présentes dans le fichier n'ont pas été dupliquées.")
                                                                .arg(r.alreadyPresent));
    if (r.skipped > 0)
        text += QLatin1Char(' ') + (r.skipped == 1 ? tr("1 annotation sur une page tournée n'a pas pu être écrite.")
                                                   : tr("%1 annotations sur des pages tournées n'ont pas pu être écrites.").arg(r.skipped));
    showFileBanner(text, QStringLiteral("done"), false);
    return res;
}

void PdfReaderTab::exportAnnotatedCopyWithDialog()
{
    if (!m_loaded)
        return;
    bool any = false;
    for (const auto& r : m_records)
        any = any || r.type != AnnotationType::Bookmark;
    if (!any) {
        auto count = m_ctx.comments.count(m_documentId);
        any = count && count.value() > 0;
    }
    if (!any) {
        showFileBanner(tr("Ce document n'a encore ni annotation ni commentaire à écrire dans une copie."), QString(), false);
        return;
    }
    QString dir = m_settings.value(QStringLiteral("export/lastDirectory")).toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(window(), tr("Exporter une copie annotée"),
                                                      QDir(dir).filePath(PdfInteropService::suggestedFileName(m_record)),
                                                      tr("Document PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    m_settings.setValue(QStringLiteral("export/lastDirectory"), QFileInfo(path).absolutePath());
    (void)exportAnnotatedCopy(path);
}

void PdfReaderTab::setFileAnnotationsVisible(bool visible)
{
    {
        const QSignalBlocker block(m_fileAnnotationsAction);
        m_fileAnnotationsAction->setChecked(visible);
    }
    m_settings.setValue(QStringLiteral("pdf/hideFileAnnotations/%1").arg(m_documentId), !visible);
    if (!m_queue || m_queue->fileAnnotationsVisible() == visible)
        return;
    m_queue->setFileAnnotationsVisible(visible);
    m_view->reloadPages();
    if (m_thumbModel)
        m_thumbModel->reloadImages();
}

bool PdfReaderTab::fileAnnotationsVisible() const
{
    return m_queue ? m_queue->fileAnnotationsVisible() : m_fileAnnotationsAction->isChecked();
}

void PdfReaderTab::restoreState()
{
    const auto pos = m_library.readingPosition(m_documentId);
    if (!pos)
        return;
    const QJsonObject view = pos->view;
    const QString mode = view.value(QStringLiteral("zoomMode")).toString();
    m_view->setRotation(view.value(QStringLiteral("rotation")).toInt(0));
    {
        const QSignalBlocker block(m_double);
        m_double->setChecked(view.value(QStringLiteral("double")).toBool(false));
    }
    m_view->setDoublePage(m_double->isChecked());
    if (mode == QLatin1String("custom"))
        m_view->setZoom(view.value(QStringLiteral("zoom")).toDouble(1.0));
    else
        m_view->setZoomMode(mode == QLatin1String("page") ? PdfView::ZoomMode::FitPage : PdfView::ZoomMode::FitWidth);
    const QJsonObject loc = pos->locator;
    if (loc.value(QStringLiteral("type")).toString() == QLatin1String("pdf"))
        m_view->setPosition({loc.value(QStringLiteral("page")).toInt(), loc.value(QStringLiteral("offset")).toDouble()});
}

void PdfReaderTab::savePosition()
{
    if (!m_loaded || m_view->pageCount() == 0)
        return;
    m_saveTimer.stop();
    const PdfPosition p = m_view->position();
    db::ReadingPosition rp;
    rp.locator = {{QStringLiteral("type"), QStringLiteral("pdf")},
                  {QStringLiteral("page"), p.page},
                  {QStringLiteral("offset"), std::round(p.offset * 10) / 10}};
    rp.progress = double(m_view->currentPage() + 1) / m_view->pageCount();
    const auto mode = m_view->zoomMode();
    rp.view = {{QStringLiteral("zoomMode"), mode == PdfView::ZoomMode::FitWidth  ? QStringLiteral("width")
                                            : mode == PdfView::ZoomMode::FitPage ? QStringLiteral("page")
                                                                                 : QStringLiteral("custom")},
               {QStringLiteral("zoom"), m_view->zoom()},
               {QStringLiteral("rotation"), m_view->rotation()},
               {QStringLiteral("double"), m_view->doublePage()}};
    (void)m_library.saveReadingPosition(m_documentId, rp);
}

void PdfReaderTab::updatePageIndicator()
{
    if (!m_pageEdit->hasFocus())
        m_pageEdit->setText(QString::number(m_view->currentPage() + 1));
    m_ocrBadge->setVisible(m_view->hasOcrText(m_view->currentPage()));
    m_clock->notePage(m_view->currentPage());
}

void PdfReaderTab::reloadOcrText()
{
    if (m_loaded && !m_ocrReload.isActive())
        m_ocrReload.start();
}

void PdfReaderTab::loadOcrText()
{
    auto pages = m_ctx.search.ocrPages(m_documentId);
    if (!pages)
        return;
    m_view->setOcrPages(pages.value());
    if (!m_searchEdit->text().trimmed().isEmpty())
        m_view->setOcrSearchString(m_searchEdit->text().trimmed());
    updatePageIndicator();
    updateSearchCount();
}

void PdfReaderTab::updateZoomCombo()
{
    const QSignalBlocker block(m_zoomCombo);
    switch (m_view->zoomMode()) {
    case PdfView::ZoomMode::FitWidth:
        m_zoomCombo->setCurrentIndex(0);
        break;
    case PdfView::ZoomMode::FitPage:
        m_zoomCombo->setCurrentIndex(1);
        break;
    case PdfView::ZoomMode::Custom: {
        const int pct = qRound(m_view->zoom() * 100);
        int idx = m_zoomCombo->findData(pct);
        if (idx < 0) {
            // Valeur libre (Ctrl + molette) : entrée temporaire en fin de liste
            if (m_zoomCombo->itemData(m_zoomCombo->count() - 1).toInt() == -3)
                m_zoomCombo->removeItem(m_zoomCombo->count() - 1);
            m_zoomCombo->addItem(QStringLiteral("%1 %").arg(pct), -3);
            idx = m_zoomCombo->count() - 1;
        }
        m_zoomCombo->setCurrentIndex(idx);
        break;
    }
    }
}

void PdfReaderTab::updateHistoryButtons()
{
    m_back->setEnabled(m_view->canGoBack());
    m_forward->setEnabled(m_view->canGoForward());
}

void PdfReaderTab::showSearch()
{
    if (!m_loaded)
        return;
    m_searchBar->show();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void PdfReaderTab::updateSearchCount()
{
    const int n = m_view->searchResultCount();
    if (m_searchEdit->text().trimmed().isEmpty())
        m_searchCount->clear();
    else if (n == 0)
        m_searchCount->setText(tr("Aucun résultat"));
    else if (m_view->currentSearchResult() >= 0)
        m_searchCount->setText(tr("%1 sur %2").arg(m_view->currentSearchResult() + 1).arg(n));
    else
        m_searchCount->setText(countLabel(n, tr("résultat"), tr("résultats")));
}

void PdfReaderTab::findNext()
{
    const int n = m_view->searchResultCount();
    // La recherche est progressive : sans résultat encore, on avancera dès le premier trouvé.
    m_findPending = n == 0 && !m_searchEdit->text().trimmed().isEmpty();
    if (n == 0)
        return;
    int next = m_view->currentSearchResult() + 1;
    if (m_view->currentSearchResult() < 0) {
        // Premier résultat à partir de la page courante
        next = 0;
        for (int i = 0; i < n; ++i)
            if (m_view->searchResultPage(i) >= m_view->currentPage()) {
                next = i;
                break;
            }
    }
    m_view->setCurrentSearchResult(next % n);
    updateSearchCount();
}

void PdfReaderTab::findPrevious()
{
    const int n = m_view->searchResultCount();
    if (n == 0)
        return;
    const int cur = m_view->currentSearchResult();
    m_view->setCurrentSearchResult(cur <= 0 ? n - 1 : cur - 1);
    updateSearchCount();
}

void PdfReaderTab::setFocusMode(bool on)
{
    if (on == m_focusMode)
        return;
    m_focusMode = on;
    {
        const QSignalBlocker block(m_focusButton);
        m_focusButton->setChecked(on);
    }
    m_toolbar->setVisible(!on);
    m_side->setVisible(!on && m_sideToggle->isChecked());
    m_sidePanel->setVisible(!on && m_paneToggle->isChecked());
    m_popup->hide();
    if (on)
        m_searchBar->hide();
    emit focusModeToggled(on);
    m_view->setFocus();
}

// ───────────────────────── Annotations ─────────────────────────

QToolButton* PdfReaderTab::toolButtonFor(PdfView::Tool tool) const
{
    return m_toolButtons.value(int(tool));
}

void PdfReaderTab::buildChapters()
{
    m_chapters.clear();
    if (!m_bookmarks)
        return;
    // Entrées des deux premiers niveaux du sommaire : « Chapitre 7 · L'entropie ».
    std::function<void(const QModelIndex&, int)> walk = [&](const QModelIndex& parent, int depth) {
        for (int i = 0; i < m_bookmarks->rowCount(parent); ++i) {
            const QModelIndex idx = m_bookmarks->index(i, 0, parent);
            const QString title = idx.data(int(QPdfBookmarkModel::Role::Title)).toString().simplified();
            const int page = idx.data(int(QPdfBookmarkModel::Role::Page)).toInt();
            if (!title.isEmpty() && page >= 0)
                m_chapters.append({page, title});
            if (depth < 1)
                walk(idx, depth + 1);
        }
    };
    walk(QModelIndex(), 0);
    std::stable_sort(m_chapters.begin(), m_chapters.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
}

QString PdfReaderTab::chapterOf(int page) const
{
    QString title;
    for (const auto& [p, t] : m_chapters) {
        if (p > page)
            break;
        title = t;
    }
    return title;
}

void PdfReaderTab::pushMarks()
{
    const Palette& pal = m_theme.palette();
    QList<PdfAnnotationMark> marks;
    QHash<int, QColor> pages;
    for (const auto& r : m_records) {
        const auto loc = PdfLocator::fromJson(r.locator);
        if (!loc)
            continue;
        PdfAnnotationMark m;
        m.id = r.id;
        m.page = loc->page;
        m.type = r.type;
        m.rects = loc->rects;
        m.hasNote = !r.note.isEmpty();
        m.fill = annotationPageFill(r.color);
        m.stroke = annotationPageStroke(r.color);
        if (r.type == AnnotationType::Bookmark) {
            m.fill = annotationPageFill(AnnotationColor::Blue);
            m.stroke = annotationPageStroke(AnnotationColor::Blue);
        }
        marks << m;
        if (!pages.contains(m.page))
            pages.insert(m.page, annotationAccent(pal, r.type == AnnotationType::Bookmark ? AnnotationColor::Blue : r.color));
    }
    m_view->setAnnotations(marks);
    if (m_thumbModel)
        m_thumbModel->setMarks(pages);
    m_pane->setAnnotations(m_records);
}

void PdfReaderTab::updateBookmarkButton()
{
    if (!m_bookmarkButton)
        return;
    bool on = false;
    for (const auto& r : m_records)
        on = on || (r.type == AnnotationType::Bookmark && r.unitIndex == m_view->currentPage());
    const QSignalBlocker block(m_bookmarkButton);
    m_bookmarkButton->setChecked(on);
    m_bookmarkButton->setToolTip(on ? tr("Retirer le signet de cette page (Ctrl+D)") : tr("Signet sur cette page (Ctrl+D)"));
}

Result<QList<AnnotationRecord>> PdfReaderTab::annotateSelection(AnnotationType type, AnnotationColor color)
{
    const auto anchors = m_view->selectionAnchors();
    QList<AnnotationRecord> created;
    for (const PdfSelectionAnchor& a : anchors) {
        PdfLocator loc;
        loc.page = a.page;
        loc.rects = a.rects;
        loc.exact = normalizeQuote(a.text);
        loc.prefix = a.prefix;
        loc.suffix = a.suffix;
        loc.start = a.start;
        loc.end = a.end;
        AnnotationRecord r;
        r.documentId = m_documentId;
        r.type = type;
        r.color = color;
        r.pageLabel = m_queue ? m_queue->pageLabel(a.page) : QString();
        if (r.pageLabel == QString::number(a.page + 1))
            r.pageLabel.clear();  // numéro physique : inutile de le stocker
        r.locator = loc.toJson();
        r.selectedText = a.text;
        auto res = m_annotations.create(r);
        if (!res)
            return res.error();
        created << res.value();
    }
    m_view->clearSelection();
    m_popup->hide();
    return created;
}

Result<AnnotationRecord> PdfReaderTab::addArea(int page, const QRectF& rect)
{
    PdfLocator loc;
    loc.page = page;
    loc.rects = {rect};
    AnnotationRecord r;
    r.documentId = m_documentId;
    r.type = AnnotationType::Marker;
    r.color = m_color;
    if (m_doc) {
        // Texte contenu dans la zone, s'il y en a (formule, légende) : utile à la recherche et à l'export.
        const QSizeF size = m_queue->pagePointSize(page);
        const QPdfSelection sel = m_doc->getSelection(
            page, QPointF(rect.left() * size.width(), rect.top() * size.height()),
            QPointF(rect.right() * size.width(), rect.bottom() * size.height()));
        if (sel.isValid())
            r.selectedText = sel.text();
        r.pageLabel = m_queue ? m_queue->pageLabel(page) : QString();
        if (r.pageLabel == QString::number(page + 1))
            r.pageLabel.clear();
    }
    loc.exact = normalizeQuote(r.selectedText);
    r.locator = loc.toJson();
    return m_annotations.create(r);
}

Result<AnnotationRecord> PdfReaderTab::addNote(int page, const QPointF& point, const QString& text)
{
    PdfLocator loc;
    loc.page = page;
    loc.rects = {QRectF(point.x() - 0.005, point.y() - 0.005, 0.01, 0.01)};
    AnnotationRecord r;
    r.documentId = m_documentId;
    r.type = AnnotationType::Note;
    r.color = m_color;
    r.locator = loc.toJson();
    r.note = text;
    if (m_doc) {
        r.pageLabel = m_queue ? m_queue->pageLabel(page) : QString();
        if (r.pageLabel == QString::number(page + 1))
            r.pageLabel.clear();
    }
    return m_annotations.create(r);
}

void PdfReaderTab::toggleBookmark()
{
    if (!m_loaded)
        return;
    const int page = m_view->currentPage();
    QString label = m_queue ? m_queue->pageLabel(page) : QString();
    if (label == QString::number(page + 1))
        label.clear();
    if (auto r = m_annotations.toggleBookmark(m_documentId, page, label); !r)
        reportError(r.error());
    updateBookmarkButton();
}

void PdfReaderTab::revealAnnotation(qint64 id)
{
    for (const auto& r : m_records) {
        if (r.id != id)
            continue;
        const auto loc = PdfLocator::fromJson(r.locator);
        if (!loc)
            return;
        m_view->revealNormalizedRect(loc->page, loc->bounds(), true);
        m_view->flashAnnotation(id);
        m_pane->setActive(id);
        return;
    }
}

std::optional<CommentTarget> PdfReaderTab::selectionTarget() const
{
    const auto anchors = m_view->selectionAnchors();
    if (anchors.isEmpty())
        return std::nullopt;
    // Passage sur plusieurs pages : ancré à sa première page, citation complète.
    const PdfSelectionAnchor& a = anchors.first();
    PdfLocator loc;
    loc.page = a.page;
    loc.rects = a.rects;
    loc.exact = normalizeQuote(a.text);
    loc.prefix = a.prefix;
    loc.suffix = a.suffix;
    loc.start = a.start;
    loc.end = a.end;
    QStringList texts;
    for (const auto& part : anchors)
        texts << part.text;
    CommentTarget t;
    t.anchor = CommentAnchor::Passage;
    t.unitIndex = a.page;
    t.locator = loc.toJson();
    const QString label = m_queue ? m_queue->pageLabel(a.page) : QString();
    if (!label.isEmpty() && label != QString::number(a.page + 1))
        t.locator.insert(QStringLiteral("pageLabel"), label);
    t.quote = normalizeQuote(texts.join(QLatin1Char(' ')));
    t.locator.insert(QStringLiteral("textQuote"),
                     QJsonObject{{QStringLiteral("exact"), t.quote}, {QStringLiteral("prefix"), a.prefix},
                                 {QStringLiteral("suffix"), anchors.last().suffix}});
    t.where = QStringLiteral("p. %1").arg(label.isEmpty() ? QString::number(a.page + 1) : label);
    return t;
}

CommentTarget PdfReaderTab::positionTarget() const
{
    const int page = m_view->currentPage();
    PdfLocator loc;
    loc.page = page;
    CommentTarget t;
    t.anchor = CommentAnchor::Page;
    t.unitIndex = page;
    t.locator = loc.toJson();
    const QString label = m_queue ? m_queue->pageLabel(page) : QString();
    if (!label.isEmpty() && label != QString::number(page + 1))
        t.locator.insert(QStringLiteral("pageLabel"), label);
    t.where = QStringLiteral("p. %1").arg(label.isEmpty() ? QString::number(page + 1) : label);
    return t;
}

void PdfReaderTab::showPassage(int unit, const QString& query)
{
    m_view->goToPage(unit);
    m_searchBar->show();
    m_searchEdit->setText(query);
    findNext();  // premier résultat à partir de cette page, dès que la recherche en trouve un
}

void PdfReaderTab::revealLocator(const QJsonObject& locator)
{
    const auto loc = PdfLocator::fromJson(locator);
    if (!loc)
        return;
    if (loc->rects.isEmpty())
        m_view->goToPage(loc->page);
    else
        m_view->revealNormalizedRect(loc->page, loc->bounds(), true);
}

void PdfReaderTab::copySelection()
{
    m_view->copySelection();
}

void PdfReaderTab::onSelectionFinished()
{
    const PdfView::Tool tool = m_view->tool();
    const AnnotationType type = tool == PdfView::Tool::Underline   ? AnnotationType::Underline
                                : tool == PdfView::Tool::Strikeout ? AnnotationType::Strikeout
                                                                   : AnnotationType::Highlight;
    if (tool == PdfView::Tool::Highlight || tool == PdfView::Tool::Underline || tool == PdfView::Tool::Strikeout) {
        if (auto r = annotateSelection(type, m_color); !r)
            reportError(r.error());
        return;
    }
    const QRect rect = m_view->selectionViewportRect();
    if (rect.isEmpty())
        return;
    m_popupAnnotation = 0;
    m_popup->showFor(AnnotationPopup::Mode::Create, rect, m_color);
}

void PdfReaderTab::showEditPopup(qint64 id)
{
    auto r = m_annotations.find(id);
    if (!r)
        return;
    m_popupAnnotation = id;
    QRect rect = m_view->annotationViewportRect(id);
    if (rect.isEmpty())
        rect = QRect(m_view->viewport()->rect().center(), QSize(1, 1));
    m_popup->showFor(AnnotationPopup::Mode::Edit, rect, r.value().color, r.value().type, !r.value().note.isEmpty());
}

}  // namespace studylab::ui
