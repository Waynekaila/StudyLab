#include "ui/reader/EpubReaderTab.h"

#include "engines/epub/EpubPackage.h"
#include "services/annotations/AnnotationService.h"
#include "services/library/LibraryService.h"
#include "services/settings/SettingsService.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/Widgets.h"
#include "ui/reader/AnnotationPopup.h"
#include "ui/reader/AnnotationStyle.h"
#include "ui/reader/AnnotationsPane.h"
#include "ui/reader/EpubWeb.h"
#include "ui/shell/AppContext.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QSlider>
#include <QStandardItemModel>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {

const QString kDisplayKey = QStringLiteral("epub/display");
constexpr int kTocSectionRole = Qt::UserRole + 1;
constexpr int kTocHrefRole = Qt::UserRole + 2;

const double kLineHeights[] = {1.4, 1.6, 1.75, 2.0};

QString frenchNumber(double v)
{
    return QString::number(v, 'g', 3).replace(QLatin1Char('.'), QLatin1Char(','));
}

}  // namespace

// ───────────────────────── Affichage du texte ─────────────────────────

TypographyPanel::TypographyPanel(ThemeManager& theme, QWidget* parent) : QFrame(parent, Qt::Popup), m_theme(theme)
{
    setObjectName(QStringLiteral("TypographyPanel"));
    setAttribute(Qt::WA_StyledBackground);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(20, 16, 20, 18);
    v->setSpacing(12);
    v->addWidget(makeLabel(tr("Affichage du texte"), "h3"));
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(12);
    grid->setColumnMinimumWidth(1, theme.px(270));

    m_font = new QComboBox(this);
    m_font->addItem(tr("Police du livre"));
    m_font->addItem(tr("Avec empattements (Georgia)"));
    m_font->addItem(tr("Sans empattements (Inter)"));
    m_font->setAccessibleName(tr("Police"));
    connect(m_font, &QComboBox::activated, this, [this](int i) {
        m_settings.font = EpubSettings::Font(i);
        emitChange();
    });

    auto* sizeRow = new QHBoxLayout;
    auto* small = makeLabel(QStringLiteral("A"), "muted");
    auto* big = makeLabel(QStringLiteral("A"), "h2");
    m_size = new QSlider(Qt::Horizontal, this);
    m_size->setRange(7, 20);  // × 10 %
    m_size->setPageStep(1);
    m_size->setAccessibleName(tr("Taille du texte"));
    connect(m_size, &QSlider::valueChanged, this, [this](int step) {
        m_size->setToolTip(QStringLiteral("%1 %").arg(step * 10));
        if (m_settings.fontSize != step * 10) {
            m_settings.fontSize = step * 10;
            emitChange();
        }
    });
    sizeRow->addWidget(small);
    sizeRow->addWidget(m_size, 1);
    sizeRow->addWidget(big);

    QStringList heights;
    for (double h : kLineHeights)
        heights << frenchNumber(h);
    m_lineHeight = new SegmentedControl(heights, this);
    connect(m_lineHeight, &SegmentedControl::currentIndexChanged, this, [this](int i) {
        m_settings.lineHeight = kLineHeights[std::clamp(i, 0, 3)];
        emitChange();
    });
    m_margins = new SegmentedControl({tr("Étroites"), tr("Normales"), tr("Larges")}, this);
    connect(m_margins, &SegmentedControl::currentIndexChanged, this, [this](int i) {
        m_settings.margins = EpubSettings::Margins(i);
        emitChange();
    });
    m_layout = new SegmentedControl({tr("Paginé"), tr("Défilement")}, this);
    connect(m_layout, &SegmentedControl::currentIndexChanged, this, [this](int i) {
        m_settings.scrolled = i == 1;
        emitChange();
    });

    auto* themes = new QHBoxLayout;
    themes->setSpacing(10);
    auto* group = new QButtonGroup(this);
    const struct {
        ReadingTheme theme;
        const char* label;
    } entries[] = {{ReadingTheme::White, QT_TR_NOOP("Blanc")},
                   {ReadingTheme::Sepia, QT_TR_NOOP("Sépia")},
                   {ReadingTheme::Green, QT_TR_NOOP("Vert d'eau")},
                   {ReadingTheme::Dark, QT_TR_NOOP("Sombre")}};
    for (const auto& e : entries) {
        auto* b = new QToolButton(this);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolTip(tr(e.label));
        b->setAccessibleName(tr("Thème : %1").arg(tr(e.label).toLower()));
        b->setCursor(Qt::PointingHandCursor);
        b->setProperty("readingTheme", true);
        b->setIconSize(QSize(theme.px(36), theme.px(30)));
        const ReadingColors c = readingColors(e.theme);
        QPixmap pm(theme.px(36) * 2, theme.px(30) * 2);
        pm.setDevicePixelRatio(2);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(theme.palette().border, 1));
            p.setBrush(c.bg);
            p.drawRoundedRect(QRectF(1, 1, theme.px(36) - 2, theme.px(30) - 2), 6, 6);
            p.setPen(c.text);
            QFont f = font();
            f.setPixelSize(theme.px(13));
            p.setFont(f);
            p.drawText(QRectF(0, 0, theme.px(36), theme.px(30)), Qt::AlignCenter, QStringLiteral("Aa"));
        }
        b->setIcon(QIcon(pm));
        const ReadingTheme t = e.theme;
        connect(b, &QToolButton::clicked, this, [this, t] {
            m_settings.theme = t;
            emitChange();
        });
        group->addButton(b);
        themes->addWidget(b);
        m_themes << b;
    }
    themes->addStretch(1);

    int row = 0;
    auto addRow = [&](const QString& label, QWidget* w, QLayout* l = nullptr) {
        grid->addWidget(makeLabel(label, "muted"), row, 0, Qt::AlignVCenter);
        if (w)
            grid->addWidget(w, row, 1);
        else
            grid->addLayout(l, row, 1);
        ++row;
    };
    addRow(tr("Police"), m_font);
    addRow(tr("Taille"), nullptr, sizeRow);
    addRow(tr("Interligne"), m_lineHeight);
    addRow(tr("Marges"), m_margins);
    addRow(tr("Mise en page"), m_layout);
    addRow(tr("Thème"), nullptr, themes);
    v->addLayout(grid);
}

