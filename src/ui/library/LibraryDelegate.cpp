#include "ui/library/LibraryDelegate.h"

#include "ui/library/CoverArt.h"
#include "ui/library/LibraryModel.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QDateTime>
#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>

namespace studylab::ui {

namespace {
constexpr int kCardWidth = 164;
constexpr int kCoverHeight = 226;
constexpr int kGap = 22;
constexpr int kListRow = 60;

QFont sized(const QFont& base, double px, QFont::Weight w = QFont::Normal)
{
    QFont f = base;
    f.setPixelSize(int(std::lround(px)));
    f.setWeight(w);
    return f;
}
}  // namespace

LibraryDelegate::LibraryDelegate(ThemeManager& theme, QObject* parent) : QStyledItemDelegate(parent), m_theme(theme) {}

QSize LibraryDelegate::gridCellSize() const
{
    const double s = m_theme.scale();
    return {int((kCardWidth + kGap) * s), int((kCoverHeight + 78) * s)};
}

QSize LibraryDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    if (m_mode == Mode::Grid)
        return gridCellSize();
    return {option.rect.width() > 0 ? option.rect.width() : 600, int(kListRow * m_theme.scale())};
}

void LibraryDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    if (m_mode == Mode::Grid)
        paintGrid(*painter, option, index);
    else
        paintList(*painter, option, index);
    painter->restore();
}

void LibraryDelegate::paintGrid(QPainter& p, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto* r = index.data(LibraryModel::RecordRole).value<const DocumentRecord*>();
    if (!r)
        return;
    const Palette& pal = m_theme.palette();
    const double s = m_theme.scale();
    const QRectF cell = option.rect;
    const qreal w = kCardWidth * s;
    const QRectF cover(cell.left() + (cell.width() - w) / 2, cell.top() + 4 * s, w, kCoverHeight * s);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;

    if (hover && !selected) {
        p.setPen(Qt::NoPen);
        p.setBrush(pal.sunken);
        p.drawRoundedRect(cover.adjusted(-4 * s, -4 * s, 4 * s, 4 * s), 9 * s, 9 * s);
    }
    CoverArt::paintCover(p, cover, *r, index.data(LibraryModel::CoverRole).value<QImage>(), pal, s);
    if (selected) {
        p.setPen(QPen(pal.primary, 2.5 * s));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(cover.adjusted(-3.5 * s, -3.5 * s, 3.5 * s, 3.5 * s), 9 * s, 9 * s);
    }
    if (r->favorite) {
        const QRectF star(cover.right() - 26 * s, cover.top() + 6 * s, 20 * s, 20 * s);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 70));
        p.drawEllipse(star);
        QFont f = sized(option.font, 12 * s, QFont::Bold);
        p.setFont(f);
        p.setPen(QColor(0xF5, 0xD7, 0x7A));
        p.drawText(star, Qt::AlignCenter, QStringLiteral("★"));
    }

    // Titre
    const QRectF titleRect(cover.left(), cover.bottom() + 8 * s, w, 20 * s);
    const QFont titleFont = sized(option.font, 13.5 * s, QFont::Medium);
    p.setFont(titleFont);
    p.setPen(pal.text);
    p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetricsF(titleFont).elidedText(r->title, Qt::ElideRight, titleRect.width()));

    // Auteur + statut
    const QRectF metaRect(cover.left(), titleRect.bottom() + 3 * s, w, 22 * s);
    const qreal pillW = CoverArt::paintStatusPill(p, metaRect, r->status, pal, s);
    const QFont small = sized(option.font, 12 * s);
    p.setFont(small);
    p.setPen(pal.text3);
    const QString author = r->authors.isEmpty() ? documentFormatLabel(r->format) : r->authors.first();
    p.drawText(metaRect.adjusted(0, 0, -pillW - 6 * s, 0), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetricsF(small).elidedText(author, Qt::ElideRight, metaRect.width() - pillW - 6 * s));

    // Progression (seulement si commencée)
    if (r->progress > 0.0) {
        const QRectF track(cover.left(), metaRect.bottom() + 6 * s, w, 3 * s);
        p.setPen(Qt::NoPen);
        p.setBrush(pal.sunken);
        p.drawRoundedRect(track, 1.5 * s, 1.5 * s);
        p.setBrush(pal.primary);
        p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * qBound(0.0, r->progress, 1.0), track.height()),
                          1.5 * s, 1.5 * s);
    }
}

