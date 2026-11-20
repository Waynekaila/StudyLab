#include "ui/pages/SettingsPage.h"

#include "services/search/TextIndexer.h"

#include "data/sqlite/Database.h"
#include "services/backup/BackupService.h"
#include "services/settings/SettingsService.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/ToggleSwitch.h"
#include "ui/components/Widgets.h"
#include "ui/pages/PageScaffold.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace studylab::ui {

namespace {
const QList<int> kScales = {90, 100, 110, 125, 150};

QWidget* titledSection(const QString& title, const QString& subtitle, PageScaffold* scaffold)
{
    auto* head = new QWidget;
    auto* v = new QVBoxLayout(head);
    v->setContentsMargins(0, 0, 0, 2);
    v->setSpacing(4);
    v->addWidget(makeLabel(title, "h1"));
    if (!subtitle.isEmpty())
        v->addWidget(makeLabel(subtitle, "subtitle"));
    scaffold->body()->addWidget(head);
    return head;
}
}  // namespace

SettingsPage::SettingsPage(AppContext& ctx, QWidget* parent) : QWidget(parent), m_ctx(ctx)
{
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    auto* side = new QFrame(this);
    side->setObjectName(QStringLiteral("SubSidebar"));
    side->setFixedWidth(232);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(12, 22, 12, 12);
    m_categories = new QListWidget(side);
    m_categories->setObjectName(QStringLiteral("Categories"));
    m_categories->addItems({tr("Apparence"), tr("Bibliothèque et import"), tr("Données"), tr("À propos")});
    sideLayout->addWidget(m_categories);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildAppearance());
    m_stack->addWidget(buildLibrary());
    m_stack->addWidget(buildData());
    m_stack->addWidget(buildAbout());

    h->addWidget(side);
    h->addWidget(m_stack, 1);

    connect(m_categories, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_categories->setCurrentRow(0);

    connect(&m_ctx.settings, &SettingsService::changed, this, [this] { syncFromSettings(); });
    syncFromSettings();
}

QWidget* SettingsPage::buildAppearance()
{
    auto* scaffold = new PageScaffold(nullptr, 820);
    titledSection(tr("Apparence"), tr("Les changements s'appliquent immédiatement, sans redémarrage."), scaffold);

    auto* card = makeCard();
    auto* v = static_cast<QVBoxLayout*>(card->layout());
    v->setSpacing(0);

    m_theme = new SegmentedControl({tr("Système"), tr("Clair"), tr("Sombre")});
    m_theme->setAccessibleName(tr("Thème"));
    connect(m_theme, &SegmentedControl::currentIndexChanged, this, [this](int i) {
        static const ThemeMode modes[] = {ThemeMode::System, ThemeMode::Light, ThemeMode::Dark};
        m_ctx.settings.setThemeMode(modes[i]);
    });
    v->addWidget(makeSettingRow(tr("Thème"), tr("Le mode Système suit le réglage clair ou sombre de votre bureau."), m_theme));
    v->addWidget(makeSeparator());

    m_scale = new QComboBox;
    m_scale->setAccessibleName(tr("Taille de l'interface"));
    for (int s : kScales)
        m_scale->addItem(QStringLiteral("%1 %").arg(s), s);
    m_scale->setMinimumWidth(140);
    connect(m_scale, &QComboBox::activated, this,
            [this](int i) { m_ctx.settings.setUiScale(m_scale->itemData(i).toInt() / 100.0); });
    v->addWidget(makeSettingRow(tr("Taille de l'interface"), tr("Indépendante de la taille du texte de lecture."), m_scale));
    v->addWidget(makeSeparator());

    m_animations = new ToggleSwitch;
    m_animations->setAccessibleName(tr("Animations"));
    connect(m_animations, &ToggleSwitch::toggled, this,
            [this](bool on) { m_ctx.settings.setAnimationsEnabled(on); });
    v->addWidget(makeSettingRow(tr("Animations"), tr("Transitions douces entre les écrans."), m_animations));

    scaffold->body()->addWidget(card);
    scaffold->body()->addStretch(1);
    return scaffold;
}

