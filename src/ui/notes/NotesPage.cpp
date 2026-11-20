#include "ui/notes/NotesPage.h"
#include "ui/shell/Shortcuts.h"

#include "core/domain/Document.h"

#include "core/domain/Comment.h"
#include "services/notes/NoteDrafts.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "services/settings/SettingsService.h"
#include "ui/components/FlowLayout.h"
#include "ui/components/MarkdownHighlighter.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/Widgets.h"
#include "ui/notes/NoteEditor.h"
#include "ui/notes/NoteExporter.h"
#include "ui/notes/NotePreview.h"
#include "ui/organize/OrganizeWidgets.h"
#include "ui/reader/MarkdownView.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>

#include <memory>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextBlock>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace studylab::ui {

namespace {

const QString kModeKey = QStringLiteral("notes/mode");
const QString kLastNoteKey = QStringLiteral("notes/lastNote");
const QString kSplitKey = QStringLiteral("notes/split");
constexpr int kIdRole = Qt::UserRole;
constexpr int kFolderRole = Qt::UserRole + 1;

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

/// Ligne cliquable du panneau de contexte : titre et extrait sur deux lignes.
class ContextRow : public QPushButton {
public:
    ContextRow(const QString& title, const QString& detail, QWidget* parent) : QPushButton(parent)
    {
        setObjectName(QStringLiteral("NoteLinkRow"));
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(title);
        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(10, 7, 10, 8);
        v->setSpacing(2);
        auto* t = makeLabel(title, nullptr, this);
        t->setStyleSheet(QStringLiteral("font-weight: 600;"));
        t->setAttribute(Qt::WA_TransparentForMouseEvents);
        t->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        v->addWidget(t);
        if (!detail.isEmpty()) {
            auto* d = makeLabel(detail, "muted", this);
            d->setWordWrap(true);
            d->setAttribute(Qt::WA_TransparentForMouseEvents);
            d->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            v->addWidget(d);
        }
    }
    QSize sizeHint() const override { return layout()->sizeHint(); }
    QSize minimumSizeHint() const override { return {0, layout()->minimumSize().height()}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return layout()->totalHeightForWidth(w); }
};

/// Titre de section du panneau de contexte, avec icône.
QWidget* sectionTitle(ThemeManager& theme, const QString& icon, const QString& text, QLabel** countOut, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    if (!icon.isEmpty()) {
        auto* i = new QLabel(w);
        i->setPixmap(Icons::get(icon, theme.palette().text2, 16).pixmap(16, 16));
        h->addWidget(i);
    }
    auto* l = makeLabel(text, "h3", w);
    h->addWidget(l);
    if (countOut) {
        *countOut = makeLabel(QStringLiteral("0"), nullptr, w);
        (*countOut)->setObjectName(QStringLiteral("CountBadge"));
        h->addWidget(*countOut);
    }
    h->addStretch(1);
    return w;
}

void clearLayout(QVBoxLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

}  // namespace

NotesPage::NotesPage(AppContext& ctx, QWidget* parent) : QWidget(parent), m_ctx(ctx), m_refs(ctx.db, ctx.notes)
{
    setObjectName(QStringLiteral("NotesPage"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildNav();
    buildEditor();
    buildContext();
    root->addWidget(m_nav);
    root->addWidget(m_center, 1);
    root->addWidget(m_context);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(700);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { saveNow(); });
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(200);
    connect(&m_previewTimer, &QTimer::timeout, this, [this] {
        refreshPreview();
        refreshOutline();
    });
    m_contextTimer.setSingleShot(true);
    m_contextTimer.setInterval(0);
    connect(&m_contextTimer, &QTimer::timeout, this, &NotesPage::refreshContext);
    m_undoTimer.setSingleShot(true);
    m_undoTimer.setInterval(8000);
    connect(&m_undoTimer, &QTimer::timeout, m_undoBar, &QWidget::hide);

    NoteService& notes = m_ctx.notes;
    connect(&notes, &NoteService::noteAdded, this, [this] {
        if (m_importing)
            return;  // import d'un coffre : une seule mise à jour à la fin
        refreshTree();
        refreshTags();
    });
    connect(&notes, &NoteService::noteRemoved, this, [this](qint64 id) {
        refreshTree();
        refreshTags();
        if (id == m_currentId) {
            m_currentId = 0;
            m_current = {};
            m_dirty = false;
            showEmpty(true);
        }
    });
    connect(&notes, &NoteService::noteChanged, this, [this](const NoteRecord& n) {
        if (n.id == m_currentId && !m_dirty && n.body != m_editor->toPlainText()) {
            // Texte modifié ailleurs (renommage d'une note citée) : on recharge sans perdre la position.
            const int pos = m_editor->textCursor().position();
            const int scroll = m_editor->verticalScrollBar()->value();
            loadNote(n);
            QTextCursor c = m_editor->textCursor();
            c.setPosition(std::min(pos, int(m_editor->toPlainText().size())));
            m_editor->setTextCursor(c);
            m_editor->verticalScrollBar()->setValue(scroll);
        } else if (n.id == m_currentId) {
            m_current.title = n.title;
            m_current.type = n.type;
            m_current.folder = n.folder;
            m_current.pinned = n.pinned;
        }
        refreshTree();
        refreshTags();
    });
    connect(&notes, &NoteService::linksChanged, this, [this] {
        m_contextTimer.start();
        m_previewTimer.start();
    });
    connect(&m_ctx.theme, &ThemeManager::themeChanged, this, [this] {
        pushPreviewTheme();
        refreshTree();
        refreshContext();
        m_typeButton->setIcon(Icons::get(noteTypeIcon(m_current.type), m_ctx.theme.palette().primaryStrong, 16));
        m_newButton->setIcon(Icons::get(QStringLiteral("plus"), m_ctx.theme.palette().text2, 18));
    });

    auto* newShortcut = Shortcuts::create(m_ctx.settings, QStringLiteral("notes.new"), this);
    newShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(newShortcut, &QShortcut::activated, this, [this] { newNote(NoteType::Personal); });
    auto* saveShortcut = Shortcuts::create(m_ctx.settings, QStringLiteral("notes.save"), this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, [this] { saveNow(); });

    const int mode = m_ctx.settings.value(kModeKey, 1).toInt();
    setMode(mode == 0 ? Mode::Source : mode == 2 ? Mode::Preview : Mode::Split);
    refreshTree();
    refreshTags();
    const qint64 last = qint64(m_ctx.settings.value(kLastNoteKey, 0).toDouble());
    if (last <= 0 || !openNote(last)) {
        const QList<qint64> ids = visibleNoteIds();
        if (ids.isEmpty() || !openNote(ids.first()))
            showEmpty(true);
    }
}

NotesPage::~NotesPage()
{
    saveNow();
}

// ───────────────────────── Construction ─────────────────────────

void NotesPage::buildNav()
{
    ThemeManager& theme = m_ctx.theme;
    m_nav = new QFrame(this);
    m_nav->setObjectName(QStringLiteral("NotesNav"));
    m_nav->setFixedWidth(theme.px(264));
    auto* v = new QVBoxLayout(m_nav);
    v->setContentsMargins(12, 14, 12, 12);
    v->setSpacing(10);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(6, 0, 0, 0);
    head->addWidget(makeLabel(tr("Mes notes"), "h2", m_nav), 1);
    m_newButton = new QToolButton(m_nav);
    m_newButton->setObjectName(QStringLiteral("NewNoteButton"));
    m_newButton->setProperty("readerTool", true);
    m_newButton->setIcon(Icons::get(QStringLiteral("plus"), theme.palette().text2, 18));
    Shortcuts::onChanged(m_ctx.settings, m_newButton, [this] {
        m_newButton->setToolTip(Shortcuts::withKey(m_ctx.settings, tr("Nouvelle note"), QStringLiteral("notes.new")));
    });
    m_newButton->setAccessibleName(tr("Nouvelle note"));
    m_newButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(m_newButton);
    for (NoteType t : allNoteTypes()) {
        QAction* a = menu->addAction(Icons::get(noteTypeIcon(t), theme.palette().text2, 16), noteTypeLabel(t));
        connect(a, &QAction::triggered, this, [this, t] { newNote(t); });
    }
    menu->addSeparator();
    QAction* importAction = menu->addAction(Icons::get(QStringLiteral("folder"), theme.palette().text2, 16),
                                            tr("Importer un coffre Obsidian ou un dossier Markdown…"));
    importAction->setObjectName(QStringLiteral("ImportVaultAction"));
    connect(importAction, &QAction::triggered, this, &NotesPage::chooseVault);
    m_newButton->setMenu(menu);
    head->addWidget(m_newButton);
    v->addLayout(head);

    m_filter = new QLineEdit(m_nav);
    m_filter->setObjectName(QStringLiteral("NoteFilter"));
    m_filter->setPlaceholderText(tr("Filtrer…"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setAccessibleName(tr("Filtrer les notes"));
    m_filter->addAction(Icons::get(QStringLiteral("search"), theme.palette().text3, 16), QLineEdit::LeadingPosition);
    connect(m_filter, &QLineEdit::textChanged, this, &NotesPage::refreshTree);
    v->addWidget(m_filter);

    m_tree = new QTreeWidget(m_nav);
    m_tree->setObjectName(QStringLiteral("NoteTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(theme.px(16));
    m_tree->setAccessibleName(tr("Notes"));
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIconSize(QSize(16, 16));
    m_tree->setUniformRowHeights(true);
    m_tree->setExpandsOnDoubleClick(true);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &NotesPage::showTreeMenu);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        const qint64 id = item->data(0, kIdRole).toLongLong();
        if (id > 0 && id != m_currentId)
            openNote(id);
    });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const qint64 id = item->data(0, kIdRole).toLongLong();
        if (id > 0) {
            if (id != m_currentId)
                openNote(id);
            m_editor->setFocus();
        }
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
        m_collapsedFolders.insert(item->data(0, kFolderRole).toString());
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        m_collapsedFolders.remove(item->data(0, kFolderRole).toString());
    });
    v->addWidget(m_tree, 1);

    m_importStatus = makeLabel(QString(), "faint", m_nav);
    m_importStatus->setObjectName(QStringLiteral("VaultImportStatus"));
    m_importStatus->setWordWrap(true);
    m_importStatus->hide();
    v->addWidget(m_importStatus);

    m_tagsTitle = makeLabel(tr("TAGS"), "caption", m_nav);
    v->addWidget(m_tagsTitle);
    m_tagHost = new QWidget(m_nav);
    m_tagFlow = new FlowLayout(m_tagHost, 6);
    m_tagFlow->setContentsMargins(0, 0, 0, 0);
    v->addWidget(m_tagHost);

    m_undoBar = new QFrame(m_nav);
    m_undoBar->setObjectName(QStringLiteral("UndoBar"));
    auto* uh = new QHBoxLayout(m_undoBar);
    uh->setContentsMargins(12, 6, 6, 6);
    m_undoText = makeLabel(QString(), nullptr, m_undoBar);
    m_undoText->setWordWrap(true);
    auto* undo = new QPushButton(tr("Annuler"), m_undoBar);
    undo->setObjectName(QStringLiteral("UndoButton"));
    undo->setProperty("variant", QStringLiteral("ghost"));
    uh->addWidget(m_undoText, 1);
    uh->addWidget(undo);
    connect(undo, &QPushButton::clicked, this, &NotesPage::undoDelete);
    m_undoBar->hide();
    v->addWidget(m_undoBar);
}

void NotesPage::buildEditor()
{
    ThemeManager& theme = m_ctx.theme;
    m_center = new QStackedWidget(this);

    // État vide
    m_emptyState = new QWidget(m_center);
    auto* ev = new QVBoxLayout(m_emptyState);
    ev->addStretch(2);
    auto* icon = new QLabel(m_emptyState);
    icon->setPixmap(Icons::get(QStringLiteral("note"), theme.palette().text3, 40).pixmap(40, 40));
    icon->setAlignment(Qt::AlignCenter);
    ev->addWidget(icon);
    auto* title = makeLabel(tr("Aucune note ouverte"), "h2", m_emptyState);
    title->setAlignment(Qt::AlignCenter);
    ev->addWidget(title);
    auto* hint = makeLabel(tr("Écrivez en Markdown, avec formules $…$ et liens [[…]] entre vos notes."), "muted", m_emptyState);
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    ev->addWidget(hint);
    auto* create = new QPushButton(tr("Nouvelle note"), m_emptyState);
    create->setObjectName(QStringLiteral("EmptyNewNote"));
    create->setProperty("variant", QStringLiteral("primary"));
    connect(create, &QPushButton::clicked, this, [this] { newNote(NoteType::Personal); });
    auto* importVaultBtn = new QPushButton(tr("Importer un coffre Obsidian…"), m_emptyState);
    importVaultBtn->setObjectName(QStringLiteral("EmptyImportVault"));
    importVaultBtn->setToolTip(tr("Coffre Obsidian, dossier de fichiers Markdown ou export StudyLab"));
    connect(importVaultBtn, &QPushButton::clicked, this, &NotesPage::chooseVault);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(create);
    row->addWidget(importVaultBtn);
    row->addStretch(1);
    ev->addLayout(row);
    ev->addStretch(3);
    m_center->addWidget(m_emptyState);

    // Éditeur
    m_editorPane = new QFrame(m_center);
    m_editorPane->setObjectName(QStringLiteral("NoteEditorPane"));
    auto* v = new QVBoxLayout(m_editorPane);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* bar = new QFrame(m_editorPane);
    bar->setObjectName(QStringLiteral("NoteBar"));
    auto* bh = new QHBoxLayout(bar);
    bh->setContentsMargins(16, 10, 16, 10);
    bh->setSpacing(10);
    m_typeButton = new QToolButton(bar);
    m_typeButton->setObjectName(QStringLiteral("NoteTypeChip"));
    m_typeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_typeButton->setPopupMode(QToolButton::InstantPopup);
    m_typeButton->setToolTip(tr("Type de la note"));
    auto* typeMenu = new QMenu(m_typeButton);
    for (NoteType t : allNoteTypes()) {
        QAction* a = typeMenu->addAction(noteTypeLabel(t));
        connect(a, &QAction::triggered, this, [this, t] {
            if (m_currentId <= 0)
                return;
            if (auto r = m_ctx.notes.setType(m_currentId, t)) {
                m_current.type = t;
                m_typeButton->setText(noteTypeLabel(t));
                m_typeButton->setIcon(Icons::get(noteTypeIcon(t), m_ctx.theme.palette().primaryStrong, 16));
            }
        });
    }
    m_typeButton->setMenu(typeMenu);
    bh->addWidget(m_typeButton);
    m_saveState = makeLabel(QString(), nullptr, bar);
    m_saveState->setObjectName(QStringLiteral("SaveState"));
    bh->addWidget(m_saveState);
    bh->addStretch(1);
    m_modeControl = new SegmentedControl({tr("Source"), tr("Partagé"), tr("Aperçu")}, bar);
    m_modeControl->setAccessibleName(tr("Affichage de la note"));
    connect(m_modeControl, &SegmentedControl::currentIndexChanged, this, [this](int i) {
        setMode(i == 0 ? Mode::Source : i == 2 ? Mode::Preview : Mode::Split);
    });
    bh->addWidget(m_modeControl);
    v->addWidget(bar);
    v->addWidget(makeSeparator(m_editorPane));

    // Fiche de révision générée (V2-3) : brouillon à relire avant de la considérer comme sienne.
    m_draftBanner = new QFrame(m_editorPane);
    m_draftBanner->setObjectName(QStringLiteral("DraftBanner"));
    auto* dh = new QHBoxLayout(m_draftBanner);
    dh->setContentsMargins(28, 8, 20, 8);
    dh->setSpacing(10);
    auto* draftText = makeLabel(tr("Fiche générée automatiquement : brouillon à relire. Reformulez, gardez l'essentiel, "
                                   "puis marquez-la comme relue."),
                                nullptr, m_draftBanner);
    draftText->setObjectName(QStringLiteral("DraftBannerText"));
    draftText->setWordWrap(true);
    dh->addWidget(draftText, 1);
    m_draftDone = new QPushButton(tr("Marquer comme relue"), m_draftBanner);
    m_draftDone->setObjectName(QStringLiteral("MarkSheetReviewed"));
    connect(m_draftDone, &QPushButton::clicked, this, &NotesPage::markSheetReviewed);
    dh->addWidget(m_draftDone);
    m_draftBanner->hide();
    v->addWidget(m_draftBanner);

    m_split = new QSplitter(Qt::Horizontal, m_editorPane);
    m_split->setObjectName(QStringLiteral("NoteSplit"));
    m_split->setChildrenCollapsible(false);
    m_split->setHandleWidth(1);

    auto* source = new QWidget(m_split);
    auto* sv = new QVBoxLayout(source);
    sv->setContentsMargins(28, 14, 20, 0);
    sv->setSpacing(4);
    m_title = new QLineEdit(source);
    m_title->setObjectName(QStringLiteral("NoteTitle"));
    m_title->setPlaceholderText(tr("Titre"));
    m_title->setAccessibleName(tr("Titre de la note"));
    connect(m_title, &QLineEdit::textEdited, this, [this] { markDirty(); });
    connect(m_title, &QLineEdit::editingFinished, this, [this] {
        if (m_dirty)
            saveNow();
        if (m_currentId > 0 && m_title->text() != m_current.title)
            m_title->setText(m_current.title);
    });
    sv->addWidget(m_title);
    m_editor = new NoteEditor(source);
    m_editor->setObjectName(QStringLiteral("NoteText"));
    m_editor->setAccessibleName(tr("Texte de la note"));
    m_editor->setPlaceholderText(tr("Écrire… Markdown, formules $…$, liens [[…]], #tags"));
    // Menu contextuel standard, plus la création d'une carte mémoire depuis la sélection.
    m_editor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_editor, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        std::unique_ptr<QMenu> menu(m_editor->createStandardContextMenu(pos));
        if (m_currentId > 0) {
            menu->addSeparator();
            menu->addAction(Icons::get(QStringLiteral("cards"), m_ctx.theme.palette().text2, 16),
                            m_editor->textCursor().hasSelection() ? tr("Créer une carte depuis la sélection…")
                                                                  : tr("Créer une carte…"),
                            this, [this] { requestCard(m_currentId); });
        }
        menu->exec(m_editor->viewport()->mapToGlobal(pos));
    });
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(theme.px(13));  // police réelle : feuille de style (#NoteText)
    m_editor->setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(u' ') * 4);
    new MarkdownHighlighter(theme, m_editor->document());
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_loading)
            markDirty();
    });
    connect(m_editor, &NoteEditor::imageInserted, this, &NotesPage::insertImage);
    // Après « [[ » : notes et documents, puis « @clé » des références.
    m_editor->setLinkCompletions([this] { return m_ctx.notes.linkCompletions() + m_refs.completions(); });
    m_editor->installEventFilter(this);
    sv->addWidget(m_editor, 1);
    m_split->addWidget(source);

    m_preview = new MarkdownView(m_split);
    m_preview->setObjectName(QStringLiteral("NotePreview"));
    m_preview->setAccessibleName(tr("Aperçu de la note"));
    connect(m_preview, &MarkdownView::wikiLinkActivated, this, [this](const QString& target) { followLink(target); });
    connect(m_preview, &MarkdownView::linkActivated, this, [this](const QUrl& url) {
        if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))
            QDesktopServices::openUrl(url);
        else if (url.scheme() == QLatin1String("studylab"))
            emit appLinkActivated(url.toString());
    });
    connect(m_preview, &MarkdownView::ready, this, [this] { refreshPreview(); });
    m_split->addWidget(m_preview);
    m_split->setStretchFactor(0, 1);
    m_split->setStretchFactor(1, 1);
    m_preview->setMinimumWidth(theme.px(220));
    source->setMinimumWidth(theme.px(260));
    // Sans position enregistrée, les deux volets se partagent la largeur au premier affichage (showEvent).
    m_splitRestored = m_split->restoreState(QByteArray::fromBase64(m_ctx.settings.value(kSplitKey).toString().toLatin1()));
    connect(m_split, &QSplitter::splitterMoved, this, [this] { m_ctx.settings.setValue(kSplitKey, QString::fromLatin1(m_split->saveState().toBase64())); });
    pushPreviewTheme();
    v->addWidget(m_split, 1);
    m_center->addWidget(m_editorPane);
}