QToolButton* TypographyPanel::themeButton(ReadingTheme theme) const
{
    return m_themes.value(int(theme));
}

void TypographyPanel::setSettings(const EpubSettings& s)
{
    m_settings = s;
    const QSignalBlocker b1(m_font);
    const QSignalBlocker b2(m_size);
    m_font->setCurrentIndex(int(s.font));
    m_size->setValue(s.fontSize / 10);
    int lh = 1;
    for (int i = 0; i < 4; ++i)
        if (qAbs(kLineHeights[i] - s.lineHeight) < 0.01)
            lh = i;
    m_lineHeight->setCurrentIndex(lh);
    m_margins->setCurrentIndex(int(s.margins));
    m_layout->setCurrentIndex(s.scrolled ? 1 : 0);
    for (int i = 0; i < m_themes.size(); ++i)
        m_themes[i]->setChecked(i == int(s.theme));
}

void TypographyPanel::emitChange()
{
    for (int i = 0; i < m_themes.size(); ++i)
        m_themes[i]->setChecked(i == int(m_settings.theme));
    emit settingsChanged(m_settings);
}

// ───────────────────────── Onglet ─────────────────────────

EpubReaderTab::EpubReaderTab(AppContext& ctx, qint64 documentId, QWidget* parent)
    : ReaderTab(ctx, documentId, parent)
{
    ThemeManager& theme = m_theme;
    m_display = EpubSettings::fromStored(m_settings.value(kDisplayKey).toObject());

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
    body->addWidget(buildSidePanel());

    m_viewHost = new QWidget(this);
    m_viewHost->setObjectName(QStringLiteral("EpubHost"));
    m_viewHost->setAttribute(Qt::WA_StyledBackground);
    auto* hv = new QVBoxLayout(m_viewHost);
    hv->setContentsMargins(0, 0, 0, 0);
    hv->setSpacing(0);
    m_footer = new QWidget(m_viewHost);
    m_footer->setObjectName(QStringLiteral("EpubFooter"));
    auto* fh = new QHBoxLayout(m_footer);
    fh->setContentsMargins(12, 4, 12, 8);
    m_pageLabel = new QLabel(m_footer);
    m_pageLabel->setObjectName(QStringLiteral("EpubPageLabel"));
    m_pageLabel->setAlignment(Qt::AlignCenter);
    fh->addWidget(m_pageLabel, 1);
    applyFooterColors();
    hv->addWidget(m_footer);
    body->addWidget(m_viewHost, 1);

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

    body->addWidget(createSidePanes(true));
    v->addLayout(body, 1);
    m_popup = new AnnotationPopup(theme, m_viewHost);

    m_typo = new TypographyPanel(theme, this);
    m_typo->setSettings(m_display);
    connect(m_typo, &TypographyPanel::settingsChanged, this, &EpubReaderTab::setDisplaySettings);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);
    connect(&m_saveTimer, &QTimer::timeout, this, &EpubReaderTab::savePosition);
    m_searchDebounce.setSingleShot(true);
    m_searchDebounce.setInterval(300);
    connect(&m_searchDebounce, &QTimer::timeout, this, [this] {
        m_searchIndex = -1;
        if (m_view)
            m_view->search(m_searchEdit->text().trimmed());
        updateSearchCount();
    });

    initAnnotations();
    connect(&theme, &ThemeManager::themeChanged, this, &EpubReaderTab::refreshIcons);

    auto* find = new QShortcut(QKeySequence::Find, this, this, &EpubReaderTab::showSearch);
    find->setContext(Qt::WidgetWithChildrenShortcut);
    auto* back = new QShortcut(QKeySequence::Back, this, this, [this] {
        if (m_view)
            m_view->back();
    });
    back->setContext(Qt::WidgetWithChildrenShortcut);
    auto* fwd = new QShortcut(QKeySequence::Forward, this, this, [this] {
        if (m_view)
            m_view->forward();
    });
    fwd->setContext(Qt::WidgetWithChildrenShortcut);
    auto* bigger = new QShortcut(QKeySequence::ZoomIn, this, this, [this] {
        EpubSettings s = m_display;
        s.fontSize = std::min(200, s.fontSize + 10);
        setDisplaySettings(s);
    });
    bigger->setContext(Qt::WidgetWithChildrenShortcut);
    auto* smaller = new QShortcut(QKeySequence::ZoomOut, this, this, [this] {
        EpubSettings s = m_display;
        s.fontSize = std::max(70, s.fontSize - 10);
        setDisplaySettings(s);
    });
    smaller->setContext(Qt::WidgetWithChildrenShortcut);

    refreshIcons();
    m_back->setEnabled(false);
    m_forward->setEnabled(false);
}

