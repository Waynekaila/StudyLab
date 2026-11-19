#include "ui/reader/ReaderPage.h"

#include "services/library/LibraryService.h"
#include "services/settings/SettingsService.h"
#include "ui/components/Widgets.h"
#include "ui/reader/EpubReaderTab.h"
#include "ui/reader/PdfReaderTab.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {
const QString kOpenTabs = QStringLiteral("reader/openTabs");
const QString kCurrentTab = QStringLiteral("reader/currentTab");
}  // namespace

ReaderPage::ReaderPage(AppContext& ctx, QWidget* parent) : QWidget(parent), m_ctx(ctx)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    v->addWidget(m_stack);

    m_tabs = new QTabWidget(m_stack);
    m_tabs->setObjectName(QStringLiteral("ReaderTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setElideMode(Qt::ElideRight);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &ReaderPage::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        if (auto* tab = currentTab())
            tab->setFocus();
        persistTabs();
    });
    connect(m_tabs->tabBar(), &QTabBar::tabMoved, this, &ReaderPage::persistTabs);
    m_stack->addWidget(m_tabs);

    m_empty = new QWidget(m_stack);
    auto* ev = new QVBoxLayout(m_empty);
    ev->setContentsMargins(40, 40, 40, 40);
    ev->addStretch(1);
    auto* card = makeCard();
    card->setMaximumWidth(480);
    auto* cv = static_cast<QVBoxLayout*>(card->layout());
    cv->setContentsMargins(32, 28, 32, 28);
    auto* title = makeLabel(tr("Aucun document ouvert"), "h2");
    title->setAlignment(Qt::AlignCenter);
    auto* text = makeLabel(tr("Ouvrez un PDF ou un EPUB depuis la bibliothèque : il s'affichera ici, à la position où vous "
                              "l'aviez laissé."),
                           "muted");
    text->setAlignment(Qt::AlignCenter);
    text->setWordWrap(true);
    auto* button = new QPushButton(tr("Aller à la bibliothèque"));
    button->setProperty("variant", QStringLiteral("primary"));
    connect(button, &QPushButton::clicked, this, &ReaderPage::libraryRequested);
    cv->addWidget(title);
    cv->addWidget(text);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(button);
    row->addStretch(1);
    cv->addSpacing(6);
    cv->addLayout(row);
    ev->addWidget(card, 0, Qt::AlignHCenter);
    ev->addStretch(2);
    m_stack->addWidget(m_empty);

    m_exitFocus = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_exitFocus->setContext(Qt::WidgetWithChildrenShortcut);
    m_exitFocus->setEnabled(false);
    connect(m_exitFocus, &QShortcut::activated, this, [this] {
        if (auto* tab = currentTab())
            tab->setFocusMode(false);
    });

    connect(&ctx.library, &LibraryService::documentsChanged, this, [this] {
        // Un document retiré de la bibliothèque ferme son onglet.
        for (int i = m_tabs->count() - 1; i >= 0; --i) {
            auto* tab = static_cast<ReaderTab*>(m_tabs->widget(i));
            if (!m_ctx.library.document(tab->documentId()))
                closeTab(i);
        }
    });

    restoreSession();
    updateEmptyState();
}

ReaderPage::~ReaderPage()
{
    saveSession();
}

int ReaderPage::tabCount() const
{
    return m_tabs->count();
}

ReaderTab* ReaderPage::currentTab() const
{
    return static_cast<ReaderTab*>(m_tabs->currentWidget());
}

ReaderTab* ReaderPage::tabFor(qint64 documentId) const
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* tab = static_cast<ReaderTab*>(m_tabs->widget(i));
        if (tab->documentId() == documentId)
            return tab;
    }
    return nullptr;
}

void ReaderPage::openItem(qint64 documentId, bool comment, qint64 itemId)
{
    openDocument(documentId);
    if (ReaderTab* tab = tabFor(documentId))
        tab->revealItem(comment, itemId);
}