void NotesPage::buildContext()
{
    ThemeManager& theme = m_ctx.theme;
    m_context = new QFrame(this);
    m_context->setObjectName(QStringLiteral("NoteContext"));
    m_context->setFixedWidth(theme.px(280));
    auto* outer = new QVBoxLayout(m_context);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(m_context);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("NoteContextBody"));
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(14, 16, 14, 16);
    v->setSpacing(8);

    // Sources : seulement pour les notes tirées de la lecture.
    m_sourcesSection = new QWidget(body);
    auto* sv = new QVBoxLayout(m_sourcesSection);
    sv->setContentsMargins(0, 0, 0, 10);
    sv->setSpacing(8);
    sv->addWidget(sectionTitle(theme, QStringLiteral("book-open"), tr("Sources"), nullptr, m_sourcesSection));
    m_sourceList = new QVBoxLayout;
    m_sourceList->setSpacing(6);
    sv->addLayout(m_sourceList);
    m_sourcesSection->setVisible(false);
    v->addWidget(m_sourcesSection);

    v->addWidget(sectionTitle(theme, QStringLiteral("link"), tr("Rétroliens"), &m_backlinkCount, body));
    m_backlinkList = new QVBoxLayout;
    m_backlinkList->setSpacing(6);
    v->addLayout(m_backlinkList);
    m_backlinkEmpty = makeLabel(tr("Aucune note ne cite encore celle-ci."), "muted", body);
    m_backlinkEmpty->setWordWrap(true);
    v->addWidget(m_backlinkEmpty);
    v->addSpacing(10);

    v->addWidget(sectionTitle(theme, QString(), tr("Plan"), nullptr, body));
    m_outlineList = new QVBoxLayout;
    m_outlineList->setSpacing(0);
    v->addLayout(m_outlineList);
    m_outlineEmpty = makeLabel(tr("Les titres « # » de la note apparaîtront ici."), "muted", body);
    m_outlineEmpty->setWordWrap(true);
    v->addWidget(m_outlineEmpty);
    v->addSpacing(10);

    m_unresolvedTitle = makeLabel(tr("Liens non résolus"), "h3", body);
    v->addWidget(m_unresolvedTitle);
    m_unresolvedList = new QVBoxLayout;
    m_unresolvedList->setSpacing(2);
    v->addLayout(m_unresolvedList);
    v->addStretch(1);

    scroll->setWidget(body);
    outer->addWidget(scroll);
}