EpubReaderTab::~EpubReaderTab()
{
    if (m_loaded)
        savePosition();
    delete m_view;  // la page web avant le désenregistrement du livre
    m_view = nullptr;
    if (!m_token.isEmpty())
        readerSchemeHandler()->unregisterBook(m_token);
}

QWidget* EpubReaderTab::buildToolbar()
{
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("ReaderToolbar"));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(10, 6, 10, 6);
    h->setSpacing(4);

    m_tocToggle = makeToolButton(tr("Table des matières"), bar, true);
    m_tocToggle->setChecked(true);
    connect(m_tocToggle, &QToolButton::toggled, this, [this](bool on) { m_side->setVisible(on); });
    h->addWidget(m_tocToggle);
    h->addSpacing(6);
    m_back = makeToolButton(tr("Précédent (Alt+←)"), bar);
    m_forward = makeToolButton(tr("Suivant (Alt+→)"), bar);
    connect(m_back, &QToolButton::clicked, this, [this] {
        if (m_view)
            m_view->back();
    });
    connect(m_forward, &QToolButton::clicked, this, [this] {
        if (m_view)
            m_view->forward();
    });
    h->addWidget(m_back);
    h->addWidget(m_forward);
    h->addSpacing(10);

    m_prevChapter = makeToolButton(tr("Chapitre précédent"), bar);
    m_nextChapter = makeToolButton(tr("Chapitre suivant"), bar);
    connect(m_prevChapter, &QToolButton::clicked, this, [this] { goToChapter(-1); });
    connect(m_nextChapter, &QToolButton::clicked, this, [this] { goToChapter(1); });
    m_chapterLabel = makeLabel(QString(), nullptr, bar);
    m_chapterLabel->setObjectName(QStringLiteral("EpubChapter"));
    m_chapterLabel->setMinimumWidth(m_theme.px(120));
    m_chapterLabel->setMaximumWidth(m_theme.px(340));
    h->addWidget(m_prevChapter);
    h->addWidget(m_chapterLabel);
    h->addWidget(m_nextChapter);
    h->addSpacing(12);
    m_progressLabel = makeLabel(QString(), "muted", bar);
    h->addWidget(m_progressLabel);
    h->addStretch(1);

    const struct {
        Tool tool;
        const char* tip;
    } tools[] = {
        {Tool::Highlight, QT_TR_NOOP("Surligner : sélectionnez un passage")},
        {Tool::Underline, QT_TR_NOOP("Souligner : sélectionnez un passage")},
        {Tool::Strikeout, QT_TR_NOOP("Barrer : sélectionnez un passage")},
    };
    auto* toolGroup = new QFrame(bar);
    toolGroup->setObjectName(QStringLiteral("ToolGroup"));
    auto* th = new QHBoxLayout(toolGroup);
    th->setContentsMargins(3, 2, 3, 2);
    th->setSpacing(1);
    for (const auto& t : tools) {
        auto* b = makeToolButton(tr(t.tip), toolGroup, true);
        const Tool tool = t.tool;
        connect(b, &QToolButton::clicked, this, [this, tool](bool on) { setTool(on ? tool : Tool::Select); });
        th->addWidget(b);
        m_toolButtons.insert(int(tool), b);
    }
    m_bookmarkButton = makeToolButton(tr("Signet ici (Ctrl+D)"), toolGroup, true);
    connect(m_bookmarkButton, &QToolButton::clicked, this, [this] { toggleBookmark(); });
    th->addWidget(m_bookmarkButton);
    h->addWidget(toolGroup);
    h->addSpacing(6);
    addColorButtons(h, bar);
    h->addSpacing(10);

    m_typoButton = makeToolButton(tr("Affichage du texte"), bar, true);
    connect(m_typoButton, &QToolButton::clicked, this, [this] {
        m_typo->setSettings(m_display);
        m_typo->adjustSize();
        const QPoint below = m_typoButton->mapToGlobal(QPoint(m_typoButton->width(), m_typoButton->height() + 6));
        m_typo->move(below.x() - m_typo->width(), below.y());
        m_typo->show();
        m_typoButton->setChecked(false);
    });
    m_searchButton = makeToolButton(tr("Rechercher dans le livre (Ctrl+F)"), bar);
    connect(m_searchButton, &QToolButton::clicked, this, &EpubReaderTab::showSearch);
    m_focusButton = makeToolButton(tr("Lecture sans distraction (Échap pour quitter)"), bar, true);
    connect(m_focusButton, &QToolButton::toggled, this, &EpubReaderTab::setFocusMode);
    m_fullButton = makeToolButton(tr("Plein écran (F11)"), bar);
    connect(m_fullButton, &QToolButton::clicked, this, &EpubReaderTab::fullScreenToggled);
    h->addWidget(m_typoButton);
    h->addWidget(m_searchButton);
    h->addWidget(m_focusButton);
    h->addWidget(m_fullButton);
    h->addSpacing(6);
    addPaneToggle(h, bar);
    return bar;
}

