#include "ui/shell/MainWindow.h"

#include "core/AppLink.h"

#include "services/library/LibraryService.h"
#include "services/search/OcrIndexer.h"
#include "ui/reader/PdfReaderTab.h"
#include "services/search/TextIndexer.h"
#include "services/settings/SettingsService.h"
#include "services/study/StudyService.h"
#include "ui/components/FadeStackedWidget.h"
#include "ui/graph/GraphPage.h"
#include "ui/library/LibraryPage.h"
#include "ui/notes/NotesPage.h"
#include "ui/organize/CollectionsPage.h"
#include "ui/organize/SubjectsPage.h"
#include "ui/reader/ReaderPage.h"
#include "ui/pages/HomePage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/references/ReferencesPage.h"
#include "ui/study/CardEditorDialog.h"
#include "ui/study/RevisionsPage.h"

#include <QLineEdit>
#include "ui/shell/SearchPalette.h"
#include "ui/shell/Sidebar.h"
#include "ui/theme/ThemeManager.h"

#include <QCloseEvent>
#include <QMessageBox>
#include <QShortcut>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QHBoxLayout>
#include <QSettings>
#include <QTimer>

namespace studylab::ui {

namespace {
const QString kLastPageKey = QStringLiteral("ui/lastPage");
}

MainWindow::MainWindow(AppContext& ctx, QWidget* parent) : QMainWindow(parent), m_ctx(ctx)
{
    setWindowTitle(QStringLiteral("StudyLab"));
    setMinimumSize(960, 600);

    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("AppRoot"));
    auto* layout = new QHBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Seuls les espaces terminés apparaissent dans la navigation.
    m_sidebar = new Sidebar(ctx.theme,
                            {
                                {QStringLiteral("home"), QStringLiteral("home"), tr("Accueil")},
                                {QStringLiteral("library"), QStringLiteral("library"), tr("Bibliothèque")},
                                {QStringLiteral("reader"), QStringLiteral("book"), tr("Lecture")},
                                {QStringLiteral("notes"), QStringLiteral("note"), tr("Mes notes")},
                                {QStringLiteral("subjects"), QStringLiteral("folder"), tr("Matières")},
                                {QStringLiteral("collections"), QStringLiteral("layers"), tr("Collections")},
                                {QStringLiteral("graph"), QStringLiteral("graph"), tr("Graphe")},
                                {QStringLiteral("revisions"), QStringLiteral("cards"), tr("Révisions")},
                                {QStringLiteral("references"), QStringLiteral("quote"), tr("Références")},
                                {QStringLiteral("settings"), QStringLiteral("settings"), tr("Paramètres")},
                            },
                            root);
    m_stack = new FadeStackedWidget(root);
    m_stack->setAnimationsEnabled(ctx.settings.animationsEnabled());

    layout->addWidget(m_sidebar);
    layout->addWidget(m_stack, 1);
    setCentralWidget(root);
    root->setFocusPolicy(Qt::ClickFocus);
    root->setFocus();

