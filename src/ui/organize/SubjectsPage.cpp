#include "ui/organize/SubjectsPage.h"

#include "services/annotations/AnnotationService.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/Widgets.h"
#include "ui/notes/NoteExporter.h"
#include "ui/organize/OrganizeWidgets.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kOverviewLimit = 5;

void clearRows(QVBoxLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

}  // namespace

SubjectsPage::SubjectsPage(AppContext& ctx, QWidget* parent) : QWidget(parent), m_ctx(ctx)
{
    setObjectName(QStringLiteral("SubjectsPage"));
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    buildNav();
    buildPage();

    // Rafraîchi à l'affichage si quelque chose a changé ailleurs (bibliothèque, notes, rattachements).
    const auto changed = [this] {
        m_dirty = true;
        if (isVisible()) {
            refreshTree();
            refreshPage();
        }
    };
    connect(&m_ctx.organize, &OrganizeService::subjectsChanged, this, changed);
    connect(&m_ctx.organize, &OrganizeService::membershipChanged, this, changed);
    connect(&m_ctx.library, &LibraryService::documentsChanged, this, changed);
    connect(&m_ctx.notes, &NoteService::noteChanged, this, changed);
    connect(&m_ctx.notes, &NoteService::noteRemoved, this, changed);
    connect(&m_ctx.theme, &ThemeManager::themeChanged, this, [this] {
        refreshTree();
        refreshPage();
    });
    refreshTree();
    refreshPage();
}

void SubjectsPage::buildNav()
{
    ThemeManager& theme = m_ctx.theme;
    auto* nav = new QFrame(this);
    nav->setObjectName(QStringLiteral("NotesNav"));
    nav->setFixedWidth(theme.px(280));
    auto* v = new QVBoxLayout(nav);
    v->setContentsMargins(12, 14, 12, 12);
    v->setSpacing(10);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(6, 0, 0, 0);
    head->addWidget(makeLabel(tr("Matières"), "h2", nav), 1);
    auto* add = new QToolButton(nav);
    add->setObjectName(QStringLiteral("NewSubjectButton"));
    add->setProperty("readerTool", true);
    add->setIcon(Icons::get(QStringLiteral("plus"), theme.palette().text2, 18));
    add->setToolTip(tr("Nouvelle matière"));
    add->setAccessibleName(tr("Nouvelle matière"));
    connect(add, &QToolButton::clicked, this, [this] { promptNewSubject(0); });
    head->addWidget(add);
    v->addLayout(head);

    m_tree = new DragTree(nav);
    m_tree->setObjectName(QStringLiteral("NoteTree"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setIndentation(theme.px(16));
    m_tree->setAccessibleName(tr("Matières"));
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIconSize(QSize(10, 10));
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item)
            showSubject(item->data(0, kIdRole).toLongLong());
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_tree->itemAt(pos);
        if (!item)
            return;
        if (QMenu* menu = contextMenuFor(item->data(0, kIdRole).toLongLong()))
            menu->popup(m_tree->viewport()->mapToGlobal(pos));
    });
    connect(m_tree, &DragTree::itemMoved, this, &SubjectsPage::onItemMoved);
    v->addWidget(m_tree, 1);

    auto* hint = makeLabel(tr("Glisser pour réordonner ou ranger · clic droit pour couleur, archivage"), "faint", nav);
    hint->setWordWrap(true);
    v->addWidget(hint);
    m_archivedToggle = new QToolButton(nav);
    m_archivedToggle->setObjectName(QStringLiteral("FilterChip"));
    m_archivedToggle->setCheckable(true);
    m_archivedToggle->setCursor(Qt::PointingHandCursor);
    connect(m_archivedToggle, &QToolButton::toggled, this, [this](bool on) {
        m_showArchived = on;
        refreshTree();
    });
    v->addWidget(m_archivedToggle, 0, Qt::AlignLeft);
    layout()->addWidget(nav);
}