QWidget* EpubReaderTab::buildSidePanel()
{
    m_side = new QFrame(this);
    m_side->setObjectName(QStringLiteral("ReaderSide"));
    m_side->setFixedWidth(m_theme.px(250));
    auto* v = new QVBoxLayout(m_side);
    v->setContentsMargins(10, 14, 10, 0);
    v->setSpacing(8);
    auto* title = makeLabel(tr("TABLE DES MATIÈRES"), "overline", m_side);
    title->setContentsMargins(8, 0, 0, 0);
    v->addWidget(title);
    m_tocModel = new QStandardItemModel(this);
    m_tocView = new QTreeView(m_side);
    m_tocView->setObjectName(QStringLiteral("Outline"));
    m_tocView->setModel(m_tocModel);
    m_tocView->setHeaderHidden(true);
    m_tocView->setFrameShape(QFrame::NoFrame);
    m_tocView->setUniformRowHeights(true);
    m_tocView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tocView->setAccessibleName(tr("Table des matières"));
    connect(m_tocView, &QTreeView::activated, this, [this](const QModelIndex& idx) {
        if (m_view)
            m_view->goTo(idx.data(kTocHrefRole).toString());
    });
    connect(m_tocView, &QTreeView::clicked, m_tocView, &QTreeView::activated);
    m_noToc = makeLabel(tr("Ce livre n'a pas de table des matières."), "faint");
    m_noToc->setWordWrap(true);
    m_noToc->setAlignment(Qt::AlignCenter);
    m_noToc->hide();
    v->addWidget(m_tocView, 1);
    v->addWidget(m_noToc);
    return m_side;
}

QWidget* EpubReaderTab::buildSearchBar()
{
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("ReaderSearchBar"));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 6, 12, 6);
    h->setSpacing(6);
    m_searchEdit = new QLineEdit(bar);
    m_searchEdit->setPlaceholderText(tr("Rechercher dans le livre"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setAccessibleName(tr("Rechercher dans le livre"));
    m_searchEdit->setMaximumWidth(m_theme.px(360));
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { m_searchDebounce.start(); });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] {
        if (m_searchDebounce.isActive()) {
            m_searchDebounce.stop();
            m_searchIndex = -1;
            if (m_view)
                m_view->search(m_searchEdit->text().trimmed());
        }
        findNext();
    });
    m_searchCount = makeLabel(QString(), "faint");
    auto* prev = new QPushButton(tr("Précédent"), bar);
    auto* next = new QPushButton(tr("Suivant"), bar);
    prev->setProperty("variant", QStringLiteral("ghost"));
    next->setProperty("variant", QStringLiteral("ghost"));
    connect(prev, &QPushButton::clicked, this, &EpubReaderTab::findPrevious);
    connect(next, &QPushButton::clicked, this, &EpubReaderTab::findNext);
    auto* close = new QPushButton(tr("Fermer"), bar);
    close->setProperty("variant", QStringLiteral("ghost"));
    connect(close, &QPushButton::clicked, this, [this] {
        m_searchEdit->clear();
        m_searchBar->hide();
        if (m_view) {
            m_view->clearSearch();
            m_view->setFocus();
        }
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

void EpubReaderTab::refreshIcons()
{
    const Palette& pal = m_theme.palette();
    auto set = [&](QToolButton* b, const char* icon) {
        if (b)
            b->setIcon(Icons::stateful(QString::fromLatin1(icon), pal.text2, pal.primaryStrong, 18));
    };
    set(m_tocToggle, "list");
    set(m_back, "arrow-left");
    set(m_forward, "arrow-right");
    set(m_prevChapter, "chevron-left");
    set(m_nextChapter, "chevron-right");
    set(m_typoButton, "type");
    set(m_searchButton, "search");
    set(m_focusButton, "eye");
    set(m_fullButton, "maximize");
    set(m_bookmarkButton, "bookmark");
    const char* toolIcons[] = {"", "highlighter", "underline", "strikethrough"};
    for (auto it = m_toolButtons.cbegin(); it != m_toolButtons.cend(); ++it)
        set(it.value(), toolIcons[it.key()]);
    refreshColorIcons();
}

void EpubReaderTab::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_loaded && !m_loadFailed)
        QTimer::singleShot(0, this, [this] { (void)ensureLoaded(); });
}

