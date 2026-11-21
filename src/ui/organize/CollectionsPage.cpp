#include "ui/organize/CollectionsPage.h"

#include "services/annotations/AnnotationService.h"
#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"
#include "ui/components/PanelSplitter.h"
#include "ui/components/Widgets.h"
#include "ui/organize/OrganizeWidgets.h"
#include "ui/organize/SmartCollectionDialog.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QHeaderView>
#include <QTreeWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {

constexpr int kIdRole = Qt::UserRole;

void clearRows(QVBoxLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

QColor kindColor(const Palette& p, CollectionKind k)
{
    return k == CollectionKind::Smart ? p.accPurple : p.primary;
}

}  // namespace

CollectionsPage::CollectionsPage(AppContext& ctx, QWidget* parent) : QWidget(parent), m_ctx(ctx)
{
    setObjectName(QStringLiteral("CollectionsPage"));
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    m_panels = new PanelSplitter(ctx.settings, ctx.theme, QStringLiteral("collections"), this);
    h->addWidget(m_panels, 1);
    buildNav();
    buildPage();
    const auto changed = [this] {
        m_dirty = true;
        if (isVisible()) {
            refreshList();
            refreshPage();
        }
    };
    connect(&m_ctx.organize, &OrganizeService::collectionsChanged, this, changed);
    connect(&m_ctx.organize, &OrganizeService::membershipChanged, this, changed);
    connect(&m_ctx.organize, &OrganizeService::subjectsChanged, this, changed);
    connect(&m_ctx.library, &LibraryService::documentsChanged, this, changed);
    connect(&m_ctx.notes, &NoteService::noteChanged, this, changed);
    connect(&m_ctx.notes, &NoteService::noteRemoved, this, changed);
    connect(&m_ctx.theme, &ThemeManager::themeChanged, this, [this] {
        refreshList();
        refreshPage();
    });
    refreshList();
    refreshPage();
}

void CollectionsPage::buildNav()
{
    ThemeManager& theme = m_ctx.theme;
    auto* nav = new QFrame(this);
    nav->setObjectName(QStringLiteral("NotesNav"));
    auto* v = new QVBoxLayout(nav);
    v->setContentsMargins(12, 14, 12, 12);
    v->setSpacing(10);
    auto* head = new QHBoxLayout;
    head->setContentsMargins(6, 0, 0, 0);
    head->addWidget(makeLabel(tr("Collections"), "h2", nav), 1);
    m_new = new QToolButton(nav);
    m_new->setObjectName(QStringLiteral("NewCollectionButton"));
    m_new->setProperty("readerTool", true);
    m_new->setIcon(Icons::get(QStringLiteral("plus"), theme.palette().text2, 18));
    m_new->setToolTip(tr("Nouvelle collection"));
    m_new->setAccessibleName(tr("Nouvelle collection"));
    m_new->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(m_new);
    for (CollectionKind k : {CollectionKind::Manual, CollectionKind::ReadingList, CollectionKind::Project, CollectionKind::Smart}) {
        QAction* a = menu->addAction(Icons::get(collectionKindIcon(k), kindColor(theme.palette(), k), 16),
                                     collectionKindLabel(k) + (k == CollectionKind::Smart ? tr("…") : QString()));
        a->setObjectName(QStringLiteral("new-") + collectionKindKey(k));
        connect(a, &QAction::triggered, this, [this, k] {
            if (k == CollectionKind::Smart)
                editSmartCollection(0);
            else
                promptNew(k);
        });
    }
    m_new->setMenu(menu);
    head->addWidget(m_new);
    m_navHead = head;
    v->addLayout(head);

    m_list = new QTreeWidget(nav);
    m_list->setObjectName(QStringLiteral("NoteTree"));
    m_list->setAccessibleName(tr("Collections"));
    m_list->setColumnCount(2);
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setIndentation(0);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->setIconSize(QSize(16, 16));
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item)
            showCollection(item->data(0, kIdRole).toLongLong());
    });
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_list->itemAt(pos);
        if (!item)
            return;
        const qint64 id = item->data(0, kIdRole).toLongLong();
        auto c = m_ctx.organize.collection(id);
        if (!c)
            return;
        auto* popup = new QMenu(this);
        popup->setAttribute(Qt::WA_DeleteOnClose);
        if (c.value().kind == CollectionKind::Smart)
            popup->addAction(tr("Modifier les règles…"), this, [this, id] { editSmartCollection(id); });
        popup->addAction(tr("Renommer…"), this, [this, id] { promptRename(id); });
        popup->addSeparator();
        popup->addAction(Icons::get(QStringLiteral("trash"), m_ctx.theme.palette().danger, 16), tr("Supprimer…"), this,
                         [this, id] { confirmRemove(id); });
        popup->popup(m_list->viewport()->mapToGlobal(pos));
    });
    v->addWidget(m_list, 1);
    auto* hint = makeLabel(tr("Collections intelligentes : recalculées d'après leurs règles à chaque affichage."),
                           "faint", nav);
    hint->setWordWrap(true);
    v->addWidget(hint);
    m_panels->addPanel(nav, tr("Collections"), theme.px(260), theme.px(200), theme.px(440));
    if (m_navHead)
        m_navHead->addWidget(m_panels->makeCollapseButton(nav, nav));
}