QWidget* SubjectsPage::listCard(const QString& title, QLabel** count, QVBoxLayout** list, QLabel** empty)
{
    auto* card = makeCard();
    auto* l = static_cast<QVBoxLayout*>(card->layout());
    auto* head = new QHBoxLayout;
    head->addWidget(makeLabel(title, "h3"), 1);
    *count = makeLabel(QStringLiteral("0"), nullptr);
    (*count)->setObjectName(QStringLiteral("CountBadge"));
    head->addWidget(*count);
    l->addLayout(head);
    *list = new QVBoxLayout;
    (*list)->setSpacing(2);
    l->addLayout(*list);
    *empty = makeLabel(QString(), "muted");
    (*empty)->setWordWrap(true);
    l->addWidget(*empty);
    l->addStretch(1);
    return card;
}

void SubjectsPage::buildPage()
{
    ThemeManager& theme = m_ctx.theme;
    m_center = new QStackedWidget(this);

    m_empty = new QWidget(m_center);
    auto* ev = new QVBoxLayout(m_empty);
    ev->addStretch(1);
    auto* et = makeLabel(tr("Rangez vos documents et vos notes par matière"), "h2", m_empty);
    et->setAlignment(Qt::AlignCenter);
    ev->addWidget(et);
    auto* ed = makeLabel(tr("Créez une matière (« Physique », puis « Thermodynamique » dedans), puis rattachez-lui des "
                            "documents depuis la bibliothèque ou des notes depuis « Mes notes »."),
                         "muted", m_empty);
    ed->setWordWrap(true);
    ed->setAlignment(Qt::AlignCenter);
    ed->setMaximumWidth(theme.px(460));
    ev->addWidget(ed, 0, Qt::AlignHCenter);
    auto* create = new QPushButton(tr("Nouvelle matière"), m_empty);
    create->setProperty("variant", QStringLiteral("primary"));
    create->setCursor(Qt::PointingHandCursor);
    connect(create, &QPushButton::clicked, this, [this] { promptNewSubject(0); });
    ev->addWidget(create, 0, Qt::AlignHCenter);
    ev->addStretch(2);
    m_center->addWidget(m_empty);

    auto* scroll = new QScrollArea(m_center);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_page = new QWidget(scroll);
    m_page->setObjectName(QStringLiteral("SubjectPageBody"));
    auto* v = new QVBoxLayout(m_page);
    v->setContentsMargins(0, 0, 0, 24);
    v->setSpacing(0);

    // En-tête doux à la couleur de la matière
    m_band = new QFrame(m_page);
    m_band->setObjectName(QStringLiteral("SubjectBand"));
    m_band->setFixedHeight(theme.px(56));
    v->addWidget(m_band);
    auto* body = new QVBoxLayout;
    body->setContentsMargins(theme.px(36), 0, theme.px(36), 0);
    body->setSpacing(18);
    auto* head = new QHBoxLayout;
    head->setSpacing(16);
    m_badge = new QLabel(m_page);
    m_badge->setObjectName(QStringLiteral("SubjectBadge"));
    m_badge->setFixedSize(theme.px(64), theme.px(64));
    m_badge->setAlignment(Qt::AlignCenter);
    head->addWidget(m_badge, 0, Qt::AlignTop);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addSpacing(theme.px(18));
    m_title = makeLabel(QString(), "h1", m_page);
    m_title->setObjectName(QStringLiteral("SubjectTitle"));
    m_path = makeLabel(QString(), "subtitle", m_page);
    titles->addWidget(m_title);
    titles->addWidget(m_path);
    head->addLayout(titles, 1);
    body->addSpacing(-theme.px(32));
    body->addLayout(head);

    // Chiffres clés
    auto* stats = new QHBoxLayout;
    stats->setSpacing(14);
    const QStringList captions{tr("documents"), tr("annotations"), tr("notes"), tr("lu en moyenne")};
    for (int i = 0; i < captions.size(); ++i) {
        auto* card = makeCard(m_page);
        card->setObjectName(QStringLiteral("StatTile"));
        auto* l = static_cast<QVBoxLayout*>(card->layout());
        l->setSpacing(2);
        auto* value = makeLabel(QStringLiteral("0"), "big", card);
        m_statValues << value;
        l->addWidget(value);
        l->addWidget(makeLabel(captions.at(i), "muted", card));
        if (i == 3) {
            m_progress = new QProgressBar(card);
            m_progress->setRange(0, 100);
            m_progress->setTextVisible(false);
            m_progress->setFixedHeight(theme.px(6));
            l->addWidget(m_progress);
        }
        stats->addWidget(card, 1);
    }
    body->addLayout(stats);

    m_tabs = new SegmentedControl({tr("Vue d'ensemble"), tr("Documents"), tr("Notes")}, m_page);
    m_tabs->setObjectName(QStringLiteral("SubjectTabs"));
    m_tabs->setCurrentIndex(0);
    connect(m_tabs, &SegmentedControl::currentIndexChanged, this, [this](int i) { setTab(Tab(i)); });
    body->addWidget(m_tabs, 0, Qt::AlignLeft);

    auto* lists = new QHBoxLayout;
    lists->setSpacing(18);
    m_docCard = listCard(tr("Documents"), &m_docCount, &m_docList, &m_docEmpty);
    m_noteCard = listCard(tr("Notes"), &m_noteCount, &m_noteList, &m_noteEmpty);
    m_docEmpty->setText(tr("Aucun document. Dans la bibliothèque, ouvrez le panneau d'un document et choisissez "
                           "« Matières »."));
    m_noteEmpty->setText(tr("Aucune note. Dans « Mes notes », clic droit sur une note puis « Matières »."));
    lists->addWidget(m_docCard, 1);
    lists->addWidget(m_noteCard, 1);
    body->addLayout(lists);
    body->addStretch(1);
    v->addLayout(body);
    scroll->setWidget(m_page);
    m_center->addWidget(scroll);
    layout()->addWidget(m_center);
}

void SubjectsPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_dirty) {
        refreshTree();
        refreshPage();
    }
}

QTreeWidgetItem* SubjectsPage::itemFor(qint64 id) const
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->data(0, kIdRole).toLongLong() == id)
            return *it;
    return nullptr;
}

void SubjectsPage::refreshTree()
{
    auto subjects = m_ctx.organize.subjects();
    auto counts = m_ctx.organize.subjectCounts();
    const QList<SubjectRecord> all = subjects ? subjects.value() : QList<SubjectRecord>{};
    const QHash<qint64, int> n = counts ? counts.value() : QHash<qint64, int>{};
    const Palette& pal = m_ctx.theme.palette();

    int archived = 0;
    for (const SubjectRecord& s : all)
        archived += s.archived;
    m_archivedToggle->setText(tr("Matières archivées (%1)").arg(archived));
    m_archivedToggle->setVisible(archived > 0);
    if (archived == 0)
        m_showArchived = false;

    const QSignalBlocker block(m_tree);
    m_tree->clear();
    QHash<qint64, QTreeWidgetItem*> items;
    // Parents d'abord : on ajoute par niveaux successifs.
    QList<SubjectRecord> pending = all;
    while (!pending.isEmpty()) {
        const qsizetype before = pending.size();
        for (qsizetype i = 0; i < pending.size();) {
            const SubjectRecord& s = pending.at(i);
            if (s.parentId > 0 && !items.contains(s.parentId) && std::any_of(pending.cbegin(), pending.cend(), [&](const SubjectRecord& o) { return o.id == s.parentId; })) {
                ++i;
                continue;
            }
            if (s.archived && !m_showArchived) {
                pending.removeAt(i);
                continue;
            }
            QTreeWidgetItem* parent = items.value(s.parentId);
            auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
            item->setText(0, s.name);
            item->setIcon(0, colorDot(pal, s.color));
            item->setText(1, QString::number(n.value(s.id)));
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            item->setForeground(1, pal.text3);
            if (s.archived)
                item->setForeground(0, pal.text3);
            item->setData(0, kIdRole, s.id);
            item->setToolTip(0, s.description.isEmpty() ? s.name : s.description);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            items.insert(s.id, item);
            pending.removeAt(i);
        }
        if (pending.size() == before)
            break;  // parent archivé masqué : ses sous-matières aussi
    }
    m_tree->expandAll();
    if (QTreeWidgetItem* cur = items.value(m_currentId))
        m_tree->setCurrentItem(cur);
    m_dirty = false;
    if (m_currentId > 0 && !std::any_of(all.cbegin(), all.cend(), [this](const SubjectRecord& s) { return s.id == m_currentId; }))
        m_currentId = 0;
    if (m_currentId == 0 && m_tree->topLevelItemCount() > 0) {
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
        m_currentId = m_tree->topLevelItem(0)->data(0, kIdRole).toLongLong();
    }
}

