#include "ui/reader/AnnotationPopup.h"

#include "ui/components/Widgets.h"
#include "ui/reader/AnnotationStyle.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace studylab::ui {

AnnotationPopup::AnnotationPopup(ThemeManager& theme, QWidget* parent) : QFrame(parent), m_theme(theme)
{
    setObjectName(QStringLiteral("AnnotationPopup"));
    setFocusPolicy(Qt::StrongFocus);
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 60));
    setGraphicsEffect(shadow);

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(8, 5, 8, 5);
    h->setSpacing(2);
    for (AnnotationColor c : allAnnotationColors()) {
        auto* b = new QToolButton(this);
        b->setProperty("readerTool", true);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolTip(tr("Surligner en %1").arg(annotationColorLabel(c).toLower()));
        b->setAccessibleName(annotationColorLabel(c));
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [this, c] { emit colorPicked(c); });
        h->addWidget(b);
        m_colors << b;
    }
    m_sep = makeSeparator(this);
    m_sep->setFrameShape(QFrame::VLine);
    m_sep->setFixedHeight(18);
    h->addSpacing(4);
    h->addWidget(m_sep);
    h->addSpacing(4);
    const auto typeButton = [this, h](AnnotationType t, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setProperty("readerTool", true);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [this, t] { emit typePicked(t); });
        h->addWidget(b);
        return b;
    };
    m_highlight = typeButton(AnnotationType::Highlight, tr("Surligner"));
    m_underline = typeButton(AnnotationType::Underline, tr("Souligner"));
    m_strike = typeButton(AnnotationType::Strikeout, tr("Barrer"));
    h->addSpacing(4);
    const auto textButton = [this, h](const QString& text) {
        auto* b = new QPushButton(text, this);
        b->setProperty("variant", QStringLiteral("ghost"));
        b->setCursor(Qt::PointingHandCursor);
        h->addWidget(b);
        return b;
    };
    m_note = textButton(tr("Note"));
    m_copy = textButton(tr("Copier"));
    m_delete = textButton(tr("Supprimer"));
    connect(m_note, &QPushButton::clicked, this, &AnnotationPopup::noteRequested);
    connect(m_copy, &QPushButton::clicked, this, &AnnotationPopup::copyRequested);
    connect(m_delete, &QPushButton::clicked, this, &AnnotationPopup::deleteRequested);

    refreshIcons();
    connect(&theme, &ThemeManager::themeChanged, this, &AnnotationPopup::refreshIcons);
    hide();
}

void AnnotationPopup::refreshIcons()
{
    const Palette& pal = m_theme.palette();
    const auto colors = allAnnotationColors();
    for (int i = 0; i < m_colors.size(); ++i)
        m_colors[i]->setIcon(colorSwatchIcon(annotationAccent(pal, colors[i]), pal.text2, 20));
    m_highlight->setIcon(Icons::stateful(QStringLiteral("highlighter"), pal.text2, pal.primaryStrong, 18));
    m_underline->setIcon(Icons::stateful(QStringLiteral("underline"), pal.text2, pal.primaryStrong, 18));
    m_strike->setIcon(Icons::stateful(QStringLiteral("strikethrough"), pal.text2, pal.primaryStrong, 18));
    m_note->setIcon(Icons::get(QStringLiteral("message-square"), pal.text2, 16));
    m_copy->setIcon(Icons::get(QStringLiteral("copy"), pal.text2, 16));
    m_delete->setIcon(Icons::get(QStringLiteral("trash"), pal.danger, 16));
}

QToolButton* AnnotationPopup::colorButton(AnnotationColor c) const
{
    return m_colors.value(int(allAnnotationColors().indexOf(c)));
}

QToolButton* AnnotationPopup::typeButton(AnnotationType t) const
{
    switch (t) {
    case AnnotationType::Highlight: return m_highlight;
    case AnnotationType::Underline: return m_underline;
    case AnnotationType::Strikeout: return m_strike;
    default: return nullptr;
    }
}

void AnnotationPopup::showFor(Mode mode, const QRect& anchor, AnnotationColor color, AnnotationType type, bool hasNote)
{
    m_mode = mode;
    const bool edit = mode == Mode::Edit;
    const bool textual = type == AnnotationType::Highlight || type == AnnotationType::Underline
                         || type == AnnotationType::Strikeout;
    const auto colors = allAnnotationColors();
    for (int i = 0; i < m_colors.size(); ++i) {
        m_colors[i]->setChecked(edit && colors[i] == color);
        m_colors[i]->setVisible(type != AnnotationType::Bookmark);
        m_colors[i]->setToolTip(edit ? tr("Couleur : %1").arg(annotationColorLabel(colors[i]).toLower())
                                     : tr("Surligner en %1").arg(annotationColorLabel(colors[i]).toLower()));
    }
    m_highlight->setVisible(edit && textual);
    m_highlight->setChecked(edit && type == AnnotationType::Highlight);
    m_underline->setVisible(!edit || textual);
    m_underline->setChecked(edit && type == AnnotationType::Underline);
    m_strike->setVisible(!edit || textual);
    m_strike->setChecked(edit && type == AnnotationType::Strikeout);
    m_sep->setVisible(type != AnnotationType::Bookmark && (!edit || textual));
    m_note->setText(edit && hasNote ? tr("Modifier la note") : tr("Note"));
    m_note->setVisible(type != AnnotationType::Bookmark);
    m_copy->setVisible(!edit || textual);
    m_delete->setVisible(edit);
    adjustSize();

    QWidget* host = parentWidget();
    const QSize s = sizeHint();
    int x = anchor.center().x() - s.width() / 2;
    int y = anchor.top() - s.height() - 10;
    if (y < 6)
        y = anchor.bottom() + 10;
    if (host) {
        x = std::clamp(x, 6, std::max(6, host->width() - s.width() - 6));
        y = std::clamp(y, 6, std::max(6, host->height() - s.height() - 6));
    }
    setGeometry(QRect(QPoint(x, y), s));
    show();
    raise();
}

void AnnotationPopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QFrame::keyPressEvent(event);
}

std::optional<QString> NoteDialog::edit(QWidget* parent, ThemeManager& theme, const QString& quote,
                                        const QString& current)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(current.isEmpty() ? QObject::tr("Ajouter une note") : QObject::tr("Modifier la note"));
    dlg.setMinimumWidth(theme.px(440));
    auto* v = new QVBoxLayout(&dlg);
    v->setContentsMargins(20, 18, 20, 16);
    v->setSpacing(10);
    if (!quote.isEmpty()) {
        QString q = normalizeQuote(quote);
        if (q.size() > 280)
            q = q.left(277) + QStringLiteral("…");
        auto* label = makeLabel(QStringLiteral("« %1 »").arg(q), "muted", &dlg);
        label->setObjectName(QStringLiteral("NoteQuote"));
        label->setWordWrap(true);
        v->addWidget(label);
    }
    auto* edit = new QPlainTextEdit(&dlg);
    edit->setObjectName(QStringLiteral("NoteEdit"));
    edit->setPlaceholderText(QObject::tr("Votre note (Markdown accepté)…"));
    edit->setPlainText(current);
    edit->setMinimumHeight(theme.px(140));
    v->addWidget(edit, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Save)->setText(QObject::tr("Enregistrer"));
    buttons->button(QDialogButtonBox::Save)->setProperty("variant", QStringLiteral("primary"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QObject::tr("Annuler"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(buttons);
    edit->setFocus();
    if (dlg.exec() != QDialog::Accepted)
        return std::nullopt;
    return edit->toPlainText();
}

}  // namespace studylab::ui
