#include "ui/reader/PdfReaderTab.h"

#include "engines/pdf/PdfRenderQueue.h"
#include "services/library/LibraryService.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/Widgets.h"
#include "ui/reader/ThumbnailModel.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
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
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {

/// Miniature : image de la page, cadre de la page courante, numéro.
class ThumbDelegate : public QStyledItemDelegate {
public:
    ThumbDelegate(ThemeManager& theme, PdfView* view, QObject* parent)
        : QStyledItemDelegate(parent), m_theme(theme), m_view(view)
    {
    }
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        const QSizeF pt = m_view->document() ? m_view->document()->pagePointSize(index.row()) : QSizeF(1, 1.4);
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
};

QToolButton* toolButton(const QString& tooltip, QWidget* parent, bool checkable = false)
{
    auto* b = new QToolButton(parent);
    b->setToolTip(tooltip);
    b->setAccessibleName(tooltip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setProperty("readerTool", true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

}  // namespace

PdfReaderTab::PdfReaderTab(LibraryService& library, ThemeManager& theme, qint64 documentId, QWidget* parent)
    : QWidget(parent), m_library(library), m_theme(theme), m_documentId(documentId)
{
    if (auto r = library.document(documentId))
        m_record = r.value();

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_toolbar = buildToolbar();
    v->addWidget(m_toolbar);
    m_searchBar = buildSearchBar();
    v->addWidget(m_searchBar);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    m_view = new PdfView(theme, this);
    body->addWidget(buildSidePanel());
    body->addWidget(m_view, 1);
    v->addLayout(body, 1);

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
    m_searchDebounce.setSingleShot(true);
    m_searchDebounce.setInterval(250);
    connect(&m_searchDebounce, &QTimer::timeout, this, [this] {
        if (m_searchModel)
            m_searchModel->setSearchString(m_searchEdit->text().trimmed());
        m_view->setCurrentSearchResult(-1);
        m_findPending = false;
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
    connect(&theme, &ThemeManager::themeChanged, this, &PdfReaderTab::refreshIcons);

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
    h->addWidget(m_pageEdit);
    h->addWidget(m_pageTotal);
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

    m_searchButton = toolButton(tr("Rechercher dans le document (Ctrl+F)"), bar);
    connect(m_searchButton, &QToolButton::clicked, this, &PdfReaderTab::showSearch);
    m_focusButton = toolButton(tr("Lecture sans distraction (Échap pour quitter)"), bar, true);
    connect(m_focusButton, &QToolButton::toggled, this, &PdfReaderTab::setFocusMode);
    m_fullButton = toolButton(tr("Plein écran (F11)"), bar);
    connect(m_fullButton, &QToolButton::clicked, this, &PdfReaderTab::fullScreenToggled);
    h->addWidget(m_searchButton);
    h->addWidget(m_focusButton);
    h->addWidget(m_fullButton);
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
    const QString file = m_library.documentFile(m_record);
    if (!QFileInfo::exists(file))
        return fail(tr("Le fichier est introuvable :\n%1").arg(QDir::toNativeSeparators(file)));

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
    m_view->setDocument(m_doc.get(), m_queue.get());

    m_searchModel = new QPdfSearchModel;
    m_searchModel->setDocument(m_doc.get());
    m_view->setSearchModel(m_searchModel);
    const auto onResults = [this] {
        if (m_findPending && m_searchModel->rowCount({}) > 0)
            findNext();
        else
            updateSearchCount();
    };
    connect(m_searchModel, &QAbstractItemModel::rowsInserted, this, onResults);
    connect(m_searchModel, &QAbstractItemModel::modelReset, this, onResults);

    m_thumbModel = new ThumbnailModel(m_doc.get(), m_queue.get());
    m_thumbs->setModel(m_thumbModel);
    m_thumbs->setItemDelegate(new ThumbDelegate(m_theme, m_view, m_thumbs));
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
    restoreState();
    updatePageIndicator();
    updateZoomCombo();
    (void)m_library.markOpened(m_documentId);
    return {};
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
    const int n = m_searchModel ? m_searchModel->rowCount({}) : 0;
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
    const int n = m_searchModel ? m_searchModel->rowCount({}) : 0;
    // La recherche est progressive : sans résultat encore, on avancera dès le premier trouvé.
    m_findPending = n == 0 && !m_searchEdit->text().trimmed().isEmpty();
    if (n == 0)
        return;
    int next = m_view->currentSearchResult() + 1;
    if (m_view->currentSearchResult() < 0) {
        // Premier résultat à partir de la page courante
        next = 0;
        for (int i = 0; i < n; ++i)
            if (m_searchModel->resultAtIndex(i).page() >= m_view->currentPage()) {
                next = i;
                break;
            }
    }
    m_view->setCurrentSearchResult(next % n);
    updateSearchCount();
}

void PdfReaderTab::findPrevious()
{
    const int n = m_searchModel ? m_searchModel->rowCount({}) : 0;
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
    if (on)
        m_searchBar->hide();
    emit focusModeToggled(on);
    m_view->setFocus();
}

}  // namespace studylab::ui
