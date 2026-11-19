#include "ui/shell/MainWindow.h"

#include "services/settings/SettingsService.h"
#include "ui/components/FadeStackedWidget.h"
#include "ui/pages/HomePage.h"
#include "ui/pages/SettingsPage.h"
#include "ui/shell/Sidebar.h"
#include "ui/theme/ThemeManager.h"

#include <QCloseEvent>
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

    addPage(QStringLiteral("home"), new HomePage(ctx, m_stack));
    addPage(QStringLiteral("settings"), new SettingsPage(ctx, m_stack));

    connect(m_sidebar, &Sidebar::navigated, this, &MainWindow::navigateTo);
    connect(&ctx.settings, &SettingsService::changed, this, [this](const QString& key) {
        if (key == SettingKeys::Animations)
            m_stack->setAnimationsEnabled(m_ctx.settings.animationsEnabled());
    });

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

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings qs;
    qs.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    QMainWindow::closeEvent(event);
}

}  // namespace studylab::ui