void NotesPage::pushPreviewTheme()
{
    const Palette& p = m_ctx.theme.palette();
    QJsonObject vars{
        {QStringLiteral("bg"), hex(p.surface)},
        {QStringLiteral("card"), hex(p.dark ? p.surface2 : p.bg)},
        {QStringLiteral("card-border"), hex(p.borderSoft)},
        {QStringLiteral("text"), hex(p.text)},
        {QStringLiteral("muted"), hex(p.text2)},
        {QStringLiteral("border"), hex(p.border)},
        {QStringLiteral("link"), hex(p.primaryStrong)},
        {QStringLiteral("accent"), hex(p.primary)},
        {QStringLiteral("wiki"), hex(p.primaryStrong)},
        {QStringLiteral("quote-bar"), hex(p.accYellow)},
        {QStringLiteral("code-bg"), p.dark ? QStringLiteral("rgba(255,255,255,.08)") : QStringLiteral("rgba(0,0,0,.05)")},
        {QStringLiteral("font"), QStringLiteral("\"%1\", system-ui, sans-serif").arg(font().family())},
        {QStringLiteral("size"), QStringLiteral("%1px").arg(m_ctx.theme.px(13))},
    };
    m_preview->setThemeVars(vars);
}

// ───────────────────────── Notes ─────────────────────────