    auto* home = new HomePage(ctx, m_stack);
    addPage(QStringLiteral("home"), home);
    connect(home, &HomePage::navigateRequested, this, &MainWindow::navigateTo);
    auto* library = new LibraryPage(ctx, m_stack);
    addPage(QStringLiteral("library"), library);
    auto* reader = new ReaderPage(ctx, m_stack);
    addPage(QStringLiteral("reader"), reader);
    connect(library, &LibraryPage::openDocumentRequested, this, [this, reader](qint64 id) {
        navigateTo(QStringLiteral("reader"));
        reader->openDocument(id);
    });
    connect(home, &HomePage::openDocumentRequested, this, [this, reader](qint64 id) {
        navigateTo(QStringLiteral("reader"));
        reader->openDocument(id);
    });
    connect(home, &HomePage::openItemRequested, this, [this, reader](qint64 doc, bool comment, qint64 item) {
        navigateTo(QStringLiteral("reader"));
        reader->openItem(doc, comment, item);
    });
    connect(reader, &ReaderPage::libraryRequested, this, [this] { navigateTo(QStringLiteral("library")); });
    connect(reader, &ReaderPage::focusModeChanged, this, [this](bool on) { m_sidebar->setVisible(!on); });
    connect(reader, &ReaderPage::fullScreenRequested, this, &MainWindow::toggleFullScreen);
    auto* fullScreen = new QShortcut(QKeySequence(Qt::Key_F11), this, this, &MainWindow::toggleFullScreen);
    fullScreen->setContext(Qt::WindowShortcut);
    auto* notes = new NotesPage(ctx, m_stack);
    addPage(QStringLiteral("notes"), notes);
    connect(reader, &ReaderPage::noteCreated, this, [this, notes](qint64 id) {
        navigateTo(QStringLiteral("notes"));
        notes->openNote(id);
    });
    connect(notes, &NotesPage::openDocumentRequested, this, [this, reader](qint64 id) {
        navigateTo(QStringLiteral("reader"));
        reader->openDocument(id);
    });
    connect(notes, &NotesPage::openItemRequested, this, [this, reader](qint64 doc, bool comment, qint64 item) {
        navigateTo(QStringLiteral("reader"));
        reader->openItem(doc, comment, item);
    });
    auto* subjects = new SubjectsPage(ctx, m_stack);
    addPage(QStringLiteral("subjects"), subjects);
    auto* collections = new CollectionsPage(ctx, m_stack);
    addPage(QStringLiteral("collections"), collections);
    const auto openDoc = [this, reader](qint64 id) {
        navigateTo(QStringLiteral("reader"));
        reader->openDocument(id);
    };
    const auto openNote = [this, notes](qint64 id) {
        navigateTo(QStringLiteral("notes"));
        notes->openNote(id);
    };
    connect(subjects, &SubjectsPage::openDocumentRequested, this, openDoc);
    connect(subjects, &SubjectsPage::openNoteRequested, this, openNote);
    connect(collections, &CollectionsPage::openDocumentRequested, this, openDoc);
    connect(collections, &CollectionsPage::openNoteRequested, this, openNote);
    auto* graph = new GraphPage(ctx, m_stack);
    addPage(QStringLiteral("graph"), graph);
    connect(graph, &GraphPage::openDocumentRequested, this, openDoc);
    connect(graph, &GraphPage::openNoteRequested, this, openNote);
    connect(graph, &GraphPage::createNoteRequested, this, [this, notes](const QString& title) {
        navigateTo(QStringLiteral("notes"));
        notes->followLink(title);
    });
    connect(graph, &GraphPage::notesRequested, this, [this] { navigateTo(QStringLiteral("notes")); });
    // Cartes mémoire et répétition espacée (V2-1)
    m_study = new StudyService(ctx.db, this);
    auto* revisions = new RevisionsPage(ctx, *m_study, m_stack);
    addPage(QStringLiteral("revisions"), revisions);
    connect(revisions, &RevisionsPage::openDocumentRequested, this, openDoc);
    connect(revisions, &RevisionsPage::openNoteRequested, this, openNote);
    connect(revisions, &RevisionsPage::openItemRequested, this, [this, reader](qint64 doc, bool comment, qint64 item) {
        navigateTo(QStringLiteral("reader"));
        reader->openItem(doc, comment, item);
    });
    connect(reader, &ReaderPage::cardRequested, this, [this](qint64 id, const QString& front, const QString& back) {
        createCard(int(CardSource::Annotation), id, front, back);
    });
    connect(notes, &NotesPage::cardRequested, this, [this](qint64 id, const QString& front, const QString& back) {
        createCard(int(CardSource::Note), id, front, back);
    });
    connect(m_study, &StudyService::changed, this, &MainWindow::refreshStudyBadge);
    // Le jour de révision change à 4 h : la pastille est recalculée régulièrement.
    auto* badgeTimer = new QTimer(this);
    badgeTimer->setInterval(5 * 60 * 1000);
    connect(badgeTimer, &QTimer::timeout, this, &MainWindow::refreshStudyBadge);
    badgeTimer->start();
    refreshStudyBadge();
    auto* references = new ReferencesPage(ctx, m_stack);
    addPage(QStringLiteral("references"), references);
    connect(references, &ReferencesPage::openDocumentRequested, this, openDoc);
    connect(references, &ReferencesPage::openNoteRequested, this, openNote);
    connect(notes, &NotesPage::citationActivated, this, [this, references](const QString& key) {
        navigateTo(QStringLiteral("references"));
        if (!references->showCitekey(key))
            references->filterField()->setText(key);
    });
    connect(library, &LibraryPage::referenceRequested, this, [this, references](qint64 id) {
        navigateTo(QStringLiteral("references"));
        references->showForDocument(id);
    });
    connect(notes, &NotesPage::appLinkActivated, this, [this](const QString& link) {
        auto opened = openLink(link);
        if (!opened)
            QMessageBox::warning(this, tr("Ouvrir le passage"), opened.error().message);
    });
    // Texte intégral : extrait en tâche de fond, peu après le démarrage et à chaque import.
    m_indexer = new TextIndexer(ctx.db, ctx.paths.libraryDir(), this);
    connect(&ctx.library, &LibraryService::documentsChanged, m_indexer, [this] { m_indexer->schedule(); });
    m_indexer->schedule(3000);
    // OCR facultatif (Tesseract chargé à l'exécution) : pages numérisées lues après l'extraction du texte natif.
    m_ocr = new OcrIndexer(ctx.db, ctx.paths.libraryDir(), this);
    const auto applyOcrSettings = [this, &ctx] {
        m_ocr->setLanguages(SettingsPage::ocrLanguages(ctx.settings));
        m_ocr->setEnabled(ctx.settings.value(SettingKeys::OcrEnabled, true).toBool());
        m_ocr->schedule();
    };
    applyOcrSettings();
    connect(&ctx.settings, &SettingsService::changed, this, [applyOcrSettings](const QString& key) {
        if (key == SettingKeys::OcrEnabled || key == SettingKeys::OcrLanguages)
            applyOcrSettings();
    });
    connect(m_indexer, &TextIndexer::finished, m_ocr, [this] { m_ocr->schedule(); });
    connect(m_ocr, &OcrIndexer::pageRecognized, this, [reader](qint64 documentId) {
        if (auto* tab = qobject_cast<PdfReaderTab*>(reader->tabFor(documentId)))
            tab->reloadOcrText();
    });
    auto* settingsPage = new SettingsPage(ctx, m_stack);
    settingsPage->setTextIndexer(m_indexer);
    settingsPage->setOcrIndexer(m_ocr);
    addPage(QStringLiteral("settings"), settingsPage);