void SubjectsPage::showSubject(qint64 id)
{
    if (id == m_currentId && m_center->currentIndex() == 1)
        return;
    m_currentId = id;
    if (QTreeWidgetItem* item = itemFor(id)) {
        const QSignalBlocker block(m_tree);
        m_tree->setCurrentItem(item);
    }
    refreshPage();
}

void SubjectsPage::setTab(Tab tab)
{
    m_tab = tab;
    m_tabs->setCurrentIndex(int(tab));
    fillLists();
}

void SubjectsPage::refreshPage()
{
    auto all = m_ctx.organize.subjects();
    const QList<SubjectRecord> subjects = all ? all.value() : QList<SubjectRecord>{};
    const auto it = std::find_if(subjects.cbegin(), subjects.cend(), [this](const SubjectRecord& s) { return s.id == m_currentId; });
    if (it == subjects.cend()) {
        m_center->setCurrentWidget(m_empty);
        return;
    }
    m_center->setCurrentIndex(1);
    const SubjectRecord& s = *it;
    const Palette& pal = m_ctx.theme.palette();
    const QColor color = organizeColor(pal, s.color);
    QColor soft = color;
    soft.setAlphaF(pal.dark ? 0.22 : 0.16);
    m_band->setStyleSheet(QStringLiteral("QFrame#SubjectBand { background: %1; }").arg(soft.name(QColor::HexArgb)));
    m_badge->setStyleSheet(QStringLiteral("QLabel#SubjectBadge { background: %1; border-radius: %2px; }")
                               .arg(color.name(), QString::number(m_ctx.theme.px(14))));
    m_badge->setPixmap(Icons::get(QStringLiteral("folder"), pal.onPrimary, 28).pixmap(28, 28));
    m_title->setText(s.name);
    QString path = m_ctx.organize.subjectPath(s.id, subjects);
    if (s.archived)
        path += tr(" · archivée");
    m_path->setText(path);
    m_path->setVisible(path != s.name);

    auto st = m_ctx.organize.subjectStats(s.id);
    const SubjectStats stats = st ? st.value() : SubjectStats{};
    m_statValues.at(0)->setText(QString::number(stats.documents));
    m_statValues.at(1)->setText(QString::number(stats.annotations));
    m_statValues.at(2)->setText(QString::number(stats.notes));
    m_statValues.at(3)->setText(QStringLiteral("%1 %").arg(qRound(stats.progress * 100)));
    m_progress->setValue(qRound(stats.progress * 100));
    fillLists();
}