void NotesPage::showEmpty(bool empty)
{
    m_center->setCurrentWidget(empty ? m_emptyState : m_editorPane);
    m_context->setVisible(!empty);
}

void NotesPage::setSaveState(const QString& text, bool error)
{
    m_saveState->setText(text);
    m_saveState->setProperty("status", error ? QStringLiteral("error") : QString());
    m_saveState->style()->unpolish(m_saveState);
    m_saveState->style()->polish(m_saveState);
}

void NotesPage::markDirty()
{
    if (m_currentId <= 0)
        return;
    m_dirty = true;
    setSaveState(tr("Modification…"));
    m_saveTimer.start();
    m_previewTimer.start();
}

void NotesPage::loadNote(const NoteRecord& note)
{
    m_loading = true;
    m_current = note;
    m_currentId = note.id;
    m_title->setText(note.title);
    m_title->setCursorPosition(0);  // début du titre visible s'il est plus long que le champ
    m_editor->setPlainText(note.body);
    m_typeButton->setText(noteTypeLabel(note.type));
    m_typeButton->setIcon(Icons::get(noteTypeIcon(note.type), m_ctx.theme.palette().primaryStrong, 16));
    m_draftBanner->setVisible(NoteDrafts::isDraftSheet(note.body));
    m_loading = false;
    m_dirty = false;
    setSaveState(tr("Enregistré"));
}

bool NotesPage::openNote(qint64 id)
{
    if (id == m_currentId && id > 0) {
        showEmpty(false);
        return true;
    }
    saveNow();
    auto note = m_ctx.notes.find(id);
    if (!note)
        return false;
    loadNote(note.value());
    m_editor->moveCursor(QTextCursor::Start);
    showEmpty(false);
    m_ctx.settings.setValue(kLastNoteKey, id);
    refreshTree();
    refreshPreview();
    refreshOutline();
    refreshContext();
    emit noteOpened(id);
    return true;
}

QString NotesPage::currentFolder() const
{
    if (QTreeWidgetItem* item = m_tree->currentItem()) {
        const QString folder = item->data(0, kFolderRole).toString();
        if (item->data(0, kIdRole).toLongLong() == 0)
            return folder;
    }
    return m_currentId > 0 ? m_current.folder : QString();
}

qint64 NotesPage::newNote(NoteType type, const QString& title)
{
    saveNow();
    auto created = m_ctx.notes.create(title.isEmpty() ? tr("Sans titre") : title, type, currentFolder());
    if (!created) {
        QMessageBox::warning(this, tr("Nouvelle note"), created.error().message);
        return 0;
    }
    openNote(created.value().id);
    m_title->setFocus();
    m_title->selectAll();
    return created.value().id;
}

qint64 NotesPage::followLink(const QString& target)
{
    if (target.trimmed().startsWith(u'@')) {
        // Citation : la fiche de la référence (ou la liste, si la clé est inconnue).
        saveNow();
        emit citationActivated(target.trimmed().mid(1).section(u'#', 0, 0).trimmed());
        return 0;
    }
    const NoteLinkTarget resolved = m_ctx.notes.resolveTarget(target);
    if (resolved.kind == NoteLinkTarget::Kind::Note) {
        openNote(resolved.id);
        return resolved.id;
    }
    if (resolved.kind == NoteLinkTarget::Kind::Document) {
        saveNow();
        emit openDocumentRequested(resolved.id);
        return 0;
    }
    // Lien non résolu : la note est créée à côté de celle qui la cite, comme dans Obsidian.
    const QString folder = m_currentId > 0 ? m_current.folder : QString();
    saveNow();
    QString title = target;
    static const QRegularExpression cut(QStringLiteral("[#^|]"));
    const qsizetype i = title.indexOf(cut);
    if (i >= 0)
        title = title.left(i);
    auto created = m_ctx.notes.create(title.trimmed(), NoteType::Personal, folder);
    if (!created) {
        QMessageBox::warning(this, tr("Nouvelle note"), created.error().message);
        return 0;
    }
    openNote(created.value().id);
    m_editor->setFocus();
    return created.value().id;
}