void EpubReaderTab::fail(const QString& message)
{
    m_loadFailed = true;
    m_viewHost->hide();
    m_side->hide();
    m_toolbar->setEnabled(false);
    m_errorBox->findChild<QLabel*>(QStringLiteral("ReaderError"))->setText(message);
    m_errorBox->show();
    emit loadFailed(message);
}

Result<void> EpubReaderTab::ensureLoaded()
{
    if (m_loaded)
        return {};
    if (m_record.id == 0) {
        fail(tr("Ce document n'existe plus dans la bibliothèque."));
        return Error::make(Error::Code::NotFound, tr("Document introuvable."));
    }
    const QString file = m_library.documentFile(m_record);
    if (!QFileInfo::exists(file)) {
        const QString msg = tr("Le fichier est introuvable :\n%1").arg(QDir::toNativeSeparators(file));
        fail(msg);
        return Error::make(Error::Code::Io, msg);
    }
    auto pkg = EpubPackage::open(file);
    if (!pkg) {
        const QString msg = tr("Cet EPUB ne peut pas être ouvert : %1").arg(pkg.error().message);
        fail(msg);
        return Error::make(Error::Code::Io, msg);
    }
    m_package = std::shared_ptr<EpubPackage>(std::move(pkg.value()));
    m_token = QStringLiteral("%1-%2").arg(m_documentId).arg(m_record.uuid.left(8));
    readerSchemeHandler()->registerBook(m_token, m_package);

    m_view = new EpubView(m_viewHost);
    static_cast<QVBoxLayout*>(m_viewHost->layout())->insertWidget(0, m_view, 1);
    m_popup->raise();
    connect(m_view, &EpubView::opened, this, &EpubReaderTab::onOpened);
    connect(m_view, &EpubView::loadFailed, this, [this](const QString& message) {
        fail(tr("Cet EPUB ne peut pas être affiché : %1").arg(message));
    });
    connect(m_view, &EpubView::relocated, this, &EpubReaderTab::onRelocated);
    connect(m_view, &EpubView::selectionMade, this, &EpubReaderTab::onSelection);
    connect(m_view, &EpubView::selectionCleared, this, [this] {
        m_selection.reset();
        if (m_popup->mode() == AnnotationPopup::Mode::Create)
            m_popup->hide();
    });
    connect(m_view, &EpubView::annotationClicked, this, [this](qint64 id, const QRect& rect) {
        m_lastAnnotationRect = rect;
        m_pane->setActive(id);
        showEditPopup(id);
    });
    connect(m_view, &EpubView::contextMenuRequested, this, [this](qint64 id, const QPoint& pos) {
        if (id)
            showAnnotationMenu(id, pos);
    });
    connect(m_view, &EpubView::searchProgress, this, [this] { updateSearchCount(); });
    connect(m_view, &EpubView::bookmarkStateChanged, this, [this](qint64 id) {
        m_visibleBookmark = id;
        updateBookmarkButton();
    });
    connect(m_view, &EpubView::escapePressed, this, [this] {
        if (m_popup->isVisible())
            m_popup->hide();
        else if (m_tool != Tool::Select)
            setTool(Tool::Select);
        else if (m_focusMode)
            setFocusMode(false);
    });
    connect(m_view, &EpubView::externalLinkRequested, this, [this](const QString& href) {
        const QUrl url(href);
        if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")
            && url.scheme() != QLatin1String("mailto"))
            return;
        if (QMessageBox::question(window(), tr("Lien externe"),
                                  tr("Ouvrir ce lien dans le navigateur ?\n%1").arg(href))
            == QMessageBox::Yes)
            QDesktopServices::openUrl(url);
    });

    QString lastCfi;
    if (const auto pos = m_library.readingPosition(m_documentId)) {
        if (pos->locator.value(QStringLiteral("type")).toString() == QLatin1String("epub"))
            lastCfi = pos->locator.value(QStringLiteral("cfi")).toString();
    }
    m_view->openBook(m_token, m_package->fileSizes(), lastCfi, m_display);
    m_loaded = true;
    reloadAnnotations();
    (void)m_library.markOpened(m_documentId);
    return {};
}

void EpubReaderTab::onOpened()
{
    m_tocModel->clear();
    QList<QStandardItem*> parents;
    for (const EpubTocEntry& e : m_view->toc()) {
        auto* item = new QStandardItem(e.label);
        item->setData(e.section, kTocSectionRole);
        item->setData(e.href, kTocHrefRole);
        item->setToolTip(e.label);
        while (parents.size() > e.depth)
            parents.removeLast();
        if (parents.isEmpty())
            m_tocModel->appendRow(item);
        else
            parents.last()->appendRow(item);
        parents.append(item);
    }
    m_tocView->expandToDepth(0);
    const bool hasToc = m_tocModel->rowCount() > 0;
    m_tocView->setVisible(hasToc);
    m_noToc->setVisible(!hasToc);
    pushMarks();
    flushPendingReveal();
}

