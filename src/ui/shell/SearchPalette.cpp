#include "ui/shell/SearchPalette.h"

#include "ui/components/Widgets.h"
#include "ui/reader/AnnotationStyle.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {
constexpr int kHitRole = Qt::UserRole + 1;
}

SearchPalette::SearchPalette(SearchService& search, ThemeManager& theme, QWidget* window)
    : QFrame(window), m_search(search), m_theme(theme)
{
    qRegisterMetaType<SearchHit>();
    setObjectName(QStringLiteral("SearchPalette"));
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(32);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(0, 0, 0, 60));
    setGraphicsEffect(shadow);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(12, 12, 12, 10);
    v->setSpacing(8);
    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("SearchInput"));
    m_input->setPlaceholderText(tr("Rechercher dans les titres, auteurs, annotations et commentaires…"));
    m_input->setAccessibleName(tr("Rechercher partout"));
    m_input->setClearButtonEnabled(true);
    v->addWidget(m_input);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("SearchResults"));
    m_list->setAccessibleName(tr("Résultats de recherche"));
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setUniformItemSizes(false);
    v->addWidget(m_list, 1);
    m_status = makeLabel(QString(), "faint", this);
    m_status->setWordWrap(true);
    v->addWidget(m_status);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);
    connect(&m_debounce, &QTimer::timeout, this, &SearchPalette::run);
    connect(m_input, &QLineEdit::textChanged, this, [this] { m_debounce.start(); });
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        m_debounce.stop();
        if (m_results.query != m_input->text())
            run();
        activate(m_list->currentRow());
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { activate(m_list->row(item)); });
    m_input->installEventFilter(this);
    window->installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (isVisible() && now && now != this && !isAncestorOf(now))
            hide();
    });
    hide();
}

void SearchPalette::place()
{
    QWidget* w = parentWidget();
    const int width = std::min(m_theme.px(640), w->width() - m_theme.px(48));
    const int height = std::min(m_theme.px(460), w->height() - m_theme.px(96));
    setGeometry((w->width() - width) / 2, m_theme.px(56), width, height);
}

void SearchPalette::popup()
{
    place();
    show();
    raise();
    m_input->setFocus(Qt::ShortcutFocusReason);
    m_input->selectAll();
    if (m_input->text().isEmpty())
        run();
}

void SearchPalette::search(const QString& text)
{
    m_input->setText(text);
    m_debounce.stop();
    run();
}

bool SearchPalette::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(event);
        switch (k->key()) {
        case Qt::Key_Escape: hide(); return true;
        case Qt::Key_Down: moveSelection(1); return true;
        case Qt::Key_Up: moveSelection(-1); return true;
        case Qt::Key_PageDown: moveSelection(5); return true;
        case Qt::Key_PageUp: moveSelection(-5); return true;
        default: break;
        }
    }
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
        place();
    return QFrame::eventFilter(watched, event);
}

void SearchPalette::moveSelection(int delta)
{
    int row = m_list->currentRow();
    const int step = delta > 0 ? 1 : -1;
    for (int n = 0; n < std::abs(delta); ++n) {
        int next = row + step;
        while (next >= 0 && next < m_list->count() && m_rows[next].itemId == 0)
            next += step;
        if (next < 0 || next >= m_list->count())
            break;
        row = next;
    }
    if (row >= 0)
        m_list->setCurrentRow(row);
}

QString SearchPalette::snippetHtml(const QString& snippet) const
{
    QString html = snippet.toHtmlEscaped();
    html.replace(QChar(SearchHit::kMarkStart),
                 QStringLiteral("<span style=\"background:%1;color:%2\">").arg(m_theme.palette().hlYellow.name(), m_theme.palette().text.name()));
    html.replace(QChar(SearchHit::kMarkEnd), QStringLiteral("</span>"));
    return html;
}

