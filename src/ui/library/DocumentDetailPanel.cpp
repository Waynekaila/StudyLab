#include "ui/library/DocumentDetailPanel.h"

#include "services/library/LibraryService.h"
#include "services/organize/OrganizeService.h"
#include "ui/components/FlowLayout.h"
#include "ui/components/Widgets.h"
#include "ui/library/CoverArt.h"
#include "ui/library/EditDocumentDialog.h"
#include "ui/organize/OrganizeWidgets.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace studylab::ui {

CoverWidget::CoverWidget(ThemeManager& theme, QWidget* parent) : QWidget(parent), m_theme(theme) {}

void CoverWidget::setDocument(const DocumentRecord& r, const QImage& cover)
{
    m_record = r;
    m_cover = cover;
    update();
}

void CoverWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    CoverArt::paintCover(p, QRectF(rect()).adjusted(1, 1, -1, -1), m_record, m_cover, m_theme.palette(),
                         m_theme.scale() * 0.8);
}

namespace {
void clearLayout(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}
}  // namespace

DocumentDetailPanel::DocumentDetailPanel(LibraryService& library, ThemeManager& theme, QWidget* parent)
    : QFrame(parent), m_library(library), m_theme(theme)
{
    setObjectName(QStringLiteral("DetailPanel"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);
    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("DetailContent"));
    scroll->setWidget(content);
    auto* v = new QVBoxLayout(content);
    v->setContentsMargins(20, 24, 20, 20);
    v->setSpacing(14);

    // En-tête : couverture + titre, auteurs
    auto* head = new QHBoxLayout;
    head->setSpacing(14);
    m_cover = new CoverWidget(theme);
    m_cover->setFixedSize(theme.px(92), theme.px(130));
    head->addWidget(m_cover, 0, Qt::AlignTop);
    auto* headText = new QVBoxLayout;
    headText->setSpacing(4);
    m_title = makeLabel(QString(), "h2");
    m_title->setWordWrap(true);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_authors = makeLabel(QString(), "muted");
    m_authors->setWordWrap(true);
    headText->addWidget(m_title);
    headText->addWidget(m_authors);
    headText->addStretch(1);
    head->addLayout(headText, 1);
    v->addLayout(head);

    // Statut + favori
    auto* statusRow = new QHBoxLayout;
    m_status = new QComboBox;
    m_status->setAccessibleName(tr("Statut de lecture"));
    m_status->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_status->setMinimumContentsLength(8);
    m_status->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (ReadingStatus s : allReadingStatuses())
        m_status->addItem(readingStatusLabel(s), int(s));
    connect(m_status, &QComboBox::activated, this, [this](int i) {
        const auto s = ReadingStatus(m_status->itemData(i).toInt());
        if (s != m_record.status && m_library.setStatus(m_record.id, s))
            m_record.status = s;
    });
    m_favorite = new QToolButton;
    m_favorite->setCheckable(true);
    m_favorite->setObjectName(QStringLiteral("FavoriteButton"));
    m_favorite->setAccessibleName(tr("Favori"));
    m_favorite->setCursor(Qt::PointingHandCursor);
    connect(m_favorite, &QToolButton::clicked, this, [this](bool on) {
        if (m_library.setFavorite(m_record.id, on))
            m_record.favorite = on;
        m_favorite->setToolTip(on ? tr("Retirer des favoris") : tr("Ajouter aux favoris"));
        m_favorite->setText(on ? QStringLiteral("★") : QStringLiteral("☆"));
    });
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_favorite);
    v->addLayout(statusRow);

    // Actions (le lecteur EPUB arrive à l'itération 5 : « Lire » n'apparaît que pour les PDF d'ici là)
    auto* actions = new QHBoxLayout;
    m_read = new QPushButton(tr("Lire"));
    m_read->setProperty("variant", QStringLiteral("primary"));
    m_read->setCursor(Qt::PointingHandCursor);
    connect(m_read, &QPushButton::clicked, this, [this] { emit readRequested(m_record.id); });
    m_edit = new QPushButton(tr("Modifier"));
    connect(m_edit, &QPushButton::clicked, this, &DocumentDetailPanel::editDocument);
    m_reveal = new QPushButton;
    m_reveal->setToolTip(tr("Afficher le fichier dans son dossier"));
    m_reveal->setAccessibleName(m_reveal->toolTip());
    connect(m_reveal, &QPushButton::clicked, this, [this] {
        const QString file = m_library.documentFile(m_record);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath()));
    });
    m_reference = new QPushButton(tr("Citer"));
    m_reference->setObjectName(QStringLiteral("DocumentReference"));
    m_reference->setToolTip(tr("Fiche bibliographique : citation [[@clé]] dans les notes, export BibTeX"));
    connect(m_reference, &QPushButton::clicked, this, [this] { emit referenceRequested(m_record.id); });
    m_remove = new QPushButton(tr("Retirer"));
    m_remove->setProperty("variant", QStringLiteral("ghost"));
    connect(m_remove, &QPushButton::clicked, this, &DocumentDetailPanel::removeDocument);
    actions->addWidget(m_read);
    actions->addWidget(m_edit);
    actions->addWidget(m_reveal);
    actions->addWidget(m_reference);
    actions->addStretch(1);
    actions->addWidget(m_remove);
    v->addLayout(actions);
    v->addWidget(makeSeparator());

    m_fields = new QGridLayout;
    m_fields->setHorizontalSpacing(14);
    m_fields->setVerticalSpacing(8);
    m_fields->setColumnStretch(1, 1);
    v->addLayout(m_fields);

    m_tagsCaption = makeLabel(tr("TAGS"), "caption");
    v->addWidget(m_tagsCaption);
    m_tagsBox = new QWidget;
    new FlowLayout(m_tagsBox, 6);
    v->addWidget(m_tagsBox);

    // Matières et collections : visibles une fois le service branché (setOrganize).
    m_orgCaption = makeLabel(tr("MATIÈRES ET COLLECTIONS"), "caption");
    m_orgCaption->hide();
    v->addWidget(m_orgCaption);
    m_orgBox = new QWidget;
    new FlowLayout(m_orgBox, 6);
    m_orgBox->hide();
    v->addWidget(m_orgBox);
    m_orgButtons = new QWidget;
    auto* ob = new QHBoxLayout(m_orgButtons);
    ob->setContentsMargins(0, 0, 0, 0);
    ob->setSpacing(6);
    m_subjectsButton = new QToolButton(m_orgButtons);
    m_collectionsButton = new QToolButton(m_orgButtons);
    for (QToolButton* b : {m_subjectsButton, m_collectionsButton}) {
        b->setObjectName(QStringLiteral("FilterChip"));
        b->setPopupMode(QToolButton::InstantPopup);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCursor(Qt::PointingHandCursor);
        b->setMenu(new QMenu(b));
        ob->addWidget(b);
    }
    m_subjectsButton->setText(tr("Matières"));
    m_subjectsButton->setAccessibleName(tr("Rattacher à des matières"));
    m_collectionsButton->setText(tr("Collections"));
    m_collectionsButton->setAccessibleName(tr("Ajouter à des collections"));
    ob->addStretch(1);
    m_orgButtons->hide();
    v->addWidget(m_orgButtons);

    m_descriptionCaption = makeLabel(tr("DESCRIPTION"), "caption");
    v->addWidget(m_descriptionCaption);
    m_description = makeLabel(QString(), "muted");
    m_description->setWordWrap(true);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(m_description);
    v->addStretch(1);

    connect(&m_library, &LibraryService::documentChanged, this, [this](qint64 id) {
        if (id == m_record.id)
            showDocument(id);
    });
    connect(&theme, &ThemeManager::themeChanged, this, [this] { m_cover->update(); });
}