    // Rechercher partout
    m_search = new SearchPalette(ctx.search, ctx.theme, this);
    m_search->setTextIndexer(m_indexer);
    connect(m_search, &SearchPalette::hitActivated, this, [this, reader, notes](const SearchHit& hit) {
        if (hit.kind == SearchHit::Kind::Note) {
            navigateTo(QStringLiteral("notes"));
            notes->openNote(hit.itemId);
            return;
        }
        navigateTo(QStringLiteral("reader"));
        if (hit.kind == SearchHit::Kind::Passage)
            reader->openPassage(hit.documentId, hit.unit, SearchPalette::highlightTerm(m_search->results().query));
        else if (hit.kind == SearchHit::Kind::Document)
            reader->openDocument(hit.documentId);
        else
            reader->openItem(hit.documentId, hit.kind == SearchHit::Kind::Comment, hit.itemId);
    });
    connect(m_sidebar, &Sidebar::searchRequested, this, &MainWindow::showSearch);
    auto* find = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this, this, &MainWindow::showSearch);
    find->setContext(Qt::WindowShortcut);

    connect(m_sidebar, &Sidebar::navigated, this, &MainWindow::navigateTo);
    connect(&ctx.settings, &SettingsService::changed, this, [this](const QString& key) {
        if (key == SettingKeys::Animations)
            m_stack->setAnimationsEnabled(m_ctx.settings.animationsEnabled());
    });

    // Glisser-déposer de fichiers ou de dossiers n'importe où dans la fenêtre → import.
    setAcceptDrops(true);

    QSettings qs;
    if (!restoreGeometry(qs.value(QStringLiteral("ui/geometry")).toByteArray()))
        resize(1280, 820);
    const QString last = ctx.settings.value(kLastPageKey, QStringLiteral("home")).toString();
    m_stack->setCurrentWidget(m_pages.value(last, m_pages.value(QStringLiteral("home"))));
    m_sidebar->setCurrent(m_pages.contains(last) ? last : QStringLiteral("home"));
}

