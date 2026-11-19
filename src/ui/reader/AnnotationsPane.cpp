#include "ui/reader/AnnotationsPane.h"

#include "core/domain/Document.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/Widgets.h"
#include "ui/reader/AnnotationStyle.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace studylab::ui {

// ───────────────────────── Modèle ─────────────────────────

AnnotationListModel::AnnotationListModel(QObject* parent) : QAbstractListModel(parent) {}

int AnnotationListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant AnnotationListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row& row = m_rows[index.row()];
    switch (role) {
    case IsHeaderRole: return row.header;
    case RecordRole: return row.header ? QVariant() : QVariant::fromValue(row.record);
    case IdRole: return row.header ? 0 : row.record.id;
    case Qt::DisplayRole:
    case Qt::AccessibleTextRole:
        if (row.header)
            return row.title;
        return QStringLiteral("%1, p. %2 : %3")
            .arg(annotationTypeLabel(row.record.type),
                 row.record.pageLabel.isEmpty() ? QString::number(row.record.unitIndex + 1) : row.record.pageLabel,
                 row.record.selectedText.isEmpty() ? row.record.note : row.record.selectedText);
    default: return {};
    }
}

Qt::ItemFlags AnnotationListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid() || m_rows[index.row()].header)
        return Qt::ItemIsEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void AnnotationListModel::setRows(const QList<AnnotationRecord>& records, const std::function<QString(int)>& groupOf)
{
    beginResetModel();
    m_rows.clear();
    m_count = int(records.size());
    QString current;
    bool first = true;
    for (const auto& r : records) {
        QString group = groupOf ? groupOf(r.unitIndex) : QString();
        if (group.isEmpty())
            group = tr("Page %1").arg(r.pageLabel.isEmpty() ? QString::number(r.unitIndex + 1) : r.pageLabel);
        if (first || group != current) {
            m_rows.append({true, group, {}});
            current = group;
            first = false;
        }
        m_rows.append({false, {}, r});
    }
    endResetModel();
}

int AnnotationListModel::rowOf(qint64 id) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (!m_rows[i].header && m_rows[i].record.id == id)
            return i;
    return -1;
}

// ───────────────────────── Délégué (cartes) ─────────────────────────

namespace {

class AnnotationDelegate : public QStyledItemDelegate {
public:
    AnnotationDelegate(ThemeManager& theme, QListView* view, const qint64* active)
        : QStyledItemDelegate(view), m_theme(theme), m_view(view), m_active(active)
    {
    }

    QFont quoteFont() const
    {
        QFont f(QStringLiteral("Georgia"));
        f.setStyleHint(QFont::Serif);
        f.setItalic(true);
        f.setPixelSize(m_theme.px(14));
        return f;
    }
    QFont smallFont() const
    {
        QFont f = m_view->font();
        f.setPixelSize(m_theme.px(11));
        return f;
    }
    QFont noteFont() const
    {
        QFont f = m_view->font();
        f.setPixelSize(m_theme.px(12));
        return f;
    }
    int cardWidth() const { return m_view->viewport()->width() - 2 * m_theme.px(4); }
    int textWidth() const { return cardWidth() - m_theme.px(14) - m_theme.px(14) - m_theme.px(3); }

    static QString quoteOf(const AnnotationRecord& r)
    {
        switch (r.type) {
        case AnnotationType::Bookmark: return QObject::tr("Signet");
        case AnnotationType::Marker:
            return r.selectedText.isEmpty() ? QObject::tr("Zone de la page") : QStringLiteral("« %1 »").arg(r.selectedText);
        case AnnotationType::Note: return QString();
        default: return QStringLiteral("« %1 »").arg(r.selectedText);
        }
    }

