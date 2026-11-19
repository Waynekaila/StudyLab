#include "ui/library/LibraryPage.h"

#include "services/library/LibraryService.h"
#include "services/settings/SettingsService.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/Widgets.h"
#include "ui/library/CoverArt.h"
#include "ui/library/DocumentDetailPanel.h"
#include "ui/library/LibraryDelegate.h"
#include "ui/library/LibraryModel.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QStyle>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {
const QString kLastImportDir = QStringLiteral("library/lastImportDir");
}

LibraryPage::LibraryPage(AppContext& ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx), m_model(new LibraryModel(ctx.library, this)),
      m_delegate(new LibraryDelegate(ctx.theme, this))
{
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    auto* main = new QWidget(this);
    main->setObjectName(QStringLiteral("PageBody"));
    auto* v = new QVBoxLayout(main);
    v->setContentsMargins(36, 28, 28, 0);
    v->setSpacing(16);
    v->addWidget(buildHeader());
    v->addWidget(buildBanner());
    v->addWidget(buildFilters());

    m_contentStack = new QStackedWidget(main);
    m_view = new QListView(m_contentStack);
    m_view->setObjectName(QStringLiteral("LibraryView"));
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setUniformItemSizes(true);
    m_view->setMouseTracking(true);
    m_view->viewport()->setAttribute(Qt::WA_Hover);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setAccessibleName(tr("Documents de la bibliothèque"));
    m_contentStack->addWidget(m_view);
    m_contentStack->addWidget(buildEmptyState());
    v->addWidget(m_contentStack, 1);
    h->addWidget(main, 1);

    m_detail = new DocumentDetailPanel(ctx.library, ctx.theme, this);
    m_detail->setFixedWidth(ctx.theme.px(352));
    m_detail->hide();
    h->addWidget(m_detail);

    setViewMode(ctx.settings.value(SettingKeys::LibraryView).toString() == QLatin1String("list"));

    m_searchDebounce.setSingleShot(true);
    m_searchDebounce.setInterval(160);
    connect(&m_searchDebounce, &QTimer::timeout, this, &LibraryPage::applyFilters);
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this, &LibraryPage::onSelectionChanged);
    connect(m_detail, &DocumentDetailPanel::removed, this, [this] {
        m_selectedId = 0;
        m_detail->hide();
    });

    LibraryService& lib = ctx.library;
    connect(&lib, &LibraryService::documentsChanged, this, [this] {
        // Rafraîchi immédiatement si l'écran est visible, sinon à son affichage.
        if (isVisible())
            refresh();
        else
            m_dirty = true;
    });
    connect(&lib, &LibraryService::importStarted, this, &LibraryPage::onImportStarted);
    connect(&lib, &LibraryService::importProgress, this, &LibraryPage::onImportProgress);
    connect(&lib, &LibraryService::importFinished, this, &LibraryPage::onImportFinished);
    connect(&ctx.theme, &ThemeManager::themeChanged, this, [this] {
        m_detail->setFixedWidth(m_ctx.theme.px(352));
        setViewMode(m_delegate->mode() == LibraryDelegate::Mode::List);
    });

    refreshTags();
    applyFilters();
}

QWidget* LibraryPage::buildHeader()
{
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addWidget(makeLabel(tr("Bibliothèque"), "h1"));
    m_subtitle = makeLabel(QString(), "subtitle");
    titles->addWidget(m_subtitle);
    h->addLayout(titles, 1);

    auto* importBtn = new QPushButton(tr("Importer"));
    importBtn->setObjectName(QStringLiteral("ImportButton"));
    importBtn->setProperty("variant", QStringLiteral("primary"));
    importBtn->setIcon(Icons::get(QStringLiteral("plus"), m_ctx.theme.palette().onPrimary, 16));
    importBtn->setCursor(Qt::PointingHandCursor);
    auto* menu = new QMenu(importBtn);
    menu->addAction(tr("Fichiers PDF ou EPUB…"), this, &LibraryPage::chooseFiles);
    menu->addAction(tr("Dossier (avec ses sous-dossiers)…"), this, &LibraryPage::chooseFolder);
    importBtn->setMenu(menu);
    connect(&m_ctx.theme, &ThemeManager::themeChanged, importBtn, [this, importBtn] {
        importBtn->setIcon(Icons::get(QStringLiteral("plus"), m_ctx.theme.palette().onPrimary, 16));
    });
    h->addWidget(importBtn, 0, Qt::AlignTop);
    return w;
}