QWidget* SettingsPage::buildLibrary()
{
    auto* scaffold = new PageScaffold(nullptr, 820);
    titledSection(tr("Bibliothèque et import"), tr("Comment les fichiers importés sont conservés."), scaffold);

    auto* card = makeCard();
    auto* v = static_cast<QVBoxLayout*>(card->layout());
    v->setSpacing(0);
    m_importMode = new SegmentedControl({tr("Copier"), tr("Lier")});
    m_importMode->setAccessibleName(tr("Mode d'import"));
    connect(m_importMode, &SegmentedControl::currentIndexChanged, this, [this](int i) {
        m_ctx.settings.setValue(SettingKeys::ImportMode, i == 1 ? QStringLiteral("link") : QStringLiteral("copy"));
    });
    v->addWidget(makeSettingRow(
        tr("Mode d'import"),
        tr("Copier (recommandé) : le fichier est rangé dans la bibliothèque de StudyLab et reste disponible même si "
           "l'original est déplacé. Lier : le fichier reste à sa place, sans copie."),
        m_importMode));
    scaffold->body()->addWidget(card);
    scaffold->body()->addStretch(1);
    return scaffold;
}

void SettingsPage::setTextIndexer(TextIndexer* indexer)
{
    m_indexer = indexer;
    m_reindexButton->setEnabled(indexer != nullptr);
    if (indexer) {
        connect(indexer, &TextIndexer::progressChanged, this, &SettingsPage::refreshIndexStatus);
        connect(indexer, &TextIndexer::finished, this, &SettingsPage::refreshIndexStatus);
    }
    refreshIndexStatus();
}

void SettingsPage::refreshIndexStatus()
{
    if (!m_indexer) {
        m_indexStatus->clear();
        return;
    }
    const auto [done, total] = m_indexer->progress();
    if (total == 0)
        m_indexStatus->setText(tr("Aucun document PDF ou EPUB dans la bibliothèque."));
    else if (done >= total)
        m_indexStatus->setText(tr("Texte de tous les documents indexé (%1).").arg(countLabel(total, tr("document"), tr("documents"))));
    else
        m_indexStatus->setText(tr("Indexation en cours : %1 sur %2.").arg(done).arg(countLabel(total, tr("document"), tr("documents"))));
}