void DocumentDetailPanel::showDocument(qint64 id)
{
    auto r = m_library.document(id);
    if (!r)
        return;
    m_record = r.value();
    render();
}

void DocumentDetailPanel::addField(const QString& label, const QString& value)
{
    if (value.trimmed().isEmpty())
        return;
    auto* l = makeLabel(label, "faint");
    auto* v = makeLabel(value);
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fields->addWidget(l, m_fieldRow, 0, Qt::AlignTop);
    m_fields->addWidget(v, m_fieldRow, 1);
    ++m_fieldRow;
}

void DocumentDetailPanel::render()
{
    const DocumentRecord& r = m_record;
    QImage cover;
    if (!r.coverPath.isEmpty())
        cover.load(m_library.coverFile(r));
    m_cover->setDocument(r, cover);
    m_title->setText(r.subtitle.isEmpty() ? r.title : r.title + QStringLiteral(" : ") + r.subtitle);
    m_authors->setText(r.authors.isEmpty() ? tr("Auteur inconnu") : r.authors.join(QStringLiteral(", ")));
    m_authors->setVisible(true);
    m_status->setCurrentIndex(m_status->findData(int(r.status)));
    m_favorite->setChecked(r.favorite);
    m_favorite->setText(r.favorite ? QStringLiteral("★") : QStringLiteral("☆"));
    m_favorite->setToolTip(r.favorite ? tr("Retirer des favoris") : tr("Ajouter aux favoris"));

    clearLayout(m_fields);
    m_fieldRow = 0;
    const QLocale fr(QLocale::French, QLocale::France);
    addField(tr("Éditeur"), r.publisher);
    addField(tr("Année"), r.year > 0 ? QString::number(r.year) : QString());
    addField(tr("ISBN"), r.isbn);
    addField(tr("DOI"), r.doi);
    const QString lang = r.language.isEmpty() ? QString() : QLocale(r.language).nativeLanguageName();
    addField(tr("Langue"), lang.isEmpty() ? r.language : lang.left(1).toUpper() + lang.mid(1));
    QString format = documentFormatLabel(r.format);
    if (r.pageCount > 0)
        format += QStringLiteral(" · ") + (r.format == DocumentFormat::Image
                                               ? countLabel(r.pageCount, tr("image"), tr("images"))
                                               : r.format == DocumentFormat::Pdf
                                               ? countLabel(r.pageCount, tr("page"), tr("pages"))
                                               : r.format == DocumentFormat::Epub
                                               ? countLabel(r.pageCount, tr("chapitre"), tr("chapitres"))
                                               : countLabel(r.pageCount, tr("section"), tr("sections")));
    format += QStringLiteral(" · ") + CoverArt::formatFileSize(r.fileSize);
    addField(tr("Format"), format);
    const QString file = m_library.documentFile(r);
    const bool available = QFileInfo::exists(file);
    addField(tr("Fichier"), r.storageMode == StorageMode::Managed
                                ? tr("Copié dans la bibliothèque")
                                : tr("Lié : %1").arg(QDir::toNativeSeparators(file)));
    if (!available)
        addField(QString(), tr("⚠ Fichier introuvable sur le disque"));
    m_reveal->setEnabled(available);
    m_reveal->setIcon(Icons::get(QStringLiteral("folder"), m_theme.palette().text2, 16));
    m_read->setEnabled(available);
    const QDateTime added = QDateTime::fromString(r.addedAt, Qt::ISODate).toLocalTime();
    addField(tr("Ajouté le"), fr.toString(added.date(), QStringLiteral("d MMMM yyyy")));

    clearLayout(m_tagsBox->layout());
    for (const QString& t : r.tags) {
        auto* chip = new QLabel(QStringLiteral("#") + t);
        chip->setProperty("chip", true);
        m_tagsBox->layout()->addWidget(chip);
    }
    m_tagsCaption->setVisible(!r.tags.isEmpty());
    m_tagsBox->setVisible(!r.tags.isEmpty());
    renderOrganize();
    m_descriptionCaption->setVisible(!r.description.isEmpty());
    m_description->setVisible(!r.description.isEmpty());
    m_description->setText(r.description);
}