void EpubReaderTab::onRelocated(const EpubLocation& l)
{
    m_popup->hide();
    m_chapterLabel->setText(l.tocLabel);
    m_chapterLabel->setToolTip(l.tocLabel);
    QString progress = QStringLiteral("%1 %").arg(qRound(l.fraction * 100));
    if (l.sectionMinutes >= 1)
        progress += tr(" · ~%1 min restantes dans le chapitre").arg(qRound(l.sectionMinutes));
    m_progressLabel->setText(progress);
    m_pageLabel->setText(l.pages > 0 ? tr("Page %1 sur %2 dans ce chapitre").arg(std::min(l.page, l.pages)).arg(l.pages)
                                     : QString());
    m_prevChapter->setEnabled(l.section > 0);
    m_nextChapter->setEnabled(l.sections == 0 || l.section < l.sections - 1);
    m_back->setEnabled(true);
    m_forward->setEnabled(true);
    m_pane->setCurrentPage(l.section);
    m_comments->setCurrentUnit(l.section);
    // Chapitre courant en évidence dans le sommaire
    std::function<QModelIndex(const QModelIndex&)> find = [&](const QModelIndex& parent) -> QModelIndex {
        QModelIndex found;
        for (int i = 0; i < m_tocModel->rowCount(parent); ++i) {
            const QModelIndex idx = m_tocModel->index(i, 0, parent);
            if (idx.data(kTocHrefRole).toString() == l.tocHref)
                return idx;
            if (const QModelIndex child = find(idx); child.isValid())
                return child;
        }
        return found;
    };
    const QModelIndex current = l.tocHref.isEmpty() ? QModelIndex() : find(QModelIndex());
    if (current.isValid()) {
        m_tocView->setCurrentIndex(current);
        m_tocView->scrollTo(current, QAbstractItemView::EnsureVisible);
    }
    m_saveTimer.start();
}

void EpubReaderTab::goToChapter(int delta)
{
    if (!m_view)
        return;
    const int target = m_view->location().section + delta;
    // Premier lien du sommaire pointant vers une section au-delà (ou en deçà) de la courante
    QString href;
    if (delta > 0) {
        for (const auto& e : m_view->toc())
            if (e.section >= target && e.depth == 0) {
                href = e.href;
                break;
            }
    } else {
        for (const auto& e : m_view->toc())
            if (e.section >= 0 && e.section <= target && e.depth == 0)
                href = e.href;
    }
    if (!href.isEmpty())
        m_view->goTo(href);
    else
        m_view->evaluate(QStringLiteral("StudyLab.goTo(%1)").arg(std::max(0, target)), [](const QVariant&) {});
}

void EpubReaderTab::applyFooterColors()
{
    // Le pied de page prolonge la page : mêmes couleurs que le thème de lecture.
    const ReadingColors c = readingColors(m_display.theme);
    QPalette pal = m_footer->palette();
    pal.setColor(QPalette::Window, c.bg);
    pal.setColor(QPalette::WindowText, c.muted);
    m_footer->setPalette(pal);
    m_footer->setAutoFillBackground(true);
    m_pageLabel->setPalette(pal);
}

void EpubReaderTab::setDisplaySettings(const EpubSettings& settings)
{
    m_display = settings;
    applyFooterColors();
    m_settings.setValue(kDisplayKey, settings.toStored());
    if (m_view)
        m_view->setSettings(settings);
    if (m_typo->isVisible())
        m_typo->setSettings(settings);
    pushMarks();  // teintes adaptées au fond (clair ou sombre)
}

void EpubReaderTab::savePosition()
{
    if (!m_view || !m_view->isOpened() || m_view->location().cfi.isEmpty())
        return;
    m_saveTimer.stop();
    db::ReadingPosition rp;
    rp.locator = {{QStringLiteral("type"), QStringLiteral("epub")}, {QStringLiteral("cfi"), m_view->location().cfi}};
    rp.progress = std::clamp(m_view->location().fraction, 0.0, 1.0);
    (void)m_library.saveReadingPosition(m_documentId, rp);
}

void EpubReaderTab::setFocusMode(bool on)
{
    if (on == m_focusMode)
        return;
    m_focusMode = on;
    {
        const QSignalBlocker block(m_focusButton);
        m_focusButton->setChecked(on);
    }
    m_toolbar->setVisible(!on);
    m_side->setVisible(!on && m_tocToggle->isChecked());
    m_sidePanel->setVisible(!on && m_paneToggle->isChecked());
    m_popup->hide();
    if (on)
        m_searchBar->hide();
    emit focusModeToggled(on);
    if (m_view)
        m_view->setFocus();
}