QWidget* LibraryPage::buildBanner()
{
    m_banner = new QFrame;
    m_banner->setObjectName(QStringLiteral("ImportBanner"));
    auto* h = new QHBoxLayout(m_banner);
    h->setContentsMargins(14, 10, 10, 10);
    h->setSpacing(12);
    m_bannerText = makeLabel(QString());
    m_bannerText->setWordWrap(true);
    m_progress = new QProgressBar;
    m_progress->setTextVisible(false);
    m_progress->setFixedWidth(180);
    m_progress->setFixedHeight(6);
    m_cancel = new QPushButton(tr("Annuler"));
    m_cancel->setProperty("variant", QStringLiteral("ghost"));
    connect(m_cancel, &QPushButton::clicked, &m_ctx.library, &LibraryService::cancelImport);
    m_details = new QPushButton(tr("Détails"));
    connect(m_details, &QPushButton::clicked, this, [this] {
        QMessageBox box(QMessageBox::Information, tr("Rapport d'import"), m_bannerText->text(), QMessageBox::Ok, window());
        box.setDetailedText(m_reportDetails);
        box.exec();
    });
    m_closeBanner = new QPushButton(tr("Fermer"));
    m_closeBanner->setProperty("variant", QStringLiteral("ghost"));
    connect(m_closeBanner, &QPushButton::clicked, m_banner, &QWidget::hide);
    h->addWidget(m_bannerText, 1);
    h->addWidget(m_progress);
    h->addWidget(m_cancel);
    h->addWidget(m_details);
    h->addWidget(m_closeBanner);
    m_banner->hide();
    return m_banner;
}

QWidget* LibraryPage::buildFilters()
{
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Titre, auteur, ISBN, tag…"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Rechercher dans la bibliothèque"));
    m_search->setMinimumWidth(120);
    auto* searchIcon = m_search->addAction(Icons::get(QStringLiteral("search"), m_ctx.theme.palette().text3, 16),
                                           QLineEdit::LeadingPosition);
    connect(&m_ctx.theme, &ThemeManager::themeChanged, searchIcon, [this, searchIcon] {
        searchIcon->setIcon(Icons::get(QStringLiteral("search"), m_ctx.theme.palette().text3, 16));
    });
    connect(m_search, &QLineEdit::textChanged, this, [this] { m_searchDebounce.start(); });
    h->addWidget(m_search, 2);

    m_statusFilter = new QComboBox;
    m_statusFilter->setAccessibleName(tr("Filtrer par statut"));
    m_statusFilter->addItem(tr("Statut"), -1);
    for (ReadingStatus s : allReadingStatuses())
        m_statusFilter->addItem(readingStatusLabel(s), int(s));
    h->addWidget(m_statusFilter);

    m_formatFilter = new QComboBox;
    m_formatFilter->setAccessibleName(tr("Filtrer par format"));
    m_formatFilter->addItem(tr("Format"), -1);
    m_formatFilter->addItem(documentFormatLabel(DocumentFormat::Pdf), int(DocumentFormat::Pdf));
    m_formatFilter->addItem(documentFormatLabel(DocumentFormat::Epub), int(DocumentFormat::Epub));
    h->addWidget(m_formatFilter);

    m_tagFilter = new QComboBox;
    m_tagFilter->setAccessibleName(tr("Filtrer par tag"));
    m_tagFilter->setMaximumWidth(m_ctx.theme.px(160));
    h->addWidget(m_tagFilter);

    m_favorites = new QPushButton(tr("☆ Favoris"));
    m_favorites->setCheckable(true);
    m_favorites->setProperty("chipButton", true);
    m_favorites->setAccessibleName(tr("Favoris uniquement"));
    connect(m_favorites, &QPushButton::toggled, this, [this](bool on) {
        m_favorites->setText(on ? tr("★ Favoris") : tr("☆ Favoris"));
        applyFilters();
    });
    h->addWidget(m_favorites);

    h->addSpacing(6);
    m_sort = new QComboBox;
    m_sort->setAccessibleName(tr("Trier"));
    m_sort->setToolTip(tr("Trier"));
    m_sort->addItem(tr("Récents"), int(db::DocumentQuery::Sort::RecentlyAdded));
    m_sort->addItem(tr("Ouverts récemment"), int(db::DocumentQuery::Sort::RecentlyOpened));
    m_sort->addItem(tr("Titre A → Z"), int(db::DocumentQuery::Sort::Title));
    m_sort->addItem(tr("Auteur A → Z"), int(db::DocumentQuery::Sort::Author));
    m_sort->setCurrentIndex(qBound(0, m_ctx.settings.value(SettingKeys::LibrarySort, 0).toInt(), 3));
    h->addWidget(m_sort);

    m_viewToggle = new SegmentedControl({tr("Grille"), tr("Liste")});
    m_viewToggle->setAccessibleName(tr("Affichage"));
    auto viewIcons = [this] {
        const Palette& pal = m_ctx.theme.palette();
        m_viewToggle->setSegmentIcon(0, Icons::stateful(QStringLiteral("grid"), pal.text2, pal.text, 16));
        m_viewToggle->setSegmentIcon(1, Icons::stateful(QStringLiteral("list"), pal.text2, pal.text, 16));
    };
    viewIcons();
    connect(&m_ctx.theme, &ThemeManager::themeChanged, m_viewToggle, viewIcons);
    connect(m_viewToggle, &SegmentedControl::currentIndexChanged, this, [this](int i) {
        setViewMode(i == 1);
        m_ctx.settings.setValue(SettingKeys::LibraryView, i == 1 ? QStringLiteral("list") : QStringLiteral("grid"));
    });
    h->addWidget(m_viewToggle);

    for (QComboBox* c : {m_statusFilter, m_formatFilter, m_tagFilter, m_sort}) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        c->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    m_favorites->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_viewToggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    for (QComboBox* c : {m_statusFilter, m_formatFilter, m_tagFilter})
        connect(c, &QComboBox::activated, this, &LibraryPage::applyFilters);
    connect(m_sort, &QComboBox::activated, this, [this](int i) {
        m_ctx.settings.setValue(SettingKeys::LibrarySort, i);
        applyFilters();
    });
    return w;
}