void LibraryDelegate::paintList(QPainter& p, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto* r = index.data(LibraryModel::RecordRole).value<const DocumentRecord*>();
    if (!r)
        return;
    const Palette& pal = m_theme.palette();
    const double s = m_theme.scale();
    const QRectF row = QRectF(option.rect).adjusted(2 * s, 1 * s, -2 * s, -1 * s);
    const bool selected = option.state & QStyle::State_Selected;
    if (selected || (option.state & QStyle::State_MouseOver)) {
        p.setPen(Qt::NoPen);
        p.setBrush(selected ? pal.primarySoft : pal.sunken);
        p.drawRoundedRect(row, 8 * s, 8 * s);
    }
    const qreal pad = 10 * s;
    const QRectF cover(row.left() + pad, row.top() + 6 * s, (row.height() - 12 * s) * 0.7, row.height() - 12 * s);
    CoverArt::paintCover(p, cover, *r, index.data(LibraryModel::CoverRole).value<QImage>(), pal, s * 0.5);

    // Colonnes à droite : format · pages, date d'ajout, statut
    const qreal statusW = 96 * s, dateW = 110 * s, formatW = 120 * s;
    qreal right = row.right() - pad;
    const QRectF statusRect(right - statusW, row.top(), statusW, row.height());
    CoverArt::paintStatusPill(p, statusRect, r->status, pal, s, Qt::AlignLeft | Qt::AlignVCenter);
    right -= statusW + 8 * s;

    const QFont small = sized(option.font, 12 * s);
    p.setFont(small);
    p.setPen(pal.text3);
    const QLocale fr(QLocale::French, QLocale::France);
    const QDateTime added = QDateTime::fromString(r->addedAt, Qt::ISODate).toLocalTime();
    const bool wide = row.width() > 640 * s;
    if (wide) {
        p.drawText(QRectF(right - dateW, row.top(), dateW, row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   fr.toString(added.date(), QStringLiteral("d MMM yyyy")));
        right -= dateW + 8 * s;
        const QString fmt = r->pageCount > 0
                                ? QStringLiteral("%1 · %2 %3").arg(documentFormatLabel(r->format)).arg(r->pageCount)
                                      .arg(r->format == DocumentFormat::Pdf ? QStringLiteral("p.") : QStringLiteral("ch."))
                                : documentFormatLabel(r->format);
        p.drawText(QRectF(right - formatW, row.top(), formatW, row.height()), Qt::AlignLeft | Qt::AlignVCenter, fmt);
        right -= formatW + 8 * s;
    }

    // Titre et auteurs
    const qreal textLeft = cover.right() + 12 * s;
    const QRectF textRect(textLeft, row.top(), right - textLeft, row.height());
    const QFont titleFont = sized(option.font, 13.5 * s, QFont::Medium);
    QString title = r->title;
    if (r->favorite)
        title = QStringLiteral("★ ") + title;
    p.setFont(titleFont);
    p.setPen(selected ? pal.primaryStrong : pal.text);
    p.drawText(textRect.adjusted(0, 0, 0, -row.height() / 2 + 2 * s), Qt::AlignLeft | Qt::AlignBottom,
               QFontMetricsF(titleFont).elidedText(title, Qt::ElideRight, textRect.width()));
    p.setFont(small);
    p.setPen(pal.text3);
    QString sub = r->authors.join(QStringLiteral(", "));
    if (r->year > 0)
        sub += (sub.isEmpty() ? QString() : QStringLiteral(" · ")) + QString::number(r->year);
    p.drawText(textRect.adjusted(0, row.height() / 2 + 2 * s, 0, 0), Qt::AlignLeft | Qt::AlignTop,
               QFontMetricsF(small).elidedText(sub, Qt::ElideRight, textRect.width()));
}

}  // namespace studylab::ui
