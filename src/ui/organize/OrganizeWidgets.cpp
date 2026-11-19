#include "ui/organize/OrganizeWidgets.h"

#include "ui/library/CoverArt.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QDropEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyleOptionButton>

namespace studylab::ui {

QColor organizeColor(const Palette& p, const QString& key)
{
    if (key == QLatin1String("blue"))
        return p.accBlue;
    if (key == QLatin1String("green"))
        return p.accGreen;
    if (key == QLatin1String("yellow"))
        return p.accYellow;
    if (key == QLatin1String("pink"))
        return p.accPink;
    if (key == QLatin1String("purple"))
        return p.accPurple;
    return p.primary;
}

QString organizeColorLabel(const QString& key)
{
    if (key == QLatin1String("blue"))
        return QObject::tr("Bleu");
    if (key == QLatin1String("green"))
        return QObject::tr("Vert");
    if (key == QLatin1String("yellow"))
        return QObject::tr("Jaune");
    if (key == QLatin1String("pink"))
        return QObject::tr("Rose");
    if (key == QLatin1String("purple"))
        return QObject::tr("Violet");
    return QObject::tr("Ardoise");
}

QIcon colorDot(const Palette& p, const QString& key, int size)
{
    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap pm(size * scale, size * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(organizeColor(p, key));
        const qreal d = size * 0.8;
        painter.drawEllipse(QRectF((size - d) / 2, (size - d) / 2, d, d));
        icon.addPixmap(pm);
    }
    return icon;
}

namespace {

/// Fond de survol commun aux lignes.
void paintRowBackground(QPainter& p, const QWidget* w, const Palette& pal, bool hovered, bool focused)
{
    QPainterPath path;
    path.addRoundedRect(QRectF(w->rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    if (hovered)
        p.fillPath(path, pal.sunken);
    if (focused) {
        p.setPen(QPen(pal.primary, 1.5));
        p.drawPath(path);
    }
}

}  // namespace

// ───────────────────────── DocumentRow ─────────────────────────

DocumentRow::DocumentRow(ThemeManager& theme, const DocumentRecord& doc, int annotations, QWidget* parent)
    : QPushButton(parent), m_theme(theme), m_doc(doc)
{
    setObjectName(QStringLiteral("OrganizeDocumentRow"));
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    m_detail = QStringLiteral("%1 %").arg(qRound(doc.progress * 100)) + QStringLiteral(" · ")
        + (annotations == 1 ? tr("1 annotation") : tr("%1 annotations").arg(annotations));
    setAccessibleName(doc.title);
    setAccessibleDescription(readingStatusLabel(doc.status) + QStringLiteral(" · ") + m_detail);
    setToolTip(doc.authors.isEmpty() ? doc.title : doc.title + QStringLiteral(" — ") + doc.authors.join(QStringLiteral(", ")));
}

QSize DocumentRow::sizeHint() const
{
    return {m_theme.px(300), fontMetrics().height() * 2 + m_theme.px(22)};
}

void DocumentRow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette& pal = m_theme.palette();
    paintRowBackground(p, this, pal, underMouse(), hasFocus());
    const double s = m_theme.px(100) / 100.0;
    const QRect r = rect();
    const QRectF cover(8 * s, 6 * s, 24 * s, r.height() - 12 * s);
    CoverArt::paintCover(p, cover, m_doc, QImage(), pal, s);
    const qreal pillW = CoverArt::paintStatusPill(p, QRectF(r.right() - 8 * s, 0, 0, r.height()), m_doc.status, pal, s);
    const QRectF text(cover.right() + 12 * s, 0, r.width() - cover.right() - 32 * s - pillW, r.height());
    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    const QFontMetricsF fb(bold);
    const QFontMetricsF fm(font());
    const qreal top = (r.height() - fb.height() - fm.height() - 2 * s) / 2;
    p.setFont(bold);
    p.setPen(pal.text);
    p.drawText(QRectF(text.left(), top, text.width(), fb.height()), Qt::AlignLeft | Qt::AlignVCenter,
               fb.elidedText(m_doc.title, Qt::ElideRight, text.width()));
    p.setFont(font());
    p.setPen(pal.text2);
    p.drawText(QRectF(text.left(), top + fb.height() + 2 * s, text.width(), fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(m_detail, Qt::ElideRight, text.width()));
}

// ───────────────────────── NoteRow ─────────────────────────

NoteRow::NoteRow(ThemeManager& theme, const NoteRecord& note, QWidget* parent)
    : QPushButton(parent), m_theme(theme), m_id(note.id), m_title(note.title), m_type(note.type)
{
    setObjectName(QStringLiteral("OrganizeNoteRow"));
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setAccessibleName(note.title);
    setAccessibleDescription(noteTypeLabel(note.type));
    setToolTip(note.folder.isEmpty() ? note.title : note.folder + QStringLiteral(" / ") + note.title);
}

QSize NoteRow::sizeHint() const
{
    return {m_theme.px(260), fontMetrics().height() + m_theme.px(16)};
}

void NoteRow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette& pal = m_theme.palette();
    paintRowBackground(p, this, pal, underMouse(), hasFocus());
    const int icon = m_theme.px(16);
    const int pad = m_theme.px(8);
    const QRect r = rect();
    Icons::get(noteTypeIcon(m_type), pal.text2, 16).paint(&p, QRect(pad, (r.height() - icon) / 2, icon, icon));
    const QFontMetricsF fm(font());
    // Type en abrégé à droite (« cours », « concept »), comme la maquette
    const QString type = noteTypeLabel(m_type).section(u' ', -1).toLower();
    const qreal typeW = fm.horizontalAdvance(type);
    p.setPen(pal.text3);
    p.drawText(QRectF(r.width() - pad - typeW, 0, typeW, r.height()), Qt::AlignRight | Qt::AlignVCenter, type);
    p.setPen(pal.text);
    const QRectF text(pad * 2 + icon, 0, r.width() - pad * 4 - icon - typeW, r.height());
    p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(m_title, Qt::ElideRight, text.width()));
}

// ───────────────────────── DragTree ─────────────────────────

DragTree::DragTree(QWidget* parent) : QTreeWidget(parent)
{
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
}

void DragTree::dropEvent(QDropEvent* event)
{
    QTreeWidgetItem* moved = currentItem();
    QTreeWidget::dropEvent(event);
    if (!moved || !event->isAccepted())
        return;
    QTreeWidgetItem* parent = moved->parent();
    const int index = parent ? parent->indexOfChild(moved) : indexOfTopLevelItem(moved);
    emit itemMoved(moved, parent, index);
}

}  // namespace studylab::ui