void SubjectsPage::fillLists()
{
    clearRows(m_docList);
    clearRows(m_noteList);
    m_documentRows.clear();
    m_noteRows.clear();
    if (m_currentId <= 0)
        return;
    auto docs = m_ctx.organize.subjectItems(m_currentId, OrganizeService::Entity::Document);
    auto notes = m_ctx.organize.subjectItems(m_currentId, OrganizeService::Entity::Note);
    const QList<qint64> docIds = docs ? docs.value() : QList<qint64>{};
    const QList<qint64> noteIds = notes ? notes.value() : QList<qint64>{};
    m_docCount->setText(QString::number(docIds.size()));
    m_noteCount->setText(QString::number(noteIds.size()));
    m_docCard->setVisible(m_tab != Tab::Notes);
    m_noteCard->setVisible(m_tab != Tab::Documents);
    // Seule la liste affichée est construite.
    const int docLimit = m_tab == Tab::Notes ? 0 : m_tab == Tab::Overview ? kOverviewLimit : int(docIds.size());
    const int noteLimit = m_tab == Tab::Documents ? 0 : m_tab == Tab::Overview ? kOverviewLimit + 1 : int(noteIds.size());

    for (int i = 0; i < docIds.size() && i < docLimit; ++i) {
        auto doc = m_ctx.library.document(docIds.at(i));
        if (!doc)
            continue;
        auto count = m_ctx.annotations.count(doc.value().id);
        auto* row = new DocumentRow(m_ctx.theme, doc.value(), count ? count.value() : 0, m_docCard);
        const qint64 id = doc.value().id;
        connect(row, &QPushButton::clicked, this, [this, id] { emit openDocumentRequested(id); });
        m_docList->addWidget(row);
        m_documentRows << row;
    }
    for (int i = 0; i < noteIds.size() && i < noteLimit; ++i) {
        auto note = m_ctx.notes.find(noteIds.at(i));
        if (!note)
            continue;
        auto* row = new NoteRow(m_ctx.theme, note.value(), m_noteCard);
        const qint64 id = note.value().id;
        connect(row, &QPushButton::clicked, this, [this, id] { emit openNoteRequested(id); });
        m_noteList->addWidget(row);
        m_noteRows << row;
    }
    if (m_tab == Tab::Overview && docIds.size() > docLimit) {
        auto* more = new QPushButton(tr("Voir les %1 documents").arg(docIds.size()), m_docCard);
        more->setProperty("variant", QStringLiteral("ghost"));
        connect(more, &QPushButton::clicked, this, [this] { setTab(Tab::Documents); });
        m_docList->addWidget(more, 0, Qt::AlignLeft);
    }
    if (m_tab == Tab::Overview && noteIds.size() > noteLimit) {
        auto* more = new QPushButton(tr("Voir les %1 notes").arg(noteIds.size()), m_noteCard);
        more->setProperty("variant", QStringLiteral("ghost"));
        connect(more, &QPushButton::clicked, this, [this] { setTab(Tab::Notes); });
        m_noteList->addWidget(more, 0, Qt::AlignLeft);
    }
    m_docEmpty->setVisible(docIds.isEmpty());
    m_noteEmpty->setVisible(noteIds.isEmpty());
}

qint64 SubjectsPage::createSubject(const QString& name, qint64 parentId)
{
    auto created = m_ctx.organize.createSubject(name, parentId);
    if (!created) {
        QMessageBox::warning(this, tr("Nouvelle matière"), created.error().message);
        return 0;
    }
    m_currentId = created.value().id;
    refreshTree();
    refreshPage();
    return created.value().id;
}

void SubjectsPage::promptNewSubject(qint64 parentId)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, parentId > 0 ? tr("Nouvelle sous-matière") : tr("Nouvelle matière"),
                                               tr("Nom :"), QLineEdit::Normal, QString(), &ok);
    if (ok && !name.trimmed().isEmpty())
        createSubject(name, parentId);
}

void SubjectsPage::promptRename(qint64 id)
{
    auto s = m_ctx.organize.subject(id);
    if (!s)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Renommer la matière"), tr("Nom :"), QLineEdit::Normal,
                                               s.value().name, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    if (auto r = m_ctx.organize.renameSubject(id, name); !r)
        QMessageBox::warning(this, tr("Renommer la matière"), r.error().message);
}

void SubjectsPage::confirmRemove(qint64 id)
{
    auto s = m_ctx.organize.subject(id);
    if (!s)
        return;
    const auto answer = QMessageBox::question(
        this, tr("Supprimer la matière"),
        tr("Supprimer « %1 » ? Les documents et les notes restent dans la bibliothèque ; ses sous-matières remontent "
           "d'un niveau.").arg(s.value().name));
    if (answer != QMessageBox::Yes)
        return;
    if (auto r = m_ctx.organize.removeSubject(id); !r)
        QMessageBox::warning(this, tr("Supprimer la matière"), r.error().message);
}

