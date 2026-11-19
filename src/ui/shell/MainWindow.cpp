#include "ui/shell/MainWindow.h"

#include "core/AppLink.h"

#include "services/settings/SettingsService.h"
#include "ui/components/FadeStackedWidget.h"
#include "ui/library/LibraryPage.h"
#include "ui/notes/NotesPage.h"
#include "ui/reader/ReaderPage.h"
#include "ui/pages/HomePage.h"
#include "ui/pages/SettingsPage.h"
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
    addPage(QStringLiteral("notes"), new NotesPage(ctx, m_stack));
    addPage(QStringLiteral("settings"), new SettingsPage(ctx, m_stack));

    // Rechercher partout
    m_search = new SearchPalette(ctx.search, ctx.theme, this);
    connect(m_search, &SearchPalette::hitActivated, this, [this, reader](const SearchHit& hit) {
        navigateTo(QStringLiteral("reader"));
        if (hit.kind == SearchHit::Kind::Document)
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