void CollectionsPage::buildPage()
{
    ThemeManager& theme = m_ctx.theme;
    m_center = new QStackedWidget(this);

    m_empty = new QWidget(m_center);
    auto* ev = new QVBoxLayout(m_empty);
    ev->addStretch(1);
    auto* et = makeLabel(tr("Regroupez des lectures autour d'un objectif"), "h2", m_empty);
    et->setAlignment(Qt::AlignCenter);
    ev->addWidget(et);
    auto* ed = makeLabel(tr("Une collection rassemble des documents et des notes (« Prépa examen S5 ») ; une liste de "
                            "lecture les garde dans l'ordre ; une collection intelligente se remplit seule d'après "
                            "des règles (« Statut n'est pas Terminé »)."),
                         "muted", m_empty);
    ed->setWordWrap(true);
    ed->setAlignment(Qt::AlignCenter);
    ed->setMaximumWidth(theme.px(480));
    // Centré par une rangée, pas par un alignement : un texte replié aligné perd sa hauteur et sa fin est coupée.
    auto* edRow = new QHBoxLayout;
    edRow->addStretch(1);
    edRow->addWidget(ed, 6);
    edRow->addStretch(1);
    ev->addLayout(edRow);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* manual = new QPushButton(tr("Nouvelle collection"), m_empty);
    manual->setProperty("variant", QStringLiteral("primary"));
    connect(manual, &QPushButton::clicked, this, [this] { promptNew(CollectionKind::Manual); });
    buttons->addWidget(manual);
    auto* smart = new QPushButton(tr("Collection intelligente…"), m_empty);
    connect(smart, &QPushButton::clicked, this, [this] { editSmartCollection(0); });
    buttons->addWidget(smart);
    buttons->addStretch(1);
    ev->addLayout(buttons);
    ev->addStretch(2);
    m_center->addWidget(m_empty);

    auto* scroll = new QScrollArea(m_center);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_page = new QWidget(scroll);
    m_page->setObjectName(QStringLiteral("CollectionPageBody"));
    auto* v = new QVBoxLayout(m_page);
    v->setContentsMargins(theme.px(36), theme.px(28), theme.px(36), theme.px(24));
    v->setSpacing(16);

    auto* head = new QHBoxLayout;
    head->setSpacing(16);
    m_badge = new QLabel(m_page);
    m_badge->setObjectName(QStringLiteral("SubjectBadge"));
    m_badge->setFixedSize(theme.px(56), theme.px(56));
    m_badge->setAlignment(Qt::AlignCenter);
    head->addWidget(m_badge, 0, Qt::AlignTop);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    m_title = makeLabel(QString(), "h1", m_page);
    m_title->setObjectName(QStringLiteral("CollectionTitle"));
    m_kind = makeLabel(QString(), "subtitle", m_page);
    titles->addWidget(m_title);
    titles->addWidget(m_kind);
    head->addLayout(titles, 1);
    m_editRules = new QPushButton(tr("Modifier les règles"), m_page);
    m_editRules->setIcon(Icons::get(QStringLiteral("sparkle"), theme.palette().accPurple, 16));
    connect(m_editRules, &QPushButton::clicked, this, [this] { editSmartCollection(m_currentId); });
    head->addWidget(m_editRules, 0, Qt::AlignTop);
    m_more = new QToolButton(m_page);
    m_more->setProperty("readerTool", true);
    m_more->setIcon(Icons::get(QStringLiteral("settings"), theme.palette().text2, 18));
    m_more->setToolTip(tr("Renommer ou supprimer"));
    m_more->setAccessibleName(tr("Actions de la collection"));
    m_more->setPopupMode(QToolButton::InstantPopup);
    auto* moreMenu = new QMenu(m_more);
    moreMenu->addAction(tr("Renommer…"), this, [this] { promptRename(m_currentId); });
    moreMenu->addAction(tr("Supprimer…"), this, [this] { confirmRemove(m_currentId); });
    m_more->setMenu(moreMenu);
    head->addWidget(m_more, 0, Qt::AlignTop);
    v->addLayout(head);

    m_rules = makeLabel(QString(), "muted", m_page);
    m_rules->setObjectName(QStringLiteral("CollectionRules"));
    m_rules->setWordWrap(true);
    v->addWidget(m_rules);

    auto* card = makeCard(m_page);
    auto* l = static_cast<QVBoxLayout*>(card->layout());
    auto* cardHead = new QHBoxLayout;
    cardHead->addWidget(makeLabel(tr("Contenu"), "h3", card), 1);
    m_count = makeLabel(QStringLiteral("0"), nullptr, card);
    m_count->setObjectName(QStringLiteral("CountBadge"));
    cardHead->addWidget(m_count);
    l->addLayout(cardHead);
    m_rowsLayout = new QVBoxLayout;
    m_rowsLayout->setSpacing(2);
    l->addLayout(m_rowsLayout);
    m_itemsEmpty = makeLabel(QString(), "muted", card);
    m_itemsEmpty->setWordWrap(true);
    l->addWidget(m_itemsEmpty);
    v->addWidget(card);
    v->addStretch(1);
    scroll->setWidget(m_page);
    m_center->addWidget(scroll);
    m_panels->addWorkArea(m_center);
}

void CollectionsPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_dirty) {
        refreshList();
        refreshPage();
    }
}

void CollectionsPage::refreshList()
{
    auto all = m_ctx.organize.collections();
    auto counts = m_ctx.organize.collectionCounts();
    const QList<CollectionRecord> collections = all ? all.value() : QList<CollectionRecord>{};
    const QHash<qint64, int> n = counts ? counts.value() : QHash<qint64, int>{};
    const Palette& pal = m_ctx.theme.palette();
    const QSignalBlocker block(m_list);
    m_list->clear();
    for (const CollectionRecord& c : collections) {
        auto* item = new QTreeWidgetItem(m_list);
        item->setIcon(0, Icons::get(collectionKindIcon(c.kind), kindColor(pal, c.kind), 16));
        item->setText(0, c.name);
        item->setText(1, QString::number(n.value(c.id)));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setForeground(1, pal.text3);
        item->setData(0, kIdRole, c.id);
        item->setToolTip(0, collectionKindLabel(c.kind));
        if (c.id == m_currentId)
            m_list->setCurrentItem(item);
    }
    m_dirty = false;
    if (m_currentId > 0 && !std::any_of(collections.cbegin(), collections.cend(), [this](const CollectionRecord& c) { return c.id == m_currentId; }))
        m_currentId = 0;
    if (m_currentId == 0 && m_list->topLevelItemCount() > 0) {
        m_list->setCurrentItem(m_list->topLevelItem(0));
        m_currentId = m_list->topLevelItem(0)->data(0, kIdRole).toLongLong();
    }
}