void DocumentDetailPanel::setOrganize(OrganizeService* organize)
{
    m_organize = organize;
    if (!organize)
        return;
    const Palette& pal = m_theme.palette();
    m_subjectsButton->setIcon(Icons::get(QStringLiteral("folder"), pal.text2, 14));
    m_collectionsButton->setIcon(Icons::get(QStringLiteral("layers"), pal.text2, 14));
    // Menus cochables remplis à l'ouverture : cocher rattache, décocher détache.
    connect(m_subjectsButton->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* menu = m_subjectsButton->menu();
        menu->clear();
        auto all = m_organize->subjects();
        auto mine = m_organize->subjectsOf(OrganizeService::Entity::Document, m_record.id);
        if (!all || all.value().isEmpty()) {
            menu->addAction(tr("Aucune matière : créez-en dans « Matières »"))->setEnabled(false);
            return;
        }
        for (const SubjectRecord& s : all.value()) {
            if (s.archived)
                continue;
            QAction* a = menu->addAction(colorDot(m_theme.palette(), s.color, 12), m_organize->subjectPath(s.id, all.value()));
            a->setCheckable(true);
            a->setChecked(mine && mine.value().contains(s.id));
            const qint64 sid = s.id;
            connect(a, &QAction::toggled, this, [this, sid](bool on) {
                if (on)
                    (void)m_organize->attach(sid, OrganizeService::Entity::Document, m_record.id);
                else
                    (void)m_organize->detach(sid, OrganizeService::Entity::Document, m_record.id);
                renderOrganize();
            });
        }
    });
    connect(m_collectionsButton->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* menu = m_collectionsButton->menu();
        menu->clear();
        auto all = m_organize->collections();
        auto mine = m_organize->collectionsOf(OrganizeService::Entity::Document, m_record.id);
        int n = 0;
        for (const CollectionRecord& c : all ? all.value() : QList<CollectionRecord>{}) {
            if (c.kind == CollectionKind::Smart)
                continue;  // se remplit d'après ses règles
            ++n;
            QAction* a = menu->addAction(Icons::get(collectionKindIcon(c.kind), m_theme.palette().text2, 14), c.name);
            a->setCheckable(true);
            a->setChecked(mine && mine.value().contains(c.id));
            const qint64 cid = c.id;
            connect(a, &QAction::toggled, this, [this, cid](bool on) {
                if (on)
                    (void)m_organize->addToCollection(cid, OrganizeService::Entity::Document, m_record.id);
                else
                    (void)m_organize->removeFromCollection(cid, OrganizeService::Entity::Document, m_record.id);
                renderOrganize();
            });
        }
        if (n == 0)
            menu->addAction(tr("Aucune collection : créez-en dans « Collections »"))->setEnabled(false);
    });
    connect(m_organize, &OrganizeService::subjectsChanged, this, &DocumentDetailPanel::renderOrganize);
    connect(m_organize, &OrganizeService::collectionsChanged, this, &DocumentDetailPanel::renderOrganize);
    renderOrganize();
}