QWidget* LibraryPage::buildEmptyState()
{
    m_empty = new QWidget;
    auto* outer = new QVBoxLayout(m_empty);
    outer->setContentsMargins(0, 40, 0, 40);
    auto* card = makeCard();
    card->setObjectName(QStringLiteral("EmptyState"));
    card->setMaximumWidth(520);
    auto* v = static_cast<QVBoxLayout*>(card->layout());
    v->setContentsMargins(32, 30, 32, 30);
    v->setSpacing(10);
    m_emptyTitle = makeLabel(QString(), "h2");
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyText = makeLabel(QString(), "muted");
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    v->addWidget(m_emptyTitle);
    v->addWidget(m_emptyText);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_emptyImport = new QPushButton(tr("Importer des fichiers"));
    m_emptyImport->setProperty("variant", QStringLiteral("primary"));
    connect(m_emptyImport, &QPushButton::clicked, this, &LibraryPage::chooseFiles);
    auto* folder = new QPushButton(tr("Importer un dossier"));
    connect(folder, &QPushButton::clicked, this, &LibraryPage::chooseFolder);
    m_emptyReset = new QPushButton(tr("Réinitialiser les filtres"));
    connect(m_emptyReset, &QPushButton::clicked, this, &LibraryPage::resetFilters);
    buttons->addWidget(m_emptyImport);
    buttons->addWidget(folder);
    buttons->addWidget(m_emptyReset);
    buttons->addStretch(1);
    v->addSpacing(6);
    v->addLayout(buttons);
    m_emptyFolder = folder;
    outer->addWidget(card, 0, Qt::AlignHCenter);
    outer->addStretch(1);
    return m_empty;
}

void LibraryPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_dirty)
        refresh();
}