    // Hauteur d'un texte replié, borné à maxLines lignes.
    static int textHeight(const QString& text, const QFont& font, int width, int maxLines)
    {
        if (text.isEmpty())
            return 0;
        const QFontMetrics fm(font);
        const QRect r = fm.boundingRect(QRect(0, 0, width, 10000), Qt::TextWordWrap, text);
        return std::min(r.height(), fm.lineSpacing() * maxLines);
    }

    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& index) const override
    {
        Q_UNUSED(opt)
        const int w = cardWidth();
        if (index.data(AnnotationListModel::IsHeaderRole).toBool())
            return {w, m_theme.px(30)};
        const auto r = index.data(AnnotationListModel::RecordRole).value<AnnotationRecord>();
        int h = m_theme.px(12);
        const QString quote = quoteOf(r);
        if (!quote.isEmpty())
            h += textHeight(quote, quoteFont(), textWidth(), 5) + m_theme.px(6);
        h += QFontMetrics(smallFont()).height();
        if (!r.note.isEmpty())
            h += m_theme.px(8) + textHeight(r.note, noteFont(), textWidth() - m_theme.px(16), 4) + m_theme.px(12);
        h += m_theme.px(12);
        return {w, h + m_theme.px(8)};
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const override
    {
        const Palette& pal = m_theme.palette();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect full = opt.rect;
        if (index.data(AnnotationListModel::IsHeaderRole).toBool()) {
            QFont f = smallFont();
            f.setWeight(QFont::DemiBold);
            f.setLetterSpacing(QFont::PercentageSpacing, 104);
            p->setFont(f);
            p->setPen(pal.text3);
            const QString t = QFontMetrics(f).elidedText(index.data().toString().toUpper(), Qt::ElideRight,
                                                         full.width() - m_theme.px(8));
            p->drawText(full.adjusted(m_theme.px(4), 0, 0, -m_theme.px(4)), Qt::AlignLeft | Qt::AlignBottom, t);
            p->restore();
            return;
        }
        const auto r = index.data(AnnotationListModel::RecordRole).value<AnnotationRecord>();
        const bool active = *m_active == r.id;
        const bool hover = opt.state & QStyle::State_MouseOver;
        const QRectF card = QRectF(full).adjusted(m_theme.px(4) + 0.5, 0.5, -m_theme.px(4) - 0.5,
                                                  -m_theme.px(8) - 0.5);
        p->setPen(QPen(active ? pal.primary : pal.border, active ? 1.6 : 1));
        p->setBrush(hover && !active ? pal.surface2 : pal.surface);
        p->drawRoundedRect(card, 8, 8);
        // Barre de couleur
        const QColor accent = annotationAccent(pal, r.color);
        const int pad = m_theme.px(14);
        const QRectF bar(card.left() + pad, card.top() + m_theme.px(12), m_theme.px(3), card.height() - m_theme.px(24));
        p->setPen(Qt::NoPen);
        p->setBrush(accent);
        p->drawRoundedRect(bar, 1.5, 1.5);

        const int x = int(bar.right()) + m_theme.px(10);
        const int tw = textWidth();
        int y = int(card.top()) + m_theme.px(12);
        const QString quote = quoteOf(r);
        if (!quote.isEmpty()) {
            const QFont qf = quoteFont();
            const int h = textHeight(quote, qf, tw, 5);
            p->setFont(qf);
            p->setPen(pal.text);
            p->drawText(QRect(x, y, tw, h), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, quote);
            y += h + m_theme.px(6);
        }
        // Ligne d'information : page · type · date
        const QFont sf = smallFont();
        QFont pageFont = sf;
        pageFont.setWeight(QFont::DemiBold);
        const QString page = QStringLiteral("p. %1").arg(r.pageLabel.isEmpty() ? QString::number(r.unitIndex + 1) : r.pageLabel);
        p->setFont(pageFont);
        p->setPen(pal.text2);
        const int pw = QFontMetrics(pageFont).horizontalAdvance(page);
        p->drawText(QRect(x, y, pw + 2, QFontMetrics(sf).height()), Qt::AlignLeft | Qt::AlignVCenter, page);
        p->setFont(sf);
        p->setPen(pal.text3);
        QString meta = QStringLiteral(" · %1").arg(annotationTypeLabel(r.type));
        const QString when = relativeTime(r.createdAt);
        if (!when.isEmpty())
            meta += QStringLiteral(" · ") + when;
        if (r.anchorStatus == QLatin1String("orphan"))
            meta += QStringLiteral(" · ") + QObject::tr("ancre perdue");
        p->drawText(QRect(x + pw, y, tw - pw, QFontMetrics(sf).height()), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(sf).elidedText(meta, Qt::ElideRight, tw - pw));
        y += QFontMetrics(sf).height();
        if (!r.note.isEmpty()) {
            y += m_theme.px(8);
            const QFont nf = noteFont();
            const int nh = textHeight(r.note, nf, tw - m_theme.px(16), 4);
            const QRectF box(x, y, tw, nh + m_theme.px(12));
            p->setPen(Qt::NoPen);
            p->setBrush(pal.lavenderSoft);
            p->drawRoundedRect(box, 6, 6);
            p->setFont(nf);
            p->setPen(pal.text);
            p->drawText(box.adjusted(m_theme.px(8), m_theme.px(6), -m_theme.px(8), -m_theme.px(6)),
                        Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, r.note);
        }
        p->restore();
    }

private:
    ThemeManager& m_theme;
    QListView* m_view;
    const qint64* m_active;
};

class AnnotationListView : public QListView {
public:
    using QListView::QListView;
    std::function<void(qint64, QPoint)> onContextMenu;

protected:
    void contextMenuEvent(QContextMenuEvent* e) override
    {
        const QModelIndex idx = indexAt(e->pos());
        const qint64 id = idx.data(AnnotationListModel::IdRole).toLongLong();
        if (id != 0 && onContextMenu)
            onContextMenu(id, e->globalPos());
    }
    void resizeEvent(QResizeEvent* e) override
    {
        QListView::resizeEvent(e);
        scheduleDelayedItemsLayout();  // hauteur des cartes selon la largeur
    }
};

}  // namespace