bool NotesPage::markSheetReviewed()
{
    if (m_currentId <= 0 || !NoteDrafts::isDraftSheet(m_editor->toPlainText()))
        return false;
    // Remplacement par le curseur : « Annuler » (Ctrl + Z) rétablit le brouillon.
    QTextCursor cursor(m_editor->document());
    cursor.select(QTextCursor::Document);
    cursor.insertText(NoteDrafts::markSheetReviewed(m_editor->toPlainText()));
    markDirty();
    const bool ok = saveNow();
    refreshPreview();
    return ok;
}

bool NotesPage::saveNow()
{
    m_saveTimer.stop();
    if (!m_dirty || m_currentId <= 0)
        return true;
    const QString body = m_editor->toPlainText();
    QString title = m_title->text().simplified();
    if (title.isEmpty())
        title = m_current.title;  // un titre effacé n'est pas enregistré : on garde le précédent
    auto saved = m_ctx.notes.save(m_currentId, title, body);
    if (!saved) {
        setSaveState(tr("Échec de l'enregistrement : %1").arg(saved.error().message), true);
        return false;
    }
    m_dirty = false;
    m_current = saved.value();
    m_draftBanner->setVisible(NoteDrafts::isDraftSheet(body));
    // Titre rendu unique (« … 2 ») ou vidé : affiché tel qu'enregistré, sauf pendant la frappe.
    if (!m_title->hasFocus() && m_title->text() != saved.value().title)
        m_title->setText(saved.value().title);
    setSaveState(tr("Enregistré"));
    return true;
}

void NotesPage::setMode(Mode mode)
{
    m_mode = mode;
    const int index = mode == Mode::Source ? 0 : mode == Mode::Split ? 1 : 2;
    m_modeControl->setCurrentIndex(index);
    m_split->widget(0)->setVisible(mode != Mode::Preview);
    m_preview->setVisible(mode != Mode::Source);
    m_ctx.settings.setValue(kModeKey, index);
    if (mode != Mode::Source)
        refreshPreview();
}

// ───────────────────────── Arborescence et tags ─────────────────────────

QList<qint64> NotesPage::visibleNoteIds() const
{
    QList<qint64> out;
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* item) {
        const qint64 id = item->data(0, kIdRole).toLongLong();
        if (id > 0)
            out << id;
        for (int i = 0; i < item->childCount(); ++i)
            walk(item->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        walk(m_tree->topLevelItem(i));
    return out;
}

NoteExporter& NotesPage::exporter()
{
    if (!m_exporter)
        m_exporter = new NoteExporter(m_ctx, this);
    return *m_exporter;
}

QList<qint64> NotesPage::notesInFolder(const QString& folder) const
{
    QList<qint64> ids;
    if (auto all = m_ctx.notes.list())
        for (const NoteRecord& n : all.value())  // triées par dossier puis titre
            if (n.folder == folder || n.folder.startsWith(folder + u'/'))
                ids << n.id;
    return ids;
}

bool NotesPage::exportNotes(const QList<qint64>& ids, const QString& heading, const QString& path)
{
    saveNow();
    return exporter().start(ids, heading, path);
}

void NotesPage::chooseVault()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Importer un coffre Obsidian ou un dossier Markdown"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (!dir.isEmpty())
        importVault(dir);
}

bool NotesPage::importVault(const QString& path)
{
    saveNow();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString name = QDir(path).dirName();
    auto scan = VaultImporter::scan(path);
    if (scan && !scan.value().studylabExport)
        scan = VaultImporter::scan(path, name);  // notes rangées dans un dossier au nom du coffre
    if (!scan) {
        QApplication::restoreOverrideCursor();
        m_importStatus->setText(scan.error().message);
        m_importStatus->setToolTip({});
        m_importStatus->show();
        return false;
    }
    const VaultScan& vault = scan.value();
    m_importing = true;
    auto report = m_ctx.notes.importNotes(vault.notes);
    m_importing = false;
    int references = 0;
    QStringList warnings = vault.warnings;
    for (const QString& bib : vault.bibFiles) {
        if (auto r = m_refs.importFile(bib)) {
            references += r.value().added;
            warnings << r.value().warnings;
        } else {
            warnings << QStringLiteral("%1 : %2").arg(QDir(path).relativeFilePath(bib), r.error().message);
        }
    }
    QApplication::restoreOverrideCursor();
    if (!report) {
        m_importStatus->setText(tr("Import impossible : %1").arg(report.error().message));
        m_importStatus->setToolTip({});
        m_importStatus->show();
        return false;
    }
    const NoteImportReport& r = report.value();
    warnings << r.warnings;
    QStringList parts;
    parts << tr("« %1 » : %2 importée(s)").arg(name, countLabel(r.imported, tr("note"), tr("notes")));
    if (r.duplicates > 0)
        parts << tr("%1 déjà présente(s)").arg(r.duplicates);
    if (r.renamed > 0)
        parts << tr("%1 renommée(s)").arg(r.renamed);
    if (r.images > 0)
        parts << countLabel(r.images, tr("image"), tr("images"));
    if (references > 0)
        parts << countLabel(references, tr("référence"), tr("références"));
    if (!warnings.isEmpty())
        parts << countLabel(warnings.size(), tr("avertissement"), tr("avertissements"));
    m_importStatus->setText(parts.join(QStringLiteral(", ")) + u'.');
    m_importStatus->setToolTip(warnings.mid(0, 30).join(u'\n'));
    m_importStatus->show();
    if (!r.noteIds.isEmpty()) {
        if (!vault.studylabExport)
            m_collapsedFolders.remove(NoteService::normalizeFolder(name));
        openNote(r.noteIds.first());
    }
    refreshTree();
    refreshTags();
    return true;
}