void LibraryPage::setViewMode(bool list)
{
    m_delegate->setMode(list ? LibraryDelegate::Mode::List : LibraryDelegate::Mode::Grid);
    m_viewToggle->setCurrentIndex(list ? 1 : 0);
    if (list) {
        m_view->setViewMode(QListView::ListMode);
        m_view->setGridSize(QSize());
        m_view->setSpacing(0);
        m_view->setWrapping(false);
        m_view->setFlow(QListView::TopToBottom);
    } else {
        m_view->setViewMode(QListView::IconMode);
        m_view->setGridSize(m_delegate->gridCellSize());
        m_view->setSpacing(0);
        m_view->setWrapping(true);
        m_view->setFlow(QListView::LeftToRight);
    }
    m_view->setResizeMode(QListView::Adjust);
    m_view->setMovement(QListView::Static);
    m_view->setDragEnabled(false);
    m_view->doItemsLayout();
}

void LibraryPage::applyFilters()
{
    db::DocumentQuery q;
    q.text = m_search->text();
    if (const int s = m_statusFilter->currentData().toInt(); s >= 0)
        q.status = ReadingStatus(s);
    if (const int f = m_formatFilter->currentData().toInt(); f >= 0)
        q.format = DocumentFormat(f);
    q.tag = m_tagFilter->currentData().toString();
    q.favoritesOnly = m_favorites->isChecked();
    q.sort = db::DocumentQuery::Sort(m_sort->currentData().toInt());
    m_model->setQuery(q);
    selectDocument(m_selectedId);
    updateHeader();
    updateEmptyState();
}

void LibraryPage::refresh()
{
    m_dirty = false;
    refreshTags();
    m_model->reload();
    selectDocument(m_selectedId);
    updateHeader();
    updateEmptyState();
}

void LibraryPage::refreshTags()
{
    const QString current = m_tagFilter->currentData().toString();
    m_tagFilter->clear();
    m_tagFilter->addItem(tr("Tags"));
    const QStringList tags = m_ctx.library.tags();
    for (const QString& t : tags)
        m_tagFilter->addItem(QStringLiteral("#") + t, t);
    // Les libellés affichent « #tag » ; la donnée associée est le nom du tag.
    const int idx = current.isEmpty() ? 0 : m_tagFilter->findData(current);
    m_tagFilter->setCurrentIndex(qMax(0, idx));
    m_tagFilter->setEnabled(!tags.isEmpty());
}

void LibraryPage::updateHeader()
{
    const QLocale fr(QLocale::French, QLocale::France);
    const int total = m_ctx.library.count();
    QString text = countLabel(total, tr("document"), tr("documents"));
    if (total > 0)
        text += QStringLiteral(" · ") + CoverArt::formatFileSize(m_ctx.library.totalFileSize());
    const int shown = m_model->rowCount();
    if (shown != total && total > 0)
        text += QStringLiteral(" · ") + countLabel(shown, tr("affiché"), tr("affichés"));
    m_subtitle->setText(text);
}

void LibraryPage::updateEmptyState()
{
    const bool libraryEmpty = m_ctx.library.count() == 0;
    const bool noResults = m_model->rowCount() == 0;
    m_contentStack->setCurrentWidget(noResults ? m_empty : static_cast<QWidget*>(m_view));
    if (libraryEmpty) {
        m_emptyTitle->setText(tr("Votre bibliothèque est vide"));
        m_emptyText->setText(tr("Importez vos PDF et EPUB, ou glissez-déposez des fichiers ou un dossier "
                                "n'importe où dans la fenêtre. Les doublons sont détectés automatiquement."));
    } else {
        m_emptyTitle->setText(tr("Aucun document ne correspond"));
        m_emptyText->setText(tr("Modifiez la recherche ou les filtres pour retrouver vos documents."));
    }
    m_emptyImport->setVisible(libraryEmpty);
    m_emptyFolder->setVisible(libraryEmpty);
    m_emptyReset->setVisible(!libraryEmpty);
}

void LibraryPage::resetFilters()
{
    m_search->clear();
    m_statusFilter->setCurrentIndex(0);
    m_formatFilter->setCurrentIndex(0);
    m_tagFilter->setCurrentIndex(0);
    const QSignalBlocker block(m_favorites);
    m_favorites->setChecked(false);
    m_favorites->setText(tr("☆ Favoris"));
    applyFilters();
}