void SearchPalette::run()
{
    m_list->clear();
    m_rows.clear();
    const QString text = m_input->text();
    if (text.trimmed().isEmpty()) {
        m_results = SearchResults{};
        m_status->setText(tr("Les mots sont cherchés au début des mots, sans tenir compte des accents. "
                             "Le texte intégral des documents sera indexé dans une prochaine version."));
        return;
    }
    auto r = m_search.search(text, 8);
    if (!r) {
        m_status->setText(tr("Recherche impossible : %1").arg(r.error().message));
        return;
    }
    m_results = r.value();
    const Palette& pal = m_theme.palette();
    auto header = [this, &pal](const QString& title, int count) {
        auto* item = new QListWidgetItem(QStringLiteral("%1 · %2").arg(title.toUpper()).arg(count), m_list);
        item->setFlags(Qt::NoItemFlags);
        QFont f = item->font();
        f.setPointSizeF(f.pointSizeF() * 0.85);
        f.setWeight(QFont::DemiBold);
        item->setFont(f);
        item->setForeground(pal.text3);
        m_rows << SearchHit{};
    };
    auto row = [this, &pal](const SearchHit& hit, const QString& icon, const QColor& iconColor, const QString& title,
                            const QString& detail) {
        auto* item = new QListWidgetItem(m_list);
        item->setData(kHitRole, QVariant::fromValue(hit));
        auto* w = new QWidget;
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(8, 5, 8, 5);
        h->setSpacing(10);
        auto* ic = new QLabel(w);
        ic->setPixmap(Icons::get(icon, iconColor, 16).pixmap(16, 16));
        ic->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        ic->setContentsMargins(0, 2, 0, 0);
        auto* label = new QLabel(w);
        label->setTextFormat(Qt::RichText);
        label->setWordWrap(false);
        label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        label->setText(QStringLiteral("<div style=\"color:%1\">%2</div><div style=\"color:%3\">%4</div>")
                          .arg(pal.text.name(), title, pal.text2.name(), detail));
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        ic->setAttribute(Qt::WA_TransparentForMouseEvents);
        h->addWidget(ic);
        h->addWidget(label, 1);
        w->setAttribute(Qt::WA_TransparentForMouseEvents);
        item->setSizeHint(QSize(0, 2 * label->fontMetrics().lineSpacing() + 14));
        m_list->setItemWidget(item, w);
        m_rows << hit;
    };
    if (!m_results.documents.isEmpty()) {
        header(tr("Documents"), int(m_results.documents.size()));
        for (const SearchHit& h : m_results.documents)
            row(h, QStringLiteral("book"), pal.text2, QStringLiteral("<b>%1</b>").arg(h.documentTitle.toHtmlEscaped()),
                h.snippet.toHtmlEscaped());
    }
    if (!m_results.annotations.isEmpty()) {
        header(tr("Annotations"), int(m_results.annotations.size()));
        for (const SearchHit& h : m_results.annotations)
            row(h, QStringLiteral("highlighter"), annotationAccent(pal, h.color), snippetHtml(h.snippet),
                QStringLiteral("%1 · %2").arg(h.documentTitle.toHtmlEscaped(), h.location.toHtmlEscaped()));
    }
    if (!m_results.comments.isEmpty()) {
        header(tr("Commentaires"), int(m_results.comments.size()));
        for (const SearchHit& h : m_results.comments) {
            QStringList where{h.commentKind, h.documentTitle};
            if (!h.location.isEmpty())
                where << h.location;
            row(h, QStringLiteral("message-square"), pal.lavender, snippetHtml(h.snippet),
                where.join(QStringLiteral(" · ")).toHtmlEscaped());
        }
    }
    m_status->setText(m_results.isEmpty() ? tr("Aucun résultat pour « %1 ».").arg(text.trimmed())
                                          : (m_results.total() == 1 ? tr("1 résultat") : tr("%1 résultats").arg(m_results.total()))
                                                + tr(" · Entrée pour ouvrir · Échap pour fermer"));
    moveSelection(1);
}

void SearchPalette::activate(int row)
{
    if (row < 0 || row >= m_rows.size() || m_rows[row].itemId == 0)
        return;
    const SearchHit hit = m_rows[row];
    hide();
    emit hitActivated(hit);
}

}  // namespace studylab::ui