void NotesPage::refreshTree()
{
    auto all = m_ctx.notes.list();
    if (!all)
        return;
    const Palette& p = m_ctx.theme.palette();
    const QString filter = m_filter->text().trimmed();
    QList<NoteRecord> notes;
    for (const NoteRecord& n : all.value()) {
        if (!filter.isEmpty() && !n.title.contains(filter, Qt::CaseInsensitive) && !n.folder.contains(filter, Qt::CaseInsensitive))
            continue;
        if (!m_tagFilter.isEmpty() && !noteTags(n.body).contains(m_tagFilter, Qt::CaseInsensitive))
            continue;
        notes << n;
    }
    // Épinglées d'abord dans chaque dossier.
    std::stable_sort(notes.begin(), notes.end(), [](const NoteRecord& a, const NoteRecord& b) {
        if (const int c = a.folder.compare(b.folder, Qt::CaseInsensitive); c != 0)
            return c < 0;
        return a.pinned && !b.pinned;
    });

    const QSignalBlocker block(m_tree);
    m_tree->clear();
    QHash<QString, QTreeWidgetItem*> folders;
    const QIcon folderIcon = Icons::get(QStringLiteral("folder"), p.text2, 16);
    std::function<QTreeWidgetItem*(const QString&)> folderItem = [&](const QString& path) -> QTreeWidgetItem* {
        if (path.isEmpty())
            return nullptr;
        if (auto* existing = folders.value(path))
            return existing;
        const qsizetype slash = path.lastIndexOf(u'/');
        QTreeWidgetItem* parent = folderItem(slash < 0 ? QString() : path.left(slash));
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, slash < 0 ? path : path.mid(slash + 1));
        item->setIcon(0, folderIcon);
        item->setData(0, kIdRole, 0);
        item->setData(0, kFolderRole, path);
        item->setToolTip(0, path);
        folders.insert(path, item);
        return item;
    };
    // Dossiers d'abord (ordre alphabétique), puis les notes de la racine.
    for (const NoteRecord& n : std::as_const(notes))
        folderItem(n.folder);
    QTreeWidgetItem* selected = nullptr;
    for (const NoteRecord& n : std::as_const(notes)) {
        QTreeWidgetItem* parent = folderItem(n.folder);
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, n.title);
        item->setIcon(0, Icons::get(n.pinned ? QStringLiteral("pin") : noteTypeIcon(n.type), n.pinned ? p.primaryStrong : p.text2, 16));
        item->setData(0, kIdRole, n.id);
        item->setData(0, kFolderRole, n.folder);
        item->setToolTip(0, QStringLiteral("%1 · %2").arg(n.title, noteTypeLabel(n.type)));
        if (n.id == m_currentId)
            selected = item;
    }
    for (auto it = folders.cbegin(); it != folders.cend(); ++it)
        it.value()->setExpanded(!filter.isEmpty() || !m_tagFilter.isEmpty() || !m_collapsedFolders.contains(it.key()));
    // Les dossiers avant les notes à chaque niveau.
    auto sortLevel = [](QTreeWidgetItem* parent, QTreeWidget* tree) {
        QList<QTreeWidgetItem*> items;
        if (parent)
            items = parent->takeChildren();
        else
            while (tree->topLevelItemCount() > 0)
                items << tree->takeTopLevelItem(0);
        std::stable_sort(items.begin(), items.end(), [](QTreeWidgetItem* a, QTreeWidgetItem* b) {
            const bool fa = a->data(0, kIdRole).toLongLong() == 0;
            const bool fb = b->data(0, kIdRole).toLongLong() == 0;
            if (fa != fb)
                return fa;
            return fa && QString::localeAwareCompare(a->text(0), b->text(0)) < 0;
        });
        if (parent)
            parent->addChildren(items);
        else
            tree->addTopLevelItems(items);
    };
    sortLevel(nullptr, m_tree);
    for (auto* folder : std::as_const(folders))
        sortLevel(folder, m_tree);
    // takeChildren / takeTopLevelItem perdent l'état déplié : on le réapplique.
    for (auto it = folders.cbegin(); it != folders.cend(); ++it)
        it.value()->setExpanded(!filter.isEmpty() || !m_tagFilter.isEmpty() || !m_collapsedFolders.contains(it.key()));
    if (selected) {
        m_tree->setCurrentItem(selected);
        m_tree->scrollToItem(selected);
    }
}

void NotesPage::refreshTags()
{
    for (QToolButton* b : std::as_const(m_tagButtons))
        b->deleteLater();
    m_tagButtons.clear();
    auto all = m_ctx.notes.list();
    QMap<QString, int> counts;  // ordre alphabétique, sans casse
    QHash<QString, QString> display;
    if (all) {
        for (const NoteRecord& n : all.value())
            for (const QString& t : noteTags(n.body)) {
                const QString key = t.toLower();
                counts[key] += 1;
                display.insert(key, t);
            }
    }
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        auto* b = new QToolButton(m_tagHost);
        b->setObjectName(QStringLiteral("FilterChip"));
        b->setText(QStringLiteral("#") + display.value(it.key()));
        b->setToolTip(tr("%1 · %2").arg(display.value(it.key()), countLabel(it.value(), tr("note"), tr("notes"))));
        b->setCheckable(true);
        b->setChecked(it.key() == m_tagFilter.toLower() && !m_tagFilter.isEmpty());
        b->setCursor(Qt::PointingHandCursor);
        const QString tag = display.value(it.key());
        connect(b, &QToolButton::clicked, this, [this, tag](bool on) { setTagFilter(on ? tag : QString()); });
        m_tagFlow->addWidget(b);
        m_tagButtons << b;
    }
    m_tagFlow->invalidate();  // QLayout::addWidget n'invalide pas la disposition
    m_tagHost->updateGeometry();
    m_tagsTitle->setVisible(!counts.isEmpty());
    m_tagHost->setVisible(!counts.isEmpty());
}

void NotesPage::setTagFilter(const QString& tag)
{
    m_tagFilter = tag;
    for (QToolButton* b : std::as_const(m_tagButtons))
        b->setChecked(!tag.isEmpty() && b->text().mid(1).compare(tag, Qt::CaseInsensitive) == 0);
    refreshTree();
}

void NotesPage::requestCard(qint64 noteId)
{
    auto note = m_ctx.notes.find(noteId);
    if (!note)
        return;
    // Recto : titre de la note ; verso : la sélection de l'éditeur quand c'est la note ouverte.
    QString back;
    if (noteId == m_currentId && m_editor->textCursor().hasSelection())
        back = m_editor->textCursor().selectedText().replace(QChar::ParagraphSeparator, u'\n').trimmed();
    emit cardRequested(noteId, note.value().title, back);
}