void LibraryPage::selectDocument(qint64 id)
{
    const int row = id > 0 ? m_model->rowOf(id) : -1;
    if (row < 0) {
        m_view->selectionModel()->clearSelection();
        m_view->selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
        if (id > 0 && m_detail->isVisible()) {
            // Masqué par les filtres : le détail reste affiché tant que le document existe.
            m_detail->showDocument(id);
        }
        return;
    }
    const QModelIndex idx = m_model->index(row);
    m_view->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    m_view->scrollTo(idx);
}

void LibraryPage::onSelectionChanged()
{
    const QModelIndex idx = m_view->currentIndex();
    if (!idx.isValid())
        return;
    m_selectedId = idx.data(LibraryModel::IdRole).toLongLong();
    m_detail->showDocument(m_selectedId);
    m_detail->show();
}

void LibraryPage::importPaths(const QStringList& paths)
{
    const bool link = m_ctx.settings.value(SettingKeys::ImportMode).toString() == QLatin1String("link");
    m_ctx.library.importPaths(paths, link ? StorageMode::Linked : StorageMode::Managed);
}

void LibraryPage::chooseFiles()
{
    const QString dir = m_ctx.settings
                            .value(kLastImportDir, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                            .toString();
    const QStringList files = QFileDialog::getOpenFileNames(window(), tr("Importer des documents"), dir,
                                                            tr("Documents (*.pdf *.epub);;PDF (*.pdf);;EPUB (*.epub)"));
    if (files.isEmpty())
        return;
    m_ctx.settings.setValue(kLastImportDir, QFileInfo(files.first()).absolutePath());
    importPaths(files);
}

void LibraryPage::chooseFolder()
{
    const QString dir = m_ctx.settings
                            .value(kLastImportDir, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                            .toString();
    const QString folder = QFileDialog::getExistingDirectory(window(), tr("Importer un dossier"), dir);
    if (folder.isEmpty())
        return;
    m_ctx.settings.setValue(kLastImportDir, folder);
    importPaths({folder});
}

void LibraryPage::onImportStarted()
{
    m_banner->setProperty("state", QStringLiteral("running"));
    m_banner->style()->unpolish(m_banner);
    m_banner->style()->polish(m_banner);
    m_bannerText->setText(tr("Recherche des fichiers…"));
    m_progress->setRange(0, 0);
    m_progress->show();
    m_cancel->show();
    m_details->hide();
    m_closeBanner->hide();
    m_banner->show();
}

void LibraryPage::onImportProgress(int done, int total)
{
    if (total <= 0)
        return;
    m_progress->setRange(0, total);
    m_progress->setValue(done);
    m_bannerText->setText(tr("Import en cours : %1 sur %2").arg(done).arg(total));
}

void LibraryPage::onImportFinished(const ImportReport& report)
{
    QStringList parts;
    parts << countLabel(report.importedIds.size(), tr("importé"), tr("importés"));
    if (!report.duplicates.isEmpty())
        parts << countLabel(report.duplicates.size(), tr("doublon ignoré"), tr("doublons ignorés"));
    if (!report.failures.isEmpty())
        parts << countLabel(report.failures.size(), tr("échec"), tr("échecs"));
    QString text = parts.join(QStringLiteral(" · "));
    if (report.canceled)
        text = tr("Import annulé · ") + text;
    if (report.requested == 0 && !report.canceled)
        text = tr("Aucun fichier PDF ou EPUB trouvé.");
    m_bannerText->setText(text);

    QStringList details;
    for (const auto& d : report.duplicates)
        details << tr("Doublon : %1\n    %2").arg(QDir::toNativeSeparators(d.path), d.reason);
    for (const auto& f : report.failures)
        details << tr("Échec : %1\n    %2").arg(QDir::toNativeSeparators(f.path), f.reason);
    m_reportDetails = details.join(QLatin1Char('\n'));

    m_banner->setProperty("state", report.failures.isEmpty() ? QStringLiteral("done") : QStringLiteral("warning"));
    m_banner->style()->unpolish(m_banner);
    m_banner->style()->polish(m_banner);
    m_progress->hide();
    m_cancel->hide();
    m_details->setVisible(!details.isEmpty());
    m_closeBanner->show();
    m_banner->show();
    refresh();
    if (report.importedIds.size() == 1)
        selectDocument(report.importedIds.first());
}

}  // namespace studylab::ui