void MainWindow::addPage(const QString& id, QWidget* page)
{
    m_pages.insert(id, page);
    m_stack->addWidget(page);
}

void MainWindow::navigateTo(const QString& pageId)
{
    QWidget* page = m_pages.value(pageId);
    if (!page)
        return;
    m_sidebar->setCurrent(pageId);
    m_stack->fadeTo(m_stack->indexOf(page));
    m_ctx.settings.setValue(kLastPageKey, pageId);
}

void MainWindow::refreshStudyBadge() { m_sidebar->setBadge(QStringLiteral("revisions"), m_study->dueTotal()); }

CardEditorDialog* MainWindow::createCard(int sourceType, qint64 sourceId, const QString& front, const QString& back)
{
    auto* dlg = new CardEditorDialog(m_ctx, *m_study, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setContent(front, back);
    Card probe;
    probe.sourceType = CardSource(sourceType);
    probe.sourceId = sourceId;
    if (auto src = m_study->source(probe))
        dlg->setSource(probe.sourceType, sourceId, src.value().label);
    if (auto* revisions = qobject_cast<RevisionsPage*>(m_pages.value(QStringLiteral("revisions")));
        revisions && revisions->currentDeckId() > 0)
        dlg->setDeck(revisions->currentDeckId());
    dlg->open();
    return dlg;
}

QString MainWindow::currentPage() const { return m_pages.key(m_stack->currentWidget()); }

namespace {
QStringList localPaths(const QMimeData* mime)
{
    QStringList out;
    if (!mime || !mime->hasUrls())
        return out;
    for (const QUrl& url : mime->urls())
        if (url.isLocalFile())
            out << url.toLocalFile();
    return out;
}
}  // namespace

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!localPaths(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QStringList paths = localPaths(event->mimeData());
    if (paths.isEmpty())
        return;
    event->acceptProposedAction();
    navigateTo(QStringLiteral("library"));
    if (auto* library = qobject_cast<LibraryPage*>(m_pages.value(QStringLiteral("library"))))
        library->importPaths(paths);
}

void MainWindow::showSearch() { m_search->popup(); }

void MainWindow::toggleFullScreen()
{
    setWindowState(windowState() ^ Qt::WindowFullScreen);
}

MainWindow::~MainWindow()
{
    // Avant la destruction des pages : le texte en attente est enregistré pendant que tout est encore debout
    // (sinon la perte du focus à la fermeture l'enregistrerait au milieu de la destruction).
    if (auto* notes = qobject_cast<NotesPage*>(m_pages.value(QStringLiteral("notes"))))
        notes->saveNow();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (auto* reader = qobject_cast<ReaderPage*>(m_pages.value(QStringLiteral("reader"))))
        reader->saveSession();
    if (auto* notes = qobject_cast<NotesPage*>(m_pages.value(QStringLiteral("notes"))))
        notes->saveNow();
    QSettings qs;
    qs.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    QMainWindow::closeEvent(event);
}

Result<void> MainWindow::openLink(const QString& link)
{
    const auto parsed = AppLink::parse(link);
    if (!parsed)
        return Error::make(Error::Code::InvalidArgument, tr("Lien StudyLab non reconnu : %1").arg(link));
    auto target = m_ctx.search.resolve(*parsed);
    if (!target)
        return target.error();
    auto* reader = qobject_cast<ReaderPage*>(m_pages.value(QStringLiteral("reader")));
    if (!reader)
        return Error::make(Error::Code::Generic, tr("Lecteur indisponible."));
    navigateTo(QStringLiteral("reader"));
    if (target.value().itemId > 0)
        reader->openItem(target.value().documentId, target.value().comment, target.value().itemId);
    else
        reader->openDocument(target.value().documentId);
    return {};
}

void MainWindow::handleInstanceMessage(const QString& message)
{
    bringToFront();
    if (!AppLink::looksLikeLink(message))
        return;
    if (auto r = openLink(message); !r)
        QMessageBox::information(this, QStringLiteral("StudyLab"), r.error().message);
}

void MainWindow::bringToFront()
{
    if (isMinimized())
        setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    show();
    raise();
    activateWindow();
}

}  // namespace studylab::ui