void CollectionsPage::showCollection(qint64 id)
{
    m_currentId = id;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->data(0, kIdRole).toLongLong() == id) {
            const QSignalBlocker block(m_list);
            m_list->setCurrentItem(m_list->topLevelItem(i));
        }
    refreshPage();
}

void CollectionsPage::refreshPage()
{
    clearRows(m_rowsLayout);
    m_rows.clear();
    m_items.clear();
    auto c = m_currentId > 0 ? m_ctx.organize.collection(m_currentId) : Result<CollectionRecord>(Error::make(Error::Code::NotFound, {}));
    if (!c) {
        m_center->setCurrentWidget(m_empty);
        return;
    }
    m_center->setCurrentIndex(1);
    const CollectionRecord& col = c.value();
    const Palette& pal = m_ctx.theme.palette();
    const bool smart = col.kind == CollectionKind::Smart;
    QColor soft = kindColor(pal, col.kind);
    soft.setAlphaF(pal.dark ? 0.24 : 0.16);
    m_badge->setStyleSheet(QStringLiteral("QLabel#SubjectBadge { background: %1; border-radius: %2px; }")
                               .arg(soft.name(QColor::HexArgb), QString::number(m_ctx.theme.px(14))));
    m_badge->setPixmap(Icons::get(collectionKindIcon(col.kind), kindColor(pal, col.kind), 26).pixmap(26, 26));
    m_title->setText(col.name);
    m_kind->setText(collectionKindLabel(col.kind));
    m_editRules->setVisible(smart);
    auto subjects = m_ctx.organize.subjects();
    m_rules->setText(smart ? tr("Documents qui respectent %1 : %2")
                                 .arg(col.query.matchAll ? tr("toutes les règles") : tr("au moins une règle"),
                                      smartQuerySummary(col.query, subjects ? subjects.value() : QList<SubjectRecord>{}))
                           : QString());
    m_rules->setVisible(smart);

    auto items = m_ctx.organize.collectionItems(col.id);
    m_items = items ? items.value() : QList<CollectionItem>{};
    m_count->setText(QString::number(m_items.size()));
    for (int i = 0; i < m_items.size(); ++i) {
        const CollectionItem& it = m_items.at(i);
        QPushButton* row = nullptr;
        if (it.type == CollectionItem::Type::Document) {
            auto doc = m_ctx.library.document(it.id);
            if (!doc)
                continue;
            auto count = m_ctx.annotations.count(it.id);
            row = new DocumentRow(m_ctx.theme, doc.value(), count ? count.value() : 0, m_page);
            const qint64 id = it.id;
            connect(row, &QPushButton::clicked, this, [this, id] { emit openDocumentRequested(id); });
        } else {
            auto note = m_ctx.notes.find(it.id);
            if (!note)
                continue;
            row = new NoteRow(m_ctx.theme, note.value(), m_page);
            const qint64 id = it.id;
            connect(row, &QPushButton::clicked, this, [this, id] { emit openNoteRequested(id); });
        }
        if (!smart) {
            row->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(row, &QWidget::customContextMenuRequested, this, [this, row, i](const QPoint& pos) {
                if (QMenu* m = itemMenu(i))
                    m->popup(row->mapToGlobal(pos));
            });
        }
        m_rowsLayout->addWidget(row);
        m_rows << row;
    }
    m_itemsEmpty->setText(smart ? tr("Aucun document ne respecte ces règles pour l'instant.")
                                : tr("Ajoutez des documents depuis la bibliothèque (panneau d'un document, "
                                     "« Collections ») ou des notes depuis « Mes notes » (clic droit)."));
    m_itemsEmpty->setVisible(m_items.isEmpty());
}