void EpubReaderTab::showSearch()
{
    if (!m_loaded)
        return;
    m_searchBar->show();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void EpubReaderTab::updateSearchCount()
{
    const int n = m_view ? m_view->searchResultCount() : 0;
    const bool done = !m_view || m_view->searchDone();
    if (m_searchEdit->text().trimmed().isEmpty())
        m_searchCount->clear();
    else if (n == 0)
        m_searchCount->setText(done ? tr("Aucun résultat") : tr("Recherche…"));
    else if (m_searchIndex >= 0)
        m_searchCount->setText(tr("%1 sur %2").arg(m_searchIndex + 1).arg(n));
    else
        m_searchCount->setText(countLabel(n, tr("résultat"), tr("résultats")));
}

void EpubReaderTab::findNext()
{
    const int n = m_view ? m_view->searchResultCount() : 0;
    if (n == 0)
        return;
    m_searchIndex = (m_searchIndex + 1) % n;
    m_view->showSearchResult(m_searchIndex);
    updateSearchCount();
}

void EpubReaderTab::findPrevious()
{
    const int n = m_view ? m_view->searchResultCount() : 0;
    if (n == 0)
        return;
    m_searchIndex = m_searchIndex <= 0 ? n - 1 : m_searchIndex - 1;
    m_view->showSearchResult(m_searchIndex);
    updateSearchCount();
}

// ───────────────────────── Annotations ─────────────────────────

QToolButton* EpubReaderTab::toolButtonFor(Tool tool) const
{
    return m_toolButtons.value(int(tool));
}

void EpubReaderTab::setTool(Tool tool)
{
    m_tool = tool;
    for (auto it = m_toolButtons.cbegin(); it != m_toolButtons.cend(); ++it) {
        const QSignalBlocker block(it.value());
        it.value()->setChecked(Tool(it.key()) == tool);
    }
    m_popup->hide();
    if (m_view)
        m_view->setFocus();
}

QString EpubReaderTab::chapterOf(int section) const
{
    if (!m_view)
        return {};
    // Entrée de premier ou second niveau la plus proche avant la section
    QString title;
    int best = -1;
    for (const EpubTocEntry& e : m_view->toc()) {
        if (e.depth > 1 || e.section < 0 || e.section > section)
            continue;
        if (e.section > best) {
            best = e.section;
            title = e.label;
        }
    }
    return title;
}

void EpubReaderTab::pushMarks()
{
    // Les surlignages sont dessinés en transparence : on prend les teintes d'accent du fond courant.
    const bool dark = readingColors(m_display.theme).dark;
    static const Palette light = Palette::light();
    static const Palette darkPal = Palette::darkPalette();
    const Palette& pal = dark ? darkPal : light;
    QJsonArray list;
    for (const auto& r : m_records) {
        const auto loc = EpubLocator::fromJson(r.locator);
        if (!loc)
            continue;
        const QString type = r.type == AnnotationType::Underline   ? QStringLiteral("underline")
                             : r.type == AnnotationType::Strikeout ? QStringLiteral("strikeout")
                             : r.type == AnnotationType::Bookmark  ? QStringLiteral("bookmark")
                                                                   : QStringLiteral("highlight");
        list.append(QJsonObject{{QStringLiteral("id"), double(r.id)},
                                {QStringLiteral("cfi"), loc->cfi},
                                {QStringLiteral("type"), type},
                                {QStringLiteral("fill"), annotationAccent(pal, r.color).name()},
                                {QStringLiteral("stroke"), annotationAccent(pal, r.color).name()}});
    }
    if (m_view && m_view->isOpened())
        m_view->setAnnotations(list);
    m_pane->setAnnotations(m_records);
}

void EpubReaderTab::copySelection()
{
    if (m_selection)
        QApplication::clipboard()->setText(normalizeQuote(m_selection->text));
}

void EpubReaderTab::onSelection(const EpubSelection& s)
{
    m_selection = s;
    if (m_tool != Tool::Select) {
        const AnnotationType type = m_tool == Tool::Underline   ? AnnotationType::Underline
                                    : m_tool == Tool::Strikeout ? AnnotationType::Strikeout
                                                                : AnnotationType::Highlight;
        if (auto r = annotateSelection(type, m_color); !r)
            reportError(r.error());
        return;
    }
    if (s.rect.isEmpty())
        return;
    m_popupAnnotation = 0;
    m_popup->showFor(AnnotationPopup::Mode::Create, s.rect, m_color);
    m_popup->raise();
}

Result<QList<AnnotationRecord>> EpubReaderTab::annotateSelection(AnnotationType type, AnnotationColor color)
{
    if (!m_selection)
        return QList<AnnotationRecord>{};
    const EpubSelection s = *m_selection;
    EpubLocator loc;
    loc.cfi = s.cfi;
    loc.section = s.section;
    loc.fraction = s.fraction;
    loc.progress = s.progress;
    loc.exact = normalizeQuote(s.text);
    loc.prefix = s.prefix;
    loc.suffix = s.suffix;
    AnnotationRecord r;
    r.documentId = m_documentId;
    r.type = type;
    r.color = color;
    r.locator = loc.toJson();
    r.selectedText = s.text;
    auto res = m_annotations.create(r);
    if (!res)
        return res.error();
    m_selection.reset();
    if (m_view)
        m_view->clearSelection();
    m_popup->hide();
    return QList<AnnotationRecord>{res.value()};
}

void EpubReaderTab::toggleBookmark()
{
    if (!m_view || !m_view->isOpened())
        return;
    if (m_visibleBookmark) {
        if (auto r = m_annotations.remove(m_visibleBookmark); !r)
            reportError(r.error());
        m_visibleBookmark = 0;
        updateBookmarkButton();
        return;
    }
    m_view->evaluate(QStringLiteral("StudyLab.currentAnchor()"), [this, guard = QPointer<EpubReaderTab>(this)](const QVariant& v) {
        if (!guard)
            return;
        const QVariantMap a = v.toMap();
        if (a.value(QStringLiteral("cfi")).toString().isEmpty())
            return;
        EpubLocator loc;
        loc.cfi = a.value(QStringLiteral("cfi")).toString();
        loc.section = a.value(QStringLiteral("section")).toInt();
        loc.fraction = a.value(QStringLiteral("fraction")).toDouble();
        loc.progress = a.value(QStringLiteral("progress")).toDouble();
        AnnotationRecord r;
        r.documentId = m_documentId;
        r.type = AnnotationType::Bookmark;
        r.color = AnnotationColor::Blue;
        r.locator = loc.toJson();
        r.title = m_view->location().tocLabel;
        auto created = m_annotations.create(r);
        if (!created) {
            reportError(created.error());
            return;
        }
        m_visibleBookmark = created.value().id;
        updateBookmarkButton();
    });
}

void EpubReaderTab::updateBookmarkButton()
{
    if (!m_bookmarkButton)
        return;
    const bool on = m_visibleBookmark != 0
                    && std::any_of(m_records.cbegin(), m_records.cend(),
                                   [this](const AnnotationRecord& r) { return r.id == m_visibleBookmark; });
    const QSignalBlocker block(m_bookmarkButton);
    m_bookmarkButton->setChecked(on);
    m_bookmarkButton->setToolTip(on ? tr("Retirer le signet (Ctrl+D)") : tr("Signet ici (Ctrl+D)"));
}

void EpubReaderTab::revealAnnotation(qint64 id)
{
    for (const auto& r : m_records) {
        if (r.id != id)
            continue;
        if (const auto loc = EpubLocator::fromJson(r.locator); loc && m_view)
            m_view->reveal(loc->cfi);
        m_pane->setActive(id);
        return;
    }
}

std::optional<CommentTarget> EpubReaderTab::selectionTarget() const
{
    if (!m_selection)
        return std::nullopt;
    const EpubSelection& s = *m_selection;
    EpubLocator loc;
    loc.cfi = s.cfi;
    loc.section = s.section;
    loc.fraction = s.fraction;
    loc.progress = s.progress;
    loc.exact = normalizeQuote(s.text);
    loc.prefix = s.prefix;
    loc.suffix = s.suffix;
    CommentTarget t;
    t.anchor = CommentAnchor::Passage;
    t.unitIndex = s.section;
    t.locator = loc.toJson();
    t.quote = loc.exact;
    t.where = QStringLiteral("%1 %").arg(qRound(s.progress * 100));
    return t;
}

CommentTarget EpubReaderTab::positionTarget() const
{
    CommentTarget t;
    if (!m_view)
        return t;
    const EpubLocation l = m_view->location();
    if (!l.cfi.startsWith(QLatin1String("epubcfi(")))
        return t;
    EpubLocator loc;
    loc.cfi = l.cfi;
    loc.section = l.section;
    loc.fraction = l.pages > 0 ? double(std::max(0, l.page - 1)) / l.pages : 0.0;
    loc.progress = l.fraction;
    t.anchor = CommentAnchor::Page;
    t.unitIndex = l.section;
    t.locator = loc.toJson();
    t.where = QStringLiteral("%1 %").arg(qRound(l.fraction * 100));
    return t;
}

void EpubReaderTab::revealLocator(const QJsonObject& locator)
{
    if (const auto loc = EpubLocator::fromJson(locator); loc && m_view)
        m_view->reveal(loc->cfi);
}

void EpubReaderTab::showEditPopup(qint64 id)
{
    auto r = m_annotations.find(id);
    if (!r)
        return;
    m_popupAnnotation = id;
    QRect rect = m_lastAnnotationRect;
    if (rect.isEmpty())
        rect = QRect(m_viewHost->rect().center(), QSize(1, 1));
    m_popup->showFor(AnnotationPopup::Mode::Edit, rect, r.value().color, r.value().type, !r.value().note.isEmpty());
    m_popup->raise();
}

}  // namespace studylab::ui