QList<qint64> SubjectsPage::subjectNoteIds(qint64 id)
{
    auto items = m_ctx.organize.subjectItems(id, OrganizeService::Entity::Note);
    if (!items)
        return {};
    QList<NoteRecord> notes;
    for (qint64 n : items.value())
        if (auto r = m_ctx.notes.find(n))
            notes << r.value();
    std::sort(notes.begin(), notes.end(), [](const NoteRecord& a, const NoteRecord& b) {
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    QList<qint64> ids;
    for (const NoteRecord& n : std::as_const(notes))
        ids << n.id;
    return ids;
}

NoteExporter& SubjectsPage::exporter()
{
    if (!m_exporter)
        m_exporter = new NoteExporter(m_ctx, this);
    return *m_exporter;
}

QMenu* SubjectsPage::contextMenuFor(qint64 id)
{
    auto s = m_ctx.organize.subject(id);
    if (!s)
        return nullptr;
    delete m_menu;
    m_menu = new QMenu(this);
    const SubjectRecord subject = s.value();
    const Palette& pal = m_ctx.theme.palette();
    m_menu->addAction(Icons::get(QStringLiteral("plus"), pal.text2, 16), tr("Nouvelle sous-matière…"), this,
                      [this, id] { promptNewSubject(id); });
    m_menu->addAction(tr("Renommer…"), this, [this, id] { promptRename(id); });
    QMenu* colors = m_menu->addMenu(Icons::get(QStringLiteral("palette"), pal.text2, 16), tr("Couleur"));
    for (const QString& key : organizeColorKeys()) {
        QAction* a = colors->addAction(colorDot(pal, key, 12), organizeColorLabel(key));
        a->setCheckable(true);
        a->setChecked(key == subject.color);
        a->setObjectName(QStringLiteral("color-") + key);
        connect(a, &QAction::triggered, this, [this, id, key] { (void)m_ctx.organize.setSubjectColor(id, key); });
    }
    if (subject.parentId > 0)
        m_menu->addAction(tr("Placer au premier niveau"), this, [this, id] { (void)m_ctx.organize.moveSubject(id, 0); });
    m_menu->addSeparator();
    const QList<qint64> notes = subjectNoteIds(id);
    QAction* pdf = m_menu->addAction(Icons::get(QStringLiteral("download"), pal.text2, 16), tr("Exporter les notes en PDF…"),
                                     this, [this, notes, name = subject.name] {
                                         exporter().exportWithDialog(this, notes, name, name, NoteExporter::Format::Pdf);
                                     });
    pdf->setObjectName(QStringLiteral("export-pdf"));
    pdf->setEnabled(!notes.isEmpty());
    QAction* html = m_menu->addAction(tr("Exporter les notes en HTML…"), this, [this, notes, name = subject.name] {
        exporter().exportWithDialog(this, notes, name, name, NoteExporter::Format::Html);
    });
    html->setObjectName(QStringLiteral("export-html"));
    html->setEnabled(!notes.isEmpty());
    m_menu->addSeparator();
    QAction* archive = m_menu->addAction(subject.archived ? tr("Désarchiver") : tr("Archiver"), this,
                                         [this, id, archived = subject.archived] {
                                             (void)m_ctx.organize.setSubjectArchived(id, !archived);
                                         });
    archive->setObjectName(QStringLiteral("archive"));
    QAction* remove = m_menu->addAction(Icons::get(QStringLiteral("trash"), pal.danger, 16), tr("Supprimer…"), this,
                                        [this, id] { confirmRemove(id); });
    remove->setObjectName(QStringLiteral("remove"));
    return m_menu;
}

void SubjectsPage::onItemMoved(QTreeWidgetItem* item, QTreeWidgetItem* newParent, int index)
{
    const qint64 id = item->data(0, kIdRole).toLongLong();
    const qint64 parentId = newParent ? newParent->data(0, kIdRole).toLongLong() : 0;
    if (auto r = m_ctx.organize.moveSubject(id, parentId, index); !r)
        QMessageBox::warning(this, tr("Déplacer la matière"), r.error().message);
    refreshTree();
}

}  // namespace studylab::ui
