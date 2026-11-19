#include "ui/pages/SettingsPage.h"

#include "data/sqlite/Database.h"
#include "services/settings/SettingsService.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/ToggleSwitch.h"
#include "ui/components/Widgets.h"
#include "ui/pages/PageScaffold.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
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
    m_categories->addItems({tr("Apparence"), tr("Données"), tr("À propos")});
    sideLayout->addWidget(m_categories);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildAppearance());
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
    v->addWidget(makeSeparator());

    auto* backupBtn = new QPushButton(tr("Créer une sauvegarde"));
    backupBtn->setProperty("variant", QStringLiteral("primary"));
    connect(backupBtn, &QPushButton::clicked, this, &SettingsPage::createBackup);
    v->addWidget(makeSettingRow(tr("Sauvegarde manuelle"),
                                tr("Copie complète et cohérente de la base dans le dossier « backups »."), backupBtn));
    m_backupStatus = makeLabel(QString(), "faint");
    m_backupStatus->setWordWrap(true);
    v->addWidget(m_backupStatus);

    scaffold->body()->addWidget(card);
    scaffold->body()->addStretch(1);
    return scaffold;
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
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString file = m_ctx.paths.backupsDir() + QStringLiteral("/studylab-manuelle-%1.db").arg(stamp);
    QDir().mkpath(m_ctx.paths.backupsDir());
    auto r = m_ctx.db.backupTo(file);
    m_backupStatus->setText(r ? tr("Sauvegarde créée : %1").arg(QDir::toNativeSeparators(file))
                              : tr("Échec de la sauvegarde : %1").arg(r.error().message));
}

}  // namespace studylab::ui