void NotesPage::showTreeMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    QMenu menu(this);
    if (!item) {
        menu.addAction(tr("Nouvelle note"), this, [this] {
            m_tree->setCurrentItem(nullptr);
            newNote(NoteType::Personal);
        });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
        return;
    }
    const qint64 id = item->data(0, kIdRole).toLongLong();
    const QString folder = item->data(0, kFolderRole).toString();
    if (id == 0) {
        menu.addAction(tr("Nouvelle note dans ce dossier"), this, [this, item] {
            m_tree->setCurrentItem(item);
            newNote(NoteType::Personal);
        });
        menu.addAction(tr("Renommer le dossier…"), this, [this, folder] { renameFolder(folder); });
        menu.addSeparator();
        const QList<qint64> ids = notesInFolder(folder);
        const QString name = folder.section(u'/', -1);
        menu.addAction(Icons::get(QStringLiteral("download"), m_ctx.theme.palette().text2, 16), tr("Exporter le dossier en PDF…"),
                       this, [this, ids, name] { exporter().exportWithDialog(this, ids, name, name, NoteExporter::Format::Pdf); })
            ->setEnabled(!ids.isEmpty());
        menu.addAction(tr("Exporter le dossier en HTML…"), this,
                       [this, ids, name] { exporter().exportWithDialog(this, ids, name, name, NoteExporter::Format::Html); })
            ->setEnabled(!ids.isEmpty());
    } else {
        auto note = m_ctx.notes.find(id);
        if (!note)
            return;
        const bool pinned = note.value().pinned;
        menu.addAction(tr("Ouvrir"), this, [this, id] { openNote(id); });
        menu.addAction(pinned ? tr("Désépingler") : tr("Épingler en tête du dossier"), this,
                       [this, id, pinned] {
                           if (auto r = m_ctx.notes.setPinned(id, !pinned); !r)
                               QMessageBox::warning(this, tr("Épingler"), r.error().message);
                       });
        menu.addAction(tr("Déplacer vers un dossier…"), this, [this, id] { moveNote(id); });
        addOrganizeMenus(&menu, id);
        menu.addAction(Icons::get(QStringLiteral("cards"), m_ctx.theme.palette().text2, 16), tr("Créer une carte…"), this,
                       [this, id] { requestCard(id); });
        menu.addSeparator();
        const QString title = note.value().title;
        menu.addAction(Icons::get(QStringLiteral("download"), m_ctx.theme.palette().text2, 16), tr("Exporter en PDF…"), this,
                       [this, id, title] {
                           saveNow();
                           exporter().exportWithDialog(this, {id}, {}, title, NoteExporter::Format::Pdf);
                       });
        menu.addAction(tr("Exporter en HTML…"), this, [this, id, title] {
            saveNow();
            exporter().exportWithDialog(this, {id}, {}, title, NoteExporter::Format::Html);
        });
        menu.addSeparator();
        menu.addAction(Icons::get(QStringLiteral("trash"), m_ctx.theme.palette().danger, 16), tr("Mettre à la corbeille"),
                       this, [this, id] { deleteNote(id); });
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void NotesPage::addOrganizeMenus(QMenu* menu, qint64 noteId)
{
    const Palette& pal = m_ctx.theme.palette();
    QMenu* subjects = menu->addMenu(Icons::get(QStringLiteral("folder"), pal.text2, 16), tr("Matières"));
    subjects->setObjectName(QStringLiteral("NoteSubjectsMenu"));
    auto all = m_ctx.organize.subjects();
    auto mine = m_ctx.organize.subjectsOf(OrganizeService::Entity::Note, noteId);
    int n = 0;
    for (const SubjectRecord& s : all ? all.value() : QList<SubjectRecord>{}) {
        if (s.archived)
            continue;
        ++n;
        QAction* a = subjects->addAction(colorDot(pal, s.color, 12), m_ctx.organize.subjectPath(s.id, all.value()));
        a->setCheckable(true);
        a->setChecked(mine && mine.value().contains(s.id));
        const qint64 sid = s.id;
        connect(a, &QAction::toggled, this, [this, sid, noteId](bool on) {
            if (on)
                (void)m_ctx.organize.attach(sid, OrganizeService::Entity::Note, noteId);
            else
                (void)m_ctx.organize.detach(sid, OrganizeService::Entity::Note, noteId);
        });
    }
    if (n == 0)
        subjects->addAction(tr("Aucune matière : créez-en dans « Matières »"))->setEnabled(false);

    QMenu* collections = menu->addMenu(Icons::get(QStringLiteral("layers"), pal.text2, 16), tr("Collections"));
    collections->setObjectName(QStringLiteral("NoteCollectionsMenu"));
    auto cols = m_ctx.organize.collections();
    auto in = m_ctx.organize.collectionsOf(OrganizeService::Entity::Note, noteId);
    n = 0;
    for (const CollectionRecord& c : cols ? cols.value() : QList<CollectionRecord>{}) {
        if (c.kind == CollectionKind::Smart)
            continue;
        ++n;
        QAction* a = collections->addAction(Icons::get(collectionKindIcon(c.kind), pal.text2, 14), c.name);
        a->setCheckable(true);
        a->setChecked(in && in.value().contains(c.id));
        const qint64 cid = c.id;
        connect(a, &QAction::toggled, this, [this, cid, noteId](bool on) {
            if (on)
                (void)m_ctx.organize.addToCollection(cid, OrganizeService::Entity::Note, noteId);
            else
                (void)m_ctx.organize.removeFromCollection(cid, OrganizeService::Entity::Note, noteId);
        });
    }
    if (n == 0)
        collections->addAction(tr("Aucune collection : créez-en dans « Collections »"))->setEnabled(false);
}

void NotesPage::moveNote(qint64 id)
{
    auto note = m_ctx.notes.find(id);
    auto all = m_ctx.notes.list();
    if (!note || !all)
        return;
    QStringList folders{QString()};
    for (const NoteRecord& n : all.value())
        if (!n.folder.isEmpty() && !folders.contains(n.folder))
            folders << n.folder;
    bool ok = false;
    const QString target = QInputDialog::getItem(this, tr("Déplacer la note"),
                                                 tr("Dossier (« A/B » pour un sous-dossier, vide pour la racine) :"),
                                                 folders, int(folders.indexOf(note.value().folder)), true, &ok);
    if (!ok)
        return;
    saveNow();
    if (auto r = m_ctx.notes.setFolder(id, target); !r)
        QMessageBox::warning(this, tr("Déplacer la note"), r.error().message);
}

void NotesPage::renameFolder(const QString& folder)
{
    bool ok = false;
    const QString target = QInputDialog::getText(this, tr("Renommer le dossier"),
                                                 tr("Nouveau chemin (« A/B » pour un sous-dossier) :"), QLineEdit::Normal,
                                                 folder, &ok);
    if (!ok || target.trimmed().isEmpty())
        return;
    saveNow();
    if (auto r = m_ctx.notes.renameFolder(folder, target); !r)
        QMessageBox::warning(this, tr("Renommer le dossier"), r.error().message);
}

void NotesPage::deleteNote(qint64 id)
{
    auto note = m_ctx.notes.find(id);
    if (!note)
        return;
    if (id == m_currentId) {
        saveNow();
        m_dirty = false;
    }
    if (auto r = m_ctx.notes.remove(id); !r) {
        QMessageBox::warning(this, tr("Supprimer la note"), r.error().message);
        return;
    }
    m_deletedId = id;
    m_undoText->setText(tr("« %1 » mise à la corbeille.").arg(note.value().title));
    m_undoBar->show();
    m_undoTimer.start();
    const QList<qint64> ids = visibleNoteIds();
    if (m_currentId == 0 && !ids.isEmpty())
        openNote(ids.first());
}

void NotesPage::undoDelete()
{
    m_undoBar->hide();
    m_undoTimer.stop();
    if (m_deletedId <= 0)
        return;
    const qint64 id = std::exchange(m_deletedId, 0);
    if (m_ctx.notes.restore(id))
        openNote(id);
}

// ───────────────────────── Aperçu et contexte ─────────────────────────

QString NotesPage::imageData(const QString& relPath) const
{
    if (auto it = m_imageCache.constFind(relPath); it != m_imageCache.cend())
        return it.value();
    const QString uri = attachmentDataUri(m_ctx.notes.attachmentsDir(), relPath);
    m_imageCache.insert(relPath, uri);
    return uri;
}

QJsonObject NotesPage::previewPayload() const
{
    const QString title = m_title->text().simplified();
    return notePreviewPayload(m_ctx.notes, m_refs, title.isEmpty() ? m_current.title : title, m_current.folder,
                              m_editor->toPlainText(), [this](const QString& rel) { return imageData(rel); });
}

void NotesPage::refreshPreview()
{
    if (m_currentId <= 0 || m_mode == Mode::Source)
        return;
    m_preview->setNote(previewPayload(), tr("Note vide."));
}

void NotesPage::refreshOutline()
{
    for (QPushButton* b : std::as_const(m_outlineButtons))
        b->deleteLater();
    m_outlineButtons.clear();
    const QList<OutlineEntry> outline = noteOutline(m_editor->toPlainText());
    int minLevel = 6;
    for (const auto& e : outline)
        minLevel = std::min(minLevel, e.level);
    for (const auto& e : outline) {
        auto* b = new QPushButton(e.text, m_context);
        b->setObjectName(QStringLiteral("OutlineItem"));
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QStringLiteral("padding-left: %1px;").arg(m_ctx.theme.px(4 + 12 * (e.level - minLevel))));
        b->setToolTip(e.text);
        const int line = e.line;
        connect(b, &QPushButton::clicked, this, [this, line] { jumpToLine(line); });
        m_outlineList->addWidget(b);
        m_outlineButtons << b;
    }
    m_outlineEmpty->setVisible(outline.isEmpty());
}

