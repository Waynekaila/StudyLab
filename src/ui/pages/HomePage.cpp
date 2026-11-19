#include "ui/pages/HomePage.h"

#include "data/migrations/Migrator.h"
#include "data/sqlite/Database.h"
#include "ui/components/Widgets.h"
#include "ui/pages/PageScaffold.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QDate>
#include <QDir>
#include <QDesktopServices>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <QPushButton>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace studylab::ui {

HomePage::HomePage(AppContext& ctx, QWidget* parent) : QWidget(parent), m_ctx(ctx)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scaffold = new PageScaffold(this);
    outer->addWidget(scaffold);
    QVBoxLayout* body = scaffold->body();

    // En-tête
    auto* header = new QVBoxLayout;
    header->setSpacing(2);
    header->addWidget(makeLabel(tr("Bienvenue dans StudyLab"), "h1"));
    m_date = makeLabel(QString(), "subtitle");
    header->addWidget(m_date);
    body->addLayout(header);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(18);

    // Carte bibliothèque
    auto* libCard = makeCard();
    auto* libLayout = static_cast<QVBoxLayout*>(libCard->layout());
    libLayout->addWidget(makeLabel(tr("VOTRE BIBLIOTHÈQUE"), "caption"));
    auto* countRow = new QHBoxLayout;
    m_docCount = makeLabel(QStringLiteral("0"), "big");
    countRow->addWidget(m_docCount);
    countRow->addWidget(makeLabel(tr("documents"), "muted"), 0, Qt::AlignBottom);
    countRow->addStretch(1);
    libLayout->addLayout(countRow);
    m_docHint = makeLabel(QString(), "muted");
    m_docHint->setWordWrap(true);
    libLayout->addWidget(m_docHint);
    libLayout->addStretch(1);
    grid->addWidget(libCard, 0, 0);

    // Carte données locales
    auto* dataCard = makeCard();
    auto* dataLayout = static_cast<QVBoxLayout*>(dataCard->layout());
    dataLayout->addWidget(makeLabel(tr("DONNÉES LOCALES"), "caption"));
    m_dataDir = makeLabel(QString(), "mono");
    m_dataDir->setWordWrap(true);
    dataLayout->addWidget(m_dataDir);
    m_schema = makeLabel(QString(), "muted");
    m_engine = makeLabel(QString(), "muted");
    m_fts = makeLabel(QString());
    dataLayout->addWidget(m_schema);
    dataLayout->addWidget(m_engine);
    dataLayout->addWidget(m_fts);
    auto* openBtn = new QPushButton(tr("Ouvrir le dossier"), dataCard);
    openBtn->setCursor(Qt::PointingHandCursor);
    connect(openBtn, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctx.paths.dataDir())); });
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(openBtn);
    btnRow->addStretch(1);
    dataLayout->addSpacing(4);
    dataLayout->addLayout(btnRow);
    grid->addWidget(dataCard, 0, 1);

    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    body->addLayout(grid);
    body->addStretch(1);

    refresh();
}

void HomePage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
}

void HomePage::refresh()
{
    const QLocale fr(QLocale::French, QLocale::France);
    QString date = fr.toString(QDate::currentDate(), QStringLiteral("dddd d MMMM yyyy"));
    if (!date.isEmpty())
        date[0] = date[0].toUpper();
    m_date->setText(date);

    const auto count = m_ctx.db.queryInt(QStringLiteral("SELECT count(*) FROM documents WHERE deleted_at IS NULL"));
    const qint64 n = count ? count.value() : 0;
    m_docCount->setText(fr.toString(n));
    m_docHint->setText(n == 0 ? tr("Aucun document pour l'instant. L'import de PDF et d'EPUB sera ajouté "
                                   "à l'itération suivante du MVP.")
                              : tr("Documents enregistrés dans votre bibliothèque locale."));

    m_dataDir->setText(QDir::toNativeSeparators(m_ctx.paths.dataDir()));
    const auto version = db::Migrator::currentVersion(m_ctx.db);
    m_schema->setText(tr("Schéma de base : version %1").arg(version ? version.value() : 0));
    m_engine->setText(tr("Moteur : SQLite %1").arg(db::Database::sqliteVersion()));
    const bool fts = m_ctx.db.hasCompileOption(QStringLiteral("ENABLE_FTS5"));
    m_fts->setText(fts ? tr("Recherche plein texte (FTS5) disponible") : tr("Recherche plein texte indisponible"));
    m_fts->setProperty("status", fts ? QStringLiteral("ok") : QStringLiteral("error"));
    m_fts->style()->unpolish(m_fts);
    m_fts->style()->polish(m_fts);
}

}  // namespace studylab::ui