QWidget* SettingsPage::buildData()
{
    auto* scaffold = new PageScaffold(nullptr, 820);
    titledSection(tr("Données"), tr("Tout est stocké sur cet ordinateur. Aucun compte, aucun envoi en ligne."), scaffold);

    auto* card = makeCard();
    auto* v = static_cast<QVBoxLayout*>(card->layout());
    v->setSpacing(0);
    auto* openBtn = new QPushButton(tr("Ouvrir"));
    connect(openBtn, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctx.paths.dataDir())); });
    v->addWidget(makeSettingRow(tr("Dossier des données"), QDir::toNativeSeparators(m_ctx.paths.dataDir()), openBtn));
    m_reindexButton = new QPushButton(tr("Réindexer"));
    m_reindexButton->setEnabled(false);
    connect(m_reindexButton, &QPushButton::clicked, this, [this] {
        if (!m_indexer)
            return;
        if (auto r = m_indexer->reindexAll(); !r)
            m_indexStatus->setText(tr("Réindexation impossible : %1").arg(r.error().message));
    });
    v->addWidget(makeSettingRow(tr("Texte intégral des documents"),
                                tr("Le texte des PDF et des EPUB est extrait en tâche de fond pour la recherche "
                                   "(Ctrl + K). Les pages numérisées sans texte ne sont pas encore lues (OCR à venir)."),
                                m_reindexButton));
    m_indexStatus = makeLabel(QString(), "faint");
    m_indexStatus->setObjectName(QStringLiteral("IndexStatus"));
    m_indexStatus->setWordWrap(true);
    v->addWidget(m_indexStatus);
    scaffold->body()->addWidget(card);

    // Sauvegardes
    auto* backups = makeCard();
    auto* b = static_cast<QVBoxLayout*>(backups->layout());
    b->setSpacing(8);
    m_backupButton = new QPushButton(tr("Créer une sauvegarde"));
    m_backupButton->setProperty("variant", QStringLiteral("primary"));
    connect(m_backupButton, &QPushButton::clicked, this, &SettingsPage::createBackup);
    b->addWidget(makeSettingRow(
        tr("Sauvegardes"),
        tr("Copie complète de la base, automatique chaque jour au démarrage et avant chaque mise à jour. "
           "Sont conservées : toutes celles de la semaine, une par semaine pendant un mois, une par mois pendant "
           "six mois. Les sauvegardes manuelles sont gardées."),
        m_backupButton));
    m_lastBackup = makeLabel(QString(), "muted");
    m_lastBackup->setWordWrap(true);
    b->addWidget(m_lastBackup);
    m_backupStatus = makeLabel(QString(), "faint");
    m_backupStatus->setWordWrap(true);
    b->addWidget(m_backupStatus);

    m_backups = new QListWidget;
    m_backups->setObjectName(QStringLiteral("BackupList"));
    m_backups->setAccessibleName(tr("Sauvegardes disponibles"));
    m_backups->setMinimumHeight(m_ctx.theme.px(180));
    b->addWidget(m_backups);

    auto* actions = new QHBoxLayout;
    m_restoreButton = new QPushButton(tr("Restaurer…"));
    m_restoreButton->setEnabled(false);
    m_restoreButton->setToolTip(tr("Remplacer la base par la sauvegarde choisie (au redémarrage)"));
    auto* folder = new QPushButton(tr("Afficher le dossier"));
    connect(folder, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctx.paths.backupsDir())); });
    actions->addWidget(m_restoreButton);
    actions->addWidget(folder);
    actions->addStretch(1);
    b->addLayout(actions);
    connect(m_backups, &QListWidget::currentRowChanged, this,
            [this](int row) { m_restoreButton->setEnabled(row >= 0 && !m_ctx.backups.pendingRestore()); });
    connect(m_restoreButton, &QPushButton::clicked, this, &SettingsPage::restoreSelected);

    auto* pendingRow = new QHBoxLayout;
    m_pendingLabel = makeLabel(QString(), "muted");
    m_pendingLabel->setWordWrap(true);
    m_cancelRestore = new QPushButton(tr("Annuler la restauration"));
    m_cancelRestore->setProperty("variant", QStringLiteral("ghost"));
    connect(m_cancelRestore, &QPushButton::clicked, this, [this] {
        m_ctx.backups.cancelPendingRestore();
        refreshBackups();
    });
    pendingRow->addWidget(m_pendingLabel, 1);
    pendingRow->addWidget(m_cancelRestore);
    b->addLayout(pendingRow);

    scaffold->body()->addWidget(backups);
    scaffold->body()->addStretch(1);
    refreshBackups();
    return scaffold;
}

void SettingsPage::refreshBackups()
{
    const QLocale locale(QLocale::French, QLocale::France);
    const QList<BackupInfo> list = m_ctx.backups.list();
    m_backups->clear();
    for (const BackupInfo& info : list) {
        auto* item = new QListWidgetItem(tr("%1 · %2 · %3")
                                             .arg(locale.toString(info.createdAt, QStringLiteral("dd/MM/yyyy HH:mm")),
                                                  backupKindLabel(info.kind), locale.formattedDataSize(info.size, 1, QLocale::DataSizeTraditionalFormat)));
        item->setData(Qt::UserRole, info.path);
        item->setToolTip(QDir::toNativeSeparators(info.path));
        m_backups->addItem(item);
    }
    if (list.isEmpty()) {
        m_lastBackup->setText(tr("Aucune sauvegarde pour l'instant."));
    } else {
        const BackupInfo& last = list.first();
        m_lastBackup->setText(tr("Dernière sauvegarde : %1 (%2).")
                                  .arg(locale.toString(last.createdAt, QStringLiteral("dddd d MMMM yyyy 'à' HH:mm")),
                                       backupKindLabel(last.kind).toLower()));
    }
    const auto pending = m_ctx.backups.pendingRestore();
    m_pendingLabel->setVisible(bool(pending));
    m_cancelRestore->setVisible(bool(pending));
    if (pending) {
        const auto info = BackupService::parse(*pending);
        m_pendingLabel->setText(
            tr("Restauration programmée au prochain démarrage : sauvegarde du %1.")
                .arg(info ? locale.toString(info->createdAt, QStringLiteral("dd/MM/yyyy HH:mm")) : QDir::toNativeSeparators(*pending)));
    }
    m_restoreButton->setEnabled(m_backups->currentRow() >= 0 && !pending);
}