// ───────────────────────── Volet ─────────────────────────

AnnotationsPane::AnnotationsPane(ThemeManager& theme, QWidget* parent) : QFrame(parent), m_theme(theme)
{
    setObjectName(QStringLiteral("AnnotationsPane"));
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(14, 12, 10, 10);
    v->setSpacing(10);

    auto* header = new QHBoxLayout;
    header->setSpacing(6);
    m_title = makeLabel(tr("Annotations"), "h3", this);
    m_count = makeLabel(QStringLiteral("0"), nullptr, this);
    m_count->setObjectName(QStringLiteral("CountBadge"));
    m_export = new QToolButton(this);
    m_export->setProperty("readerTool", true);
    m_export->setAutoRaise(true);
    m_export->setToolTip(tr("Exporter les annotations en Markdown…"));
    m_export->setAccessibleName(tr("Exporter en Markdown"));
    m_close = new QToolButton(this);
    m_close->setProperty("readerTool", true);
    m_close->setAutoRaise(true);
    m_close->setToolTip(tr("Masquer le volet"));
    m_close->setAccessibleName(tr("Masquer le volet Annotations"));
    header->addWidget(m_title);
    header->addWidget(m_count);
    header->addStretch(1);
    header->addWidget(m_export);
    header->addWidget(m_close);
    v->addLayout(header);
    connect(m_export, &QToolButton::clicked, this, &AnnotationsPane::exportRequested);
    connect(m_close, &QToolButton::clicked, this, &AnnotationsPane::closeRequested);

    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Rechercher"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Rechercher dans les annotations"));
    m_scope = new SegmentedControl({tr("Ce document"), tr("Page")}, this);
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(m_scope);
    v->addLayout(searchRow);
    m_searchDebounce.setSingleShot(true);
    m_searchDebounce.setInterval(150);
    connect(m_search, &QLineEdit::textChanged, this, [this] { m_searchDebounce.start(); });
    connect(&m_searchDebounce, &QTimer::timeout, this, &AnnotationsPane::rebuild);
    connect(m_scope, &SegmentedControl::currentIndexChanged, this, &AnnotationsPane::rebuild);

    // Filtres par couleur (avec compteurs) et par type
    auto* chips = new QHBoxLayout;
    chips->setSpacing(4);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i <= 5; ++i) {
        auto* b = new QToolButton(this);
        b->setObjectName(QStringLiteral("FilterChip"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolButtonStyle(i == 0 ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
        if (i == 0) {
            b->setText(tr("Tout"));
            b->setChecked(true);
            b->setAccessibleName(tr("Toutes les couleurs"));
        } else {
            const AnnotationColor c = allAnnotationColors()[i - 1];
            b->setAccessibleName(annotationColorLabel(c));
            b->setToolTip(annotationColorLabel(c));
        }
        group->addButton(b, i);
        chips->addWidget(b);
        m_colorButtons << b;
    }
    chips->addStretch(1);
    v->addLayout(chips);
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        m_colorFilter = id == 0 ? std::nullopt : std::optional(allAnnotationColors()[id - 1]);
        rebuild();
    });

    m_type = new QComboBox(this);
    m_type->setAccessibleName(tr("Type d'annotation"));
    m_type->addItem(tr("Tous les types"), QString());
    for (AnnotationType t : allAnnotationTypes())
        m_type->addItem(annotationTypeLabel(t), annotationTypeKey(t));
    m_type->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto* typeRow = new QHBoxLayout;
    typeRow->addWidget(m_type);
    typeRow->addStretch(1);
    v->addLayout(typeRow);
    connect(m_type, &QComboBox::currentIndexChanged, this, &AnnotationsPane::rebuild);

    m_model = new AnnotationListModel(this);
    auto* list = new AnnotationListView(this);
    list->onContextMenu = [this](qint64 id, QPoint pos) { emit contextMenuRequested(id, pos); };
    m_list = list;
    m_list->setObjectName(QStringLiteral("AnnotationList"));
    m_list->setModel(m_model);
    m_list->setItemDelegate(new AnnotationDelegate(theme, m_list, &m_active));
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSpacing(0);
    m_list->setAccessibleName(tr("Liste des annotations"));
    v->addWidget(m_list, 1);
    connect(m_list, &QListView::clicked, this, [this](const QModelIndex& idx) {
        const qint64 id = idx.data(AnnotationListModel::IdRole).toLongLong();
        if (id == 0)
            return;
        setActive(id, false);
        emit annotationActivated(id);
    });
    connect(m_list, &QListView::activated, m_list, &QListView::clicked);

    m_empty = makeLabel(QString(), "muted", this);
    m_empty->setWordWrap(true);
    m_empty->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_empty->setContentsMargins(12, 24, 12, 0);
    v->addWidget(m_empty, 1);

    m_undoBar = new QFrame(this);
    m_undoBar->setObjectName(QStringLiteral("UndoBar"));
    auto* uh = new QHBoxLayout(m_undoBar);
    uh->setContentsMargins(12, 6, 6, 6);
    m_undoText = makeLabel(QString(), nullptr, m_undoBar);
    m_undo = new QPushButton(tr("Annuler"), m_undoBar);
    m_undo->setProperty("variant", QStringLiteral("ghost"));
    m_undo->setToolTip(tr("Annuler la suppression (Ctrl+Z)"));
    uh->addWidget(m_undoText, 1);
    uh->addWidget(m_undo);
    v->addWidget(m_undoBar);
    m_undoBar->hide();
    m_undoTimer.setSingleShot(true);
    m_undoTimer.setInterval(8000);
    connect(&m_undoTimer, &QTimer::timeout, this, &AnnotationsPane::hideUndo);
    connect(m_undo, &QPushButton::clicked, this, [this] {
        hideUndo();
        emit undoRequested();
    });

    refreshIcons();
    connect(&theme, &ThemeManager::themeChanged, this, [this] {
        refreshIcons();
        m_list->viewport()->update();
    });
    rebuild();
}