QMenu* CollectionsPage::itemMenu(int row)
{
    if (row < 0 || row >= m_items.size())
        return nullptr;
    delete m_menu;
    m_menu = new QMenu(this);
    const CollectionItem item = m_items.at(row);
    QAction* up = m_menu->addAction(Icons::get(QStringLiteral("chevron-up"), m_ctx.theme.palette().text2, 16),
                                    tr("Monter"), this, [this, row] { moveItem(row, -1); });
    up->setObjectName(QStringLiteral("up"));
    up->setEnabled(row > 0);
    QAction* down = m_menu->addAction(Icons::get(QStringLiteral("chevron-down"), m_ctx.theme.palette().text2, 16),
                                      tr("Descendre"), this, [this, row] { moveItem(row, 1); });
    down->setObjectName(QStringLiteral("down"));
    down->setEnabled(row + 1 < m_items.size());
    m_menu->addSeparator();
    QAction* remove = m_menu->addAction(tr("Retirer de la collection"), this, [this, item] {
        (void)m_ctx.organize.removeFromCollection(m_currentId,
                                                  item.type == CollectionItem::Type::Note ? OrganizeService::Entity::Note
                                                                                          : OrganizeService::Entity::Document,
                                                  item.id);
    });
    remove->setObjectName(QStringLiteral("remove"));
    return m_menu;
}

void CollectionsPage::moveItem(int row, int delta)
{
    const int to = row + delta;
    if (row < 0 || row >= m_items.size() || to < 0 || to >= m_items.size())
        return;
    QList<CollectionItem> order = m_items;
    order.move(row, to);
    if (auto r = m_ctx.organize.reorder(m_currentId, order); !r)
        QMessageBox::warning(this, tr("Réordonner"), r.error().message);
}

qint64 CollectionsPage::createCollection(const QString& name, CollectionKind kind)
{
    auto created = m_ctx.organize.createCollection(name, kind);
    if (!created) {
        QMessageBox::warning(this, tr("Nouvelle collection"), created.error().message);
        return 0;
    }
    m_currentId = created.value().id;
    refreshList();
    refreshPage();
    return m_currentId;
}

void CollectionsPage::promptNew(CollectionKind kind)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, collectionKindLabel(kind), tr("Nom :"), QLineEdit::Normal, QString(), &ok);
    if (ok && !name.trimmed().isEmpty())
        createCollection(name, kind);
}

void CollectionsPage::editSmartCollection(qint64 id)
{
    qint64 saved = 0;
    if (m_smartEditor) {
        saved = m_smartEditor(id);
    } else {
        SmartCollectionDialog dialog(m_ctx, id, window());
        if (dialog.exec() == QDialog::Accepted)
            saved = dialog.savedId();
    }
    if (saved > 0) {
        m_currentId = saved;
        refreshList();
        refreshPage();
    }
}

void CollectionsPage::promptRename(qint64 id)
{
    auto c = m_ctx.organize.collection(id);
    if (!c)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Renommer la collection"), tr("Nom :"), QLineEdit::Normal,
                                               c.value().name, &ok);
    if (ok && !name.trimmed().isEmpty())
        if (auto r = m_ctx.organize.renameCollection(id, name); !r)
            QMessageBox::warning(this, tr("Renommer la collection"), r.error().message);
}

void CollectionsPage::confirmRemove(qint64 id)
{
    auto c = m_ctx.organize.collection(id);
    if (!c)
        return;
    if (QMessageBox::question(this, tr("Supprimer la collection"),
                              tr("Supprimer « %1 » ? Les documents et les notes qu'elle contient ne sont pas supprimés.")
                                  .arg(c.value().name))
        != QMessageBox::Yes)
        return;
    (void)m_ctx.organize.removeCollection(id);
}

}  // namespace studylab::ui