void NotesPage::jumpToLine(int line)
{
    if (m_mode != Mode::Preview) {
        m_editor->goToLine(line);
        m_editor->setFocus();
    }
    if (m_mode != Mode::Source)
        m_preview->scrollToLine(line);
}

void NotesPage::refreshContext()
{
    clearLayout(m_backlinkList);
    clearLayout(m_unresolvedList);
    clearLayout(m_sourceList);
    m_backlinkButtons.clear();
    m_createButtons.clear();
    m_sourceButtons.clear();
    m_sourcesSection->setVisible(false);
    if (m_currentId <= 0)
        return;

    auto sources = m_ctx.notes.sources(m_currentId);
    if (sources && !sources.value().isEmpty()) {
        for (const NoteSource& src : sources.value()) {
            QString title = src.documentTitle.isEmpty() ? tr("Document") : src.documentTitle;
            if (!src.pageLabel.isEmpty())
                title += tr(" · p. %1").arg(src.pageLabel);
            else if (src.unitIndex >= 0 && src.format == QLatin1String("pdf"))
                title += tr(" · p. %1").arg(src.unitIndex + 1);
            QString detail = src.quote.simplified();
            if (detail.size() > 140)
                detail = detail.left(139) + QChar(0x2026);
            if (src.missing)
                detail = tr("Source supprimée depuis.");
            auto* row = new ContextRow(title, detail, m_context);
            row->setEnabled(!src.missing);
            row->setToolTip(src.missing ? detail : tr("Ouvrir le passage dans le lecteur"));
            const QString kind = src.kind;
            const qint64 doc = src.documentId;
            const qint64 item = src.id;
            connect(row, &QPushButton::clicked, this, [this, kind, doc, item] {
                saveNow();
                if (kind == QLatin1String("document"))
                    emit openDocumentRequested(doc);
                else
                    emit openItemRequested(doc, kind == QLatin1String("comment"), item);
            });
            m_sourceList->addWidget(row);
            m_sourceButtons << row;
        }
        m_sourcesSection->setVisible(true);
    }

    auto back = m_ctx.notes.backlinks(m_currentId);
    const QList<NoteBacklink> backlinks = back ? back.value() : QList<NoteBacklink>{};
    m_backlinkCount->setText(QString::number(backlinks.size()));
    for (const NoteBacklink& b : backlinks) {
        auto* row = new ContextRow(b.title, b.snippet, m_context);
        const qint64 id = b.noteId;
        connect(row, &QPushButton::clicked, this, [this, id] { openNote(id); });
        m_backlinkList->addWidget(row);
        m_backlinkButtons << row;
    }
    m_backlinkEmpty->setVisible(backlinks.isEmpty());

    auto links = m_ctx.notes.links(m_currentId);
    int unresolved = 0;
    if (links) {
        for (const NoteLink& l : links.value()) {
            if (l.resolved())
                continue;
            ++unresolved;
            auto* row = new QWidget(m_context);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(6);
            auto* label = makeLabel(QStringLiteral("[[%1]]").arg(l.target), "muted", row);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            label->setToolTip(l.target);
            h->addWidget(label, 1);
            auto* create = new QPushButton(tr("créer"), row);
            create->setObjectName(QStringLiteral("CreateLinkedNote"));
            create->setProperty("variant", QStringLiteral("ghost"));
            create->setCursor(Qt::PointingHandCursor);
            create->setAccessibleName(tr("Créer la note « %1 »").arg(l.target));
            const QString target = l.target;
            connect(create, &QPushButton::clicked, this, [this, target] { followLink(target); });
            h->addWidget(create);
            m_unresolvedList->addWidget(row);
            m_createButtons << create;
        }
    }
    m_unresolvedTitle->setVisible(unresolved > 0);
}

// ───────────────────────── Images, événements ─────────────────────────

bool NotesPage::insertImage(const QByteArray& bytes, const QString& extension)
{
    if (m_currentId <= 0)
        return false;
    auto rel = m_ctx.notes.addImage(m_currentId, bytes, extension);
    if (!rel) {
        setSaveState(rel.error().message, true);
        return false;
    }
    QTextCursor c = m_editor->textCursor();
    const bool lineStart = c.positionInBlock() == 0;
    c.insertText(QStringLiteral("%1![](%2)\n").arg(lineStart ? QString() : QStringLiteral("\n"), rel.value()));
    m_editor->setTextCursor(c);
    return true;
}

bool NotesPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_editor && event->type() == QEvent::FocusOut)
        saveNow();
    return QWidget::eventFilter(watched, event);
}

void NotesPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Des documents ont pu être ajoutés, renommés ou retirés de la bibliothèque depuis.
    auto changed = m_ctx.notes.refreshLinks();
    if (changed && changed.value() && m_currentId > 0) {
        refreshPreview();
        refreshContext();
    }
    if (!m_splitRestored && m_split->width() > 0) {
        m_splitRestored = true;
        m_split->setSizes({m_split->width() / 2, m_split->width() - m_split->width() / 2});
    }
}

void NotesPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!m_splitRestored && isVisible() && m_split->width() > 0) {
        m_splitRestored = true;
        m_split->setSizes({m_split->width() / 2, m_split->width() - m_split->width() / 2});
    }
}

void NotesPage::hideEvent(QHideEvent* event)
{
    saveNow();
    QWidget::hideEvent(event);
}

}  // namespace studylab::ui