void AnnotationsPane::refreshIcons()
{
    const Palette& pal = m_theme.palette();
    m_export->setIcon(Icons::get(QStringLiteral("download"), pal.text2, 18));
    m_close->setIcon(Icons::get(QStringLiteral("x"), pal.text2, 16));
    const auto colors = allAnnotationColors();
    for (int i = 1; i < m_colorButtons.size(); ++i)
        m_colorButtons[i]->setIcon(colorSwatchIcon(annotationAccent(pal, colors[i - 1]), pal.text2, 14));
}

QToolButton* AnnotationsPane::colorFilter(std::optional<AnnotationColor> color) const
{
    if (!color)
        return m_colorButtons.first();
    return m_colorButtons.value(int(allAnnotationColors().indexOf(*color)) + 1);
}

void AnnotationsPane::setAnnotations(const QList<AnnotationRecord>& records)
{
    m_all = records;
    rebuild();
}

void AnnotationsPane::setCurrentPage(int page)
{
    if (page == m_currentPage)
        return;
    m_currentPage = page;
    if (m_scope->currentIndex() == 1)
        rebuild();
}

void AnnotationsPane::setGroupResolver(std::function<QString(int)> resolver)
{
    m_groupOf = std::move(resolver);
    rebuild();
}

void AnnotationsPane::setActive(qint64 id, bool scrollTo)
{
    m_active = id;
    const int row = m_model->rowOf(id);
    if (row >= 0 && scrollTo)
        m_list->scrollTo(m_model->index(row), QAbstractItemView::EnsureVisible);
    m_list->viewport()->update();
}