void DocumentDetailPanel::renderOrganize()
{
    if (!m_organize)
        return;
    clearLayout(m_orgBox->layout());
    const bool shown = m_record.id > 0;
    m_orgCaption->setVisible(shown);
    m_orgButtons->setVisible(shown);
    if (!shown) {
        m_orgBox->hide();
        return;
    }
    const Palette& pal = m_theme.palette();
    int chips = 0;
    auto subjects = m_organize->subjects();
    auto mine = m_organize->subjectsOf(OrganizeService::Entity::Document, m_record.id);
    if (subjects && mine) {
        for (const SubjectRecord& s : subjects.value()) {
            if (!mine.value().contains(s.id))
                continue;
            auto* chip = new QLabel;
            chip->setProperty("chip", true);
            chip->setText(QStringLiteral("<span style=\"color:%1\">●</span> %2")
                              .arg(organizeColor(pal, s.color).name(), s.name.toHtmlEscaped()));
            chip->setToolTip(m_organize->subjectPath(s.id, subjects.value()));
            m_orgBox->layout()->addWidget(chip);
            ++chips;
        }
    }
    auto collections = m_organize->collections();
    auto in = m_organize->collectionsOf(OrganizeService::Entity::Document, m_record.id);
    if (collections && in) {
        for (const CollectionRecord& c : collections.value()) {
            if (!in.value().contains(c.id))
                continue;
            auto* chip = new QLabel(c.name);
            chip->setProperty("chip", true);
            chip->setToolTip(collectionKindLabel(c.kind));
            m_orgBox->layout()->addWidget(chip);
            ++chips;
        }
    }
    m_orgBox->setVisible(chips > 0);
    m_orgBox->layout()->invalidate();
}

void DocumentDetailPanel::editDocument()
{
    EditDocumentDialog dlg(m_record, m_library.tags(), window());
    if (dlg.exec() != QDialog::Accepted)
        return;
    DocumentRecord updated = dlg.result();
    if (auto r = m_library.save(updated); !r) {
        QMessageBox::warning(window(), tr("Modification impossible"), r.error().message);
        return;
    }
    showDocument(updated.id);
}

void DocumentDetailPanel::removeDocument()
{
    QMessageBox box(QMessageBox::Question, tr("Retirer le document"),
                    tr("Retirer « %1 » de la bibliothèque ?").arg(m_record.title), QMessageBox::NoButton, window());
    box.setInformativeText(tr("Le fichier n'est pas supprimé du disque. Vous pourrez l'importer de nouveau."));
    QPushButton* yes = box.addButton(tr("Retirer"), QMessageBox::AcceptRole);
    box.addButton(tr("Annuler"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != yes)
        return;
    const qint64 id = m_record.id;
    if (auto r = m_library.remove(id); !r) {
        QMessageBox::warning(window(), tr("Retrait impossible"), r.error().message);
        return;
    }
    emit removed(id);
}

}  // namespace studylab::ui