void ReaderPage::openDocument(qint64 documentId)
{
    if (auto* existing = tabFor(documentId)) {
        m_tabs->setCurrentWidget(existing);
        return;
    }
    auto doc = m_ctx.library.document(documentId);
    if (!doc)
        return;
    ReaderTab* tab = nullptr;
    if (doc.value().format == DocumentFormat::Pdf)
        tab = new PdfReaderTab(m_ctx, documentId, m_tabs);
    else if (doc.value().format == DocumentFormat::Epub)
        tab = new EpubReaderTab(m_ctx, documentId, m_tabs);
    else
        return;
    const QString title = doc.value().title;
    const int idx = m_tabs->addTab(tab, QFontMetrics(font()).elidedText(title, Qt::ElideRight, 180));
    m_tabs->setTabToolTip(idx, title);
    // Bouton de fermeture au trait, aux couleurs du thème (celui du style natif détonne).
    auto* close = new QToolButton(m_tabs->tabBar());
    close->setObjectName(QStringLiteral("TabClose"));
    close->setAutoRaise(true);
    close->setCursor(Qt::PointingHandCursor);
    close->setToolTip(tr("Fermer l'onglet"));
    close->setAccessibleName(tr("Fermer l'onglet"));
    const auto refreshClose = [this, close] {
        close->setIcon(Icons::get(QStringLiteral("x"), m_ctx.theme.palette().text2, 14));
    };
    refreshClose();
    connect(&m_ctx.theme, &ThemeManager::themeChanged, close, refreshClose);
    connect(close, &QToolButton::clicked, this, [this, tab] {
        const int i = m_tabs->indexOf(tab);
        if (i >= 0)
            closeTab(i);
    });
    m_tabs->tabBar()->setTabButton(idx, QTabBar::RightSide, close);
    connect(tab, &ReaderTab::focusModeToggled, this, [this](bool on) {
        m_exitFocus->setEnabled(on);
        m_tabs->tabBar()->setVisible(!on);
        emit focusModeChanged(on);
    });
    connect(tab, &ReaderTab::fullScreenToggled, this, &ReaderPage::fullScreenRequested);
    if (!m_restoring)
        m_tabs->setCurrentIndex(idx);
    updateEmptyState();
    persistTabs();
}

void ReaderPage::closeTab(int index)
{
    auto* tab = static_cast<ReaderTab*>(m_tabs->widget(index));
    if (!tab)
        return;
    tab->setFocusMode(false);
    m_tabs->removeTab(index);
    delete tab;  // enregistre la position
    updateEmptyState();
    persistTabs();
}

void ReaderPage::updateEmptyState()
{
    m_stack->setCurrentWidget(m_tabs->count() > 0 ? static_cast<QWidget*>(m_tabs) : m_empty);
}

void ReaderPage::persistTabs()
{
    if (m_restoring)
        return;
    QJsonArray ids;
    for (int i = 0; i < m_tabs->count(); ++i)
        ids.append(static_cast<ReaderTab*>(m_tabs->widget(i))->documentId());
    m_ctx.settings.setValue(kOpenTabs, ids);
    m_ctx.settings.setValue(kCurrentTab, m_tabs->currentIndex());
}

void ReaderPage::restoreSession()
{
    m_restoring = true;
    for (const QJsonValue& v : m_ctx.settings.value(kOpenTabs).toArray())
        openDocument(qint64(v.toDouble()));
    m_restoring = false;
    const int current = m_ctx.settings.value(kCurrentTab, 0).toInt();
    if (current >= 0 && current < m_tabs->count())
        m_tabs->setCurrentIndex(current);
    persistTabs();
}

void ReaderPage::saveSession()
{
    for (int i = 0; i < m_tabs->count(); ++i)
        static_cast<ReaderTab*>(m_tabs->widget(i))->savePosition();
    persistTabs();
}

}  // namespace studylab::ui