void AnnotationsPane::showUndo(const QString& message)
{
    m_undoText->setText(message);
    m_undoBar->show();
    m_undoTimer.start();
}

void AnnotationsPane::hideUndo()
{
    m_undoTimer.stop();
    m_undoBar->hide();
}

void AnnotationsPane::updateColorCounts()
{
    const auto colors = allAnnotationColors();
    QList<int> counts(colors.size(), 0);
    for (const auto& a : m_all)
        if (a.type != AnnotationType::Bookmark)
            ++counts[int(colors.indexOf(a.color))];
    for (int i = 0; i < colors.size(); ++i)
        m_colorButtons[i + 1]->setText(QString::number(counts[i]));
}

void AnnotationsPane::rebuild()
{
    updateColorCounts();
    const QString needle = m_search->text().trimmed();
    const bool pageOnly = m_scope->currentIndex() == 1;
    const QString typeKey = m_type->currentData().toString();
    QList<AnnotationRecord> shown;
    for (const auto& a : m_all) {
        if (pageOnly && a.unitIndex != m_currentPage)
            continue;
        if (m_colorFilter && (a.color != *m_colorFilter || a.type == AnnotationType::Bookmark))
            continue;
        if (!typeKey.isEmpty() && annotationTypeKey(a.type) != typeKey)
            continue;
        if (!needle.isEmpty()) {
            const QString hay = foldForSort(a.selectedText + QLatin1Char(' ') + a.note + QLatin1Char(' ') + a.title);
            bool all = true;
            for (const QString& word : foldForSort(needle).split(QLatin1Char(' '), Qt::SkipEmptyParts))
                all = all && hay.contains(word);
            if (!all)
                continue;
        }
        shown << a;
    }
    const int scroll = m_list->verticalScrollBar()->value();
    m_model->setRows(shown, m_groupOf);
    m_list->verticalScrollBar()->setValue(scroll);
    m_count->setText(QString::number(m_all.size()));
    m_count->setToolTip(ui::countLabel(m_all.size(), tr("annotation"), tr("annotations")));

    const bool empty = shown.isEmpty();
    m_list->setVisible(!empty);
    m_empty->setVisible(empty);
    if (m_all.isEmpty())
        m_empty->setText(tr("Aucune annotation pour l'instant.\nSélectionnez un passage pour le surligner, "
                            "ou choisissez un outil dans la barre du lecteur."));
    else if (pageOnly && needle.isEmpty() && !m_colorFilter && typeKey.isEmpty())
        m_empty->setText(tr("Aucune annotation sur cette page."));
    else
        m_empty->setText(tr("Aucune annotation ne correspond aux filtres."));
}

}  // namespace studylab::ui