void SettingsPage::restoreSelected()
{
    QListWidgetItem* item = m_backups->currentItem();
    if (!item)
        return;
    const auto info = BackupService::parse(item->data(Qt::UserRole).toString());
    if (!info)
        return;
    int answer = 0;
    if (m_confirmRestore) {
        answer = m_confirmRestore(*info);
    } else {
        QMessageBox box(QMessageBox::Question, tr("Restaurer une sauvegarde"),
                        tr("Remplacer toutes vos données par la sauvegarde du %1 ?")
                            .arg(QLocale(QLocale::French, QLocale::France).toString(info->createdAt, QStringLiteral("dddd d MMMM yyyy 'à' HH:mm"))),
                        QMessageBox::NoButton, window());
        box.setInformativeText(tr("La restauration s'applique au démarrage de StudyLab. Les données actuelles sont "
                                  "d'abord copiées dans le dossier des sauvegardes ; rien n'est perdu."));
        auto* now = box.addButton(tr("Redémarrer maintenant"), QMessageBox::AcceptRole);
        auto* later = box.addButton(tr("Au prochain démarrage"), QMessageBox::ActionRole);
        box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.exec();
        answer = box.clickedButton() == now ? 2 : box.clickedButton() == later ? 1 : 0;
    }
    if (answer == 0)
        return;
    if (auto r = m_ctx.backups.scheduleRestore(info->path); !r) {
        m_backupStatus->setText(tr("Restauration impossible : %1").arg(r.error().message));
        return;
    }
    refreshBackups();
    if (answer == 2) {
        qApp->setProperty(kRestartProperty, true);
        window()->close();
    }
}

QWidget* SettingsPage::buildAbout()
{
    auto* scaffold = new PageScaffold(nullptr, 820);
    titledSection(tr("À propos"), QString(), scaffold);

    auto* card = makeCard();
    auto* v = static_cast<QVBoxLayout*>(card->layout());
    v->addWidget(makeLabel(QStringLiteral("StudyLab %1").arg(QCoreApplication::applicationVersion()), "h2"));
    v->addWidget(makeLabel(tr("Read – Annotate – Understand – Connect – Learn"), "muted"));
    v->addSpacing(6);
    auto* license = makeLabel(tr("Logiciel libre distribué sous licence "
                                 "<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU GPL v3 ou ultérieure</a>."));
    license->setOpenExternalLinks(true);
    license->setTextFormat(Qt::RichText);
    v->addWidget(license);
    v->addWidget(makeLabel(tr("Qt %1 · SQLite %2").arg(QString::fromLatin1(qVersion()), db::Database::sqliteVersion()), "faint"));
    auto* repo = makeLabel(QStringLiteral("<a href=\"https://github.com/Waynekaila/StudyLab\">github.com/Waynekaila/StudyLab</a>"));
    repo->setOpenExternalLinks(true);
    repo->setTextFormat(Qt::RichText);
    v->addWidget(repo);

    scaffold->body()->addWidget(card);
    scaffold->body()->addStretch(1);
    return scaffold;
}

void SettingsPage::syncFromSettings()
{
    m_importMode->setCurrentIndex(m_ctx.settings.value(SettingKeys::ImportMode).toString() == QLatin1String("link") ? 1 : 0);
    switch (m_ctx.settings.themeMode()) {
    case ThemeMode::System: m_theme->setCurrentIndex(0); break;
    case ThemeMode::Light: m_theme->setCurrentIndex(1); break;
    case ThemeMode::Dark: m_theme->setCurrentIndex(2); break;
    }
    const int pct = int(std::lround(m_ctx.settings.uiScale() * 100));
    int best = 1;
    for (int i = 0; i < kScales.size(); ++i) {
        if (std::abs(kScales[i] - pct) < std::abs(kScales[best] - pct))
            best = i;
    }
    m_scale->setCurrentIndex(best);
    {
        const QSignalBlocker block(m_animations);
        m_animations->setAnimated(false);
        m_animations->setChecked(m_ctx.settings.animationsEnabled());
        m_animations->setAnimated(true);
    }
}

void SettingsPage::createBackup()
{
    auto r = m_ctx.backups.create(BackupKind::Manual);
    m_backupStatus->setText(r ? tr("Sauvegarde créée : %1").arg(QDir::toNativeSeparators(r.value().path))
                              : tr("Échec de la sauvegarde : %1").arg(r.error().message));
    refreshBackups();
}

}  // namespace studylab::ui
