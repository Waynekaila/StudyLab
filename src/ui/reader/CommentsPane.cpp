#include "ui/reader/CommentsPane.h"

#include "services/settings/SettingsService.h"
#include "ui/components/MarkdownHighlighter.h"
#include "ui/components/SegmentedControl.h"
#include "ui/components/Widgets.h"
#include "ui/reader/AnnotationStyle.h"
#include "ui/reader/MarkdownView.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QButtonGroup>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

QString colorAdjective(AnnotationColor c)
{
    switch (c) {
    case AnnotationColor::Yellow: return QObject::tr("jaune");
    case AnnotationColor::Green: return QObject::tr("verte");
    case AnnotationColor::Blue: return QObject::tr("bleue");
    case AnnotationColor::Pink: return QObject::tr("rose");
    case AnnotationColor::Purple: return QObject::tr("violette");
    }
    return {};
}

QString elide(const QString& s, int max)
{
    const QString t = s.simplified();
    return t.size() > max ? t.left(max - 1).trimmed() + QChar(0x2026) : t;
}

}  // namespace

// ───────────────────────── Cible ─────────────────────────

QJsonObject CommentTarget::toJson() const
{
    return {{QStringLiteral("anchor"), commentAnchorKey(anchor)},
            {QStringLiteral("annotation"), double(annotationId)},
            {QStringLiteral("locator"), locator},
            {QStringLiteral("unit"), unitIndex},
            {QStringLiteral("quote"), quote},
            {QStringLiteral("where"), where},
            {QStringLiteral("color"), annotationColorKey(color)}};
}

CommentTarget CommentTarget::fromJson(const QJsonObject& o)
{
    CommentTarget t;
    t.anchor = commentAnchorFromKey(o.value(QStringLiteral("anchor")).toString()).value_or(CommentAnchor::Free);
    t.annotationId = qint64(o.value(QStringLiteral("annotation")).toDouble());
    t.locator = o.value(QStringLiteral("locator")).toObject();
    t.unitIndex = o.value(QStringLiteral("unit")).toInt(-1);
    t.quote = o.value(QStringLiteral("quote")).toString();
    t.where = o.value(QStringLiteral("where")).toString();
    t.color = annotationColorFromKey(o.value(QStringLiteral("color")).toString()).value_or(AnnotationColor::Yellow);
    return t;
}

// ───────────────────────── Volet ─────────────────────────

CommentsPane::CommentsPane(ThemeManager& theme, SettingsService& settings, qint64 documentId, QWidget* parent)
    : QFrame(parent), m_theme(theme), m_settings(settings), m_documentId(documentId)
{
    qRegisterMetaType<CommentTarget>();
    setObjectName(QStringLiteral("CommentsPane"));
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(14, 12, 10, 10);
    v->setSpacing(10);

    auto* header = new QHBoxLayout;
    header->setSpacing(6);
    m_title = makeLabel(tr("Commentaires"), "h3", this);
    m_count = makeLabel(QStringLiteral("0"), nullptr, this);
    m_count->setObjectName(QStringLiteral("CountBadge"));
    auto tool = [this](const QString& tip, bool checkable) {
        auto* b = new QToolButton(this);
        b->setProperty("readerTool", true);
        b->setAutoRaise(true);
        b->setCheckable(checkable);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    m_searchToggle = tool(tr("Rechercher dans les commentaires"), true);
    m_collapse = tool(tr("Replier le volet Commentaires"), false);
    header->addWidget(m_title);
    header->addWidget(m_count);
    header->addStretch(1);
    header->addWidget(m_searchToggle);
    header->addWidget(m_collapse);
    v->addLayout(header);
    connect(m_collapse, &QToolButton::clicked, this, [this] { setCollapsed(!m_collapsed); });

    m_body = new QWidget(this);
    auto* bv = new QVBoxLayout(m_body);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(10);
    v->addWidget(m_body, 1);

    // Filtres
    auto* chips = new QHBoxLayout;
    chips->setSpacing(4);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    const QStringList labels{tr("Tous"), tr("Questions"), tr("Explications"), tr("Cette page")};
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = new QToolButton(m_body);
        b->setObjectName(QStringLiteral("FilterChip"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setText(labels[i]);
        b->setChecked(i == 0);
        group->addButton(b, i);
        chips->addWidget(b);
        m_filters << b;
    }
    chips->addStretch(1);
    bv->addLayout(chips);
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        m_filter = Filter(id);
        rebuild();
    });

    m_search = new QLineEdit(m_body);
    m_search->setPlaceholderText(tr("Rechercher dans les commentaires"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Rechercher dans les commentaires"));
    m_search->hide();
    bv->addWidget(m_search);
    m_searchDebounce.setSingleShot(true);
    m_searchDebounce.setInterval(150);
    connect(m_search, &QLineEdit::textChanged, this, [this] { m_searchDebounce.start(); });
    connect(&m_searchDebounce, &QTimer::timeout, this, &CommentsPane::rebuild);
    connect(m_searchToggle, &QToolButton::toggled, this, [this](bool on) {
        m_search->setVisible(on);
        if (on) {
            m_search->setFocus();
        } else if (!m_search->text().isEmpty()) {
            m_search->clear();
            rebuild();
        }
    });

    // Liste (vue Web créée au premier affichage : Qt WebEngine ne démarre que si le volet sert)
    m_listHost = new QStackedWidget(m_body);
    m_loading = makeLabel(QString(), "muted", m_listHost);
    m_loading->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_loading->setWordWrap(true);
    m_listHost->addWidget(m_loading);
    bv->addWidget(m_listHost, 1);

    m_undoBar = new QFrame(m_body);
    m_undoBar->setObjectName(QStringLiteral("UndoBar"));
    auto* uh = new QHBoxLayout(m_undoBar);
    uh->setContentsMargins(12, 6, 6, 6);
    m_undoText = makeLabel(QString(), nullptr, m_undoBar);
    m_undo = new QPushButton(tr("Annuler"), m_undoBar);
    m_undo->setProperty("variant", QStringLiteral("ghost"));
    m_undo->setToolTip(tr("Annuler la suppression (Ctrl+Z)"));
    uh->addWidget(m_undoText, 1);
    uh->addWidget(m_undo);
    bv->addWidget(m_undoBar);
    m_undoBar->hide();
    m_undoTimer.setSingleShot(true);
    m_undoTimer.setInterval(8000);
    connect(&m_undoTimer, &QTimer::timeout, this, &CommentsPane::hideUndo);
    connect(m_undo, &QPushButton::clicked, this, [this] {
        hideUndo();
        emit undoRequested();
    });

    // Éditeur
    m_editorFrame = new QFrame(m_body);
    m_editorFrame->setObjectName(QStringLiteral("CommentEditor"));
    auto* ev = new QVBoxLayout(m_editorFrame);
    ev->setContentsMargins(10, 8, 10, 10);
    ev->setSpacing(8);

    auto* targetRow = new QHBoxLayout;
    targetRow->setSpacing(6);
    m_targetIcon = new QLabel(m_editorFrame);
    m_targetLabel = makeLabel(QString(), "faint", m_editorFrame);
    m_targetLabel->setObjectName(QStringLiteral("CommentTarget"));
    m_targetLabel->setTextFormat(Qt::PlainText);
    m_detach = tool(tr("Commentaire sur le document entier"), false);
    m_detach->setParent(m_editorFrame);
    m_pageTarget = tool(QString(), false);
    m_pageTarget->setParent(m_editorFrame);
    targetRow->addWidget(m_targetIcon);
    targetRow->addWidget(m_targetLabel, 1);
    targetRow->addWidget(m_pageTarget);
    targetRow->addWidget(m_detach);
    ev->addLayout(targetRow);
    connect(m_detach, &QToolButton::clicked, this, [this] {
        m_target = CommentTarget{};
        updateTarget();
        m_draftTimer.start();
    });
    connect(m_pageTarget, &QToolButton::clicked, this, [this] {
        if (!m_positionTarget)
            return;
        m_target = m_positionTarget();
        updateTarget();
        m_draftTimer.start();
    });

    m_mode = new SegmentedControl({tr("Écrire"), tr("Aperçu")}, m_editorFrame);
    m_mode->setAccessibleName(tr("Mode de l'éditeur"));
    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(m_mode);
    modeRow->addStretch(1);
    ev->addLayout(modeRow);

    m_editorStack = new QStackedWidget(m_editorFrame);
    m_editor = new QPlainTextEdit(m_editorStack);
    m_editor->setObjectName(QStringLiteral("CommentText"));
    m_editor->setPlaceholderText(tr("Écrire un commentaire… Markdown, formules $…$ et liens [[…]]"));
    m_editor->setAccessibleName(tr("Texte du commentaire"));
    m_editor->setTabChangesFocus(true);
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new MarkdownHighlighter(theme, m_editor->document());
    m_editorStack->addWidget(m_editor);
    // Hauteur modeste, et une seule ligne au repos : la liste des commentaires garde la place restante.
    m_editorStack->setFixedHeight(theme.px(104));
    m_editorFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    ev->addWidget(m_editorStack);
    m_editor->installEventFilter(this);
    connect(m_mode, &SegmentedControl::currentIndexChanged, this, [this](int i) {
        if (i == 1) {
            if (!m_preview) {
                m_preview = new MarkdownView(m_editorStack);
                m_preview->setObjectName(QStringLiteral("CommentPreview"));
                m_editorStack->addWidget(m_preview);
                pushTheme(m_preview);
                connect(m_preview, &MarkdownView::linkActivated, this, &CommentsPane::linkActivated);
            }
            updatePreview();
            m_editorStack->setCurrentWidget(m_preview);
        } else {
            m_editorStack->setCurrentWidget(m_editor);
            m_editor->setFocus();
        }
    });

    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(6);
    QStringList kinds;
    for (CommentKind k : editableCommentKinds())
        kinds << commentKindLabel(k);
    m_kind = new SegmentedControl(kinds, m_editorFrame);
    m_kind->setAccessibleName(tr("Type de commentaire"));
    m_kind->setCurrentIndex(int(editableCommentKinds().indexOf(CommentKind::Reflection)));
    m_cancel = new QPushButton(tr("Annuler"), m_editorFrame);
    m_cancel->setProperty("variant", QStringLiteral("ghost"));
    m_submit = new QPushButton(tr("Ajouter"), m_editorFrame);
    m_submit->setProperty("variant", QStringLiteral("primary"));
    m_submit->setToolTip(tr("Ajouter le commentaire (Ctrl+Entrée)"));
    // Type sur sa propre ligne : le volet reste lisible quand on le rétrécit.
    ev->addWidget(m_kind);
    bottom->addStretch(1);
    bottom->addWidget(m_cancel);
    bottom->addWidget(m_submit);
    ev->addLayout(bottom);
    bv->addWidget(m_editorFrame);
    connect(m_kind, &SegmentedControl::currentIndexChanged, this, [this] { m_draftTimer.start(); });
    connect(m_cancel, &QPushButton::clicked, this, &CommentsPane::cancelEditing);
    connect(m_submit, &QPushButton::clicked, this, &CommentsPane::submit);

    m_draftTimer.setSingleShot(true);
    m_draftTimer.setInterval(2000);
    connect(&m_draftTimer, &QTimer::timeout, this, &CommentsPane::saveDraft);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        updateSubmit();
        if (!m_restoring)
            m_draftTimer.start();
    });

    refreshIcons();
    connect(&theme, &ThemeManager::themeChanged, this, [this] {
        refreshIcons();
        pushTheme(m_view);
        pushTheme(m_preview);
        rebuild();
    });
    m_collapsed = m_settings.value(QStringLiteral("reader/commentsCollapsed"), false).toBool();
    m_body->setVisible(!m_collapsed);
    restoreDraft();
    updateTarget();
    updateSubmit();
    rebuild();
    connect(qApp, &QApplication::focusChanged, this, [this] { updateComposer(); });
    connect(m_mode, &SegmentedControl::currentIndexChanged, this, [this] { updateComposer(); });
}

CommentsPane::~CommentsPane()
{
    if (m_draftTimer.isActive())
        saveDraft();
}

QString CommentsPane::draftKey(qint64 documentId) { return QStringLiteral("drafts/comment/%1").arg(documentId); }

void CommentsPane::refreshIcons()
{
    const Palette& pal = m_theme.palette();
    m_searchToggle->setIcon(Icons::stateful(QStringLiteral("search"), pal.text2, pal.primaryStrong, 16));
    m_collapse->setIcon(Icons::get(m_collapsed ? QStringLiteral("chevron-down") : QStringLiteral("chevron-up"), pal.text2, 16));
    m_detach->setIcon(Icons::get(QStringLiteral("x"), pal.text3, 14));
    m_pageTarget->setIcon(Icons::get(QStringLiteral("bookmark"), pal.text3, 14));
    updateTarget();
}

void CommentsPane::pushTheme(MarkdownView* view)
{
    if (!view)
        return;
    const Palette& p = m_theme.palette();
    QJsonObject vars{
        {QStringLiteral("bg"), hex(view == m_preview ? p.surface : p.surface2)},
        {QStringLiteral("card"), hex(p.lavenderSoft)},
        {QStringLiteral("card-border"), hex(p.dark ? p.lavenderSoft.lighter(130) : p.lavenderSoft.darker(106))},
        {QStringLiteral("text"), hex(p.text)},
        {QStringLiteral("muted"), hex(p.text2)},
        {QStringLiteral("border"), hex(p.border)},
        {QStringLiteral("link"), hex(p.primaryStrong)},
        {QStringLiteral("accent"), hex(p.primary)},
        {QStringLiteral("wiki"), hex(p.primaryStrong)},
        {QStringLiteral("code-bg"), p.dark ? QStringLiteral("rgba(255,255,255,.08)") : QStringLiteral("rgba(0,0,0,.05)")},
        {QStringLiteral("font"), QStringLiteral("\"%1\", system-ui, sans-serif").arg(font().family())},
        {QStringLiteral("size"), QStringLiteral("%1px").arg(m_theme.px(13))},
    };
    view->setThemeVars(vars);
}

void CommentsPane::ensureView()
{
    if (m_view)
        return;
    m_view = new MarkdownView(m_listHost);
    m_view->setObjectName(QStringLiteral("CommentList"));
    m_view->setAccessibleName(tr("Liste des commentaires"));
    m_listHost->addWidget(m_view);
    m_listHost->setCurrentWidget(m_view);
    pushTheme(m_view);
    connect(m_view, &MarkdownView::cardActivated, this, [this](qint64 id) {
        setActive(id, false);
        emit commentActivated(id);
    });
    connect(m_view, &MarkdownView::cardEditRequested, this, &CommentsPane::editRequested);
    connect(m_view, &MarkdownView::cardDeleteRequested, this, &CommentsPane::deleteRequested);
    connect(m_view, &MarkdownView::cardMenuRequested, this, &CommentsPane::menuRequested);
    connect(m_view, &MarkdownView::linkActivated, this, &CommentsPane::linkActivated);
    rebuild();
}

void CommentsPane::showEvent(QShowEvent* event)
{
    QFrame::showEvent(event);
    if (!m_collapsed)
        ensureView();
}

bool CommentsPane::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_editor) {
        if (event->type() == QEvent::FocusOut && m_draftTimer.isActive()) {
            m_draftTimer.stop();
            saveDraft();
        } else if (event->type() == QEvent::KeyPress) {
            auto* k = static_cast<QKeyEvent*>(event);
            if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && (k->modifiers() & Qt::ControlModifier)) {
                submit();
                return true;
            }
            if (k->key() == Qt::Key_Escape && (m_editingId != 0 || !m_editor->toPlainText().isEmpty())) {
                cancelEditing();
                return true;
            }
        }
    }
    return QFrame::eventFilter(watched, event);
}

void CommentsPane::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    m_body->setVisible(!collapsed);
    m_collapse->setToolTip(collapsed ? tr("Déplier le volet Commentaires") : tr("Replier le volet Commentaires"));
    m_collapse->setAccessibleName(m_collapse->toolTip());
    m_settings.setValue(QStringLiteral("reader/commentsCollapsed"), collapsed);
    refreshIcons();
    if (!collapsed && isVisible())
        ensureView();
    // Replié, le volet ne garde que son en-tête : le séparateur laisse la place aux annotations.
    setMaximumHeight(collapsed ? sizeHint().height() : QWIDGETSIZE_MAX);
}

void CommentsPane::setComments(const QList<CommentRecord>& comments)
{
    m_all = comments;
    rebuild();
}

void CommentsPane::setCurrentUnit(int unit)
{
    if (unit == m_currentUnit)
        return;
    m_currentUnit = unit;
    if (m_filter == Filter::CurrentUnit)
        rebuild();
}

void CommentsPane::setUnitIsChapter(bool chapter)
{
    m_unitIsChapter = chapter;
    m_filters[int(Filter::CurrentUnit)]->setText(chapter ? tr("Ce chapitre") : tr("Cette page"));
    updateTarget();
    rebuild();
}

QToolButton* CommentsPane::filterButton(Filter f) const { return m_filters.value(int(f)); }

void CommentsPane::setActive(qint64 id, bool scroll)
{
    m_active = id;
    if (m_view)
        m_view->setActive(id, scroll);
}

QString CommentsPane::emptyText() const
{
    if (m_all.isEmpty())
        return tr("Aucun commentaire. Écrivez ci-dessous, ou sélectionnez un passage puis « Commenter ».");
    return tr("Aucun commentaire ne correspond.");
}

QJsonObject CommentsPane::cardFor(const CommentRecord& c) const
{
    const Palette& pal = m_theme.palette();
    QString title = commentKindLabel(c.kind);
    QString where = commentLocation(c);
    if (c.anchor == CommentAnchor::Annotation && c.kind == CommentKind::Reflection)
        title = tr("Lié à l'annotation %1").arg(colorAdjective(c.annotationColor));
    QString quoteColor = hex(pal.border);
    if (c.anchor == CommentAnchor::Annotation)
        quoteColor = hex(annotationAccent(pal, c.annotationColor));
    return {{QStringLiteral("id"), double(c.id)},
            {QStringLiteral("kind"), commentKindKey(c.kind)},
            {QStringLiteral("anchor"), commentAnchorKey(c.anchor)},
            {QStringLiteral("title"), title},
            {QStringLiteral("where"), where},
            {QStringLiteral("date"), relativeTime(c.createdAt)},
            {QStringLiteral("quote"), c.quote.isEmpty() ? QString() : QStringLiteral("« %1 »").arg(elide(c.quote, 220))},
            {QStringLiteral("quoteColor"), quoteColor},
            {QStringLiteral("body"), c.body},
            {QStringLiteral("resolved"), c.resolved},
            {QStringLiteral("resolvedLabel"), tr("Résolue")},
            {QStringLiteral("openLabel"), tr("Ouverte")},
            {QStringLiteral("menuLabel"), tr("Actions")}};
}

void CommentsPane::rebuild()
{
    const QString needle = m_search->text().trimmed();
    QJsonArray cards;
    m_visible.clear();
    for (const CommentRecord& c : std::as_const(m_all)) {
        if (m_filter == Filter::Questions && c.kind != CommentKind::Question)
            continue;
        if (m_filter == Filter::Explanations && c.kind != CommentKind::Explanation)
            continue;
        if (m_filter == Filter::CurrentUnit && c.unitIndex != m_currentUnit)
            continue;
        if (!needle.isEmpty() && !c.body.contains(needle, Qt::CaseInsensitive) && !c.quote.contains(needle, Qt::CaseInsensitive))
            continue;
        cards.append(cardFor(c));
        m_visible << c.id;
    }
    m_count->setText(QString::number(m_all.size()));
    m_loading->setText(emptyText());
    if (m_view) {
        m_view->setCards(cards, emptyText());
        m_view->setActive(m_active, false);
    }
}

void CommentsPane::updateTarget()
{
    const Palette& pal = m_theme.palette();
    const bool editing = m_editingId != 0;
    QString icon = QStringLiteral("book-open");
    QString text;
    switch (m_target.anchor) {
    case CommentAnchor::Free: text = tr("Sur le document entier"); break;
    case CommentAnchor::Page:
        icon = QStringLiteral("bookmark");
        text = m_unitIsChapter ? tr("Ici · %1").arg(m_target.where) : tr("Page · %1").arg(m_target.where);
        break;
    case CommentAnchor::Passage:
        icon = QStringLiteral("type");
        text = tr("Passage · %1 · « %2 »").arg(m_target.where, elide(m_target.quote, 60));
        break;
    case CommentAnchor::Annotation:
        icon = QStringLiteral("highlighter");
        text = tr("Annotation %1 · %2 · « %3 »").arg(colorAdjective(m_target.color), m_target.where, elide(m_target.quote, 60));
        break;
    }
    if (editing)
        text = tr("Modification · %1").arg(text);
    m_targetLabel->setText(text);
    m_targetLabel->setToolTip(m_target.quote.isEmpty() ? text : m_target.quote);
    const QColor iconColor = m_target.anchor == CommentAnchor::Annotation ? annotationAccent(pal, m_target.color) : pal.text3;
    m_targetIcon->setPixmap(Icons::get(icon, iconColor, 14).pixmap(m_theme.px(14), m_theme.px(14)));
    m_detach->setVisible(!editing && m_target.anchor != CommentAnchor::Free);
    m_pageTarget->setVisible(!editing && m_positionTarget && m_target.anchor == CommentAnchor::Free);
    const QString pageTip = m_unitIsChapter ? tr("Rattacher à la position courante") : tr("Rattacher à la page courante");
    m_pageTarget->setToolTip(pageTip);
    m_pageTarget->setAccessibleName(pageTip);
    m_submit->setText(editing ? tr("Enregistrer") : tr("Ajouter"));
    if (m_composerInit)
        updateComposer();
    m_cancel->setVisible(editing || !m_editor->toPlainText().isEmpty() || m_target.anchor != CommentAnchor::Free);
}

void CommentsPane::updatePreview()
{
    if (m_preview)
        m_preview->setMarkdown(m_editor->toPlainText(), tr("Rien à prévisualiser."));
}

void CommentsPane::updateSubmit()
{
    m_submit->setEnabled(!m_editor->toPlainText().trimmed().isEmpty());
    m_cancel->setVisible(m_editingId != 0 || !m_editor->toPlainText().isEmpty() || m_target.anchor != CommentAnchor::Free);
    updateComposer();
}

void CommentsPane::updateComposer()
{
    const QWidget* focus = QApplication::focusWidget();
    const bool active = m_editingId != 0 || !m_editor->toPlainText().isEmpty() || m_target.anchor != CommentAnchor::Free
                        || m_mode->currentIndex() == 1 || (focus && m_editorFrame->isAncestorOf(focus));
    if (active == m_composerActive && m_composerInit)
        return;
    m_composerInit = true;
    m_composerActive = active;
    m_mode->setVisible(active);
    m_kind->setVisible(active);
    m_submit->setVisible(active);
    if (!active)
        m_cancel->setVisible(false);
    m_editorStack->setFixedHeight(m_theme.px(active ? 104 : 36));
}

CommentKind CommentsPane::kind() const
{
    return editableCommentKinds().value(m_kind->currentIndex(), CommentKind::Reflection);
}

void CommentsPane::setKind(CommentKind kind)
{
    const int i = int(editableCommentKinds().indexOf(kind));
    m_kind->setCurrentIndex(i >= 0 ? i : int(editableCommentKinds().indexOf(CommentKind::Reflection)));
}

QString CommentsPane::text() const { return m_editor->toPlainText(); }

void CommentsPane::startComment(const CommentTarget& target)
{
    if (m_editingId != 0) {
        m_editingId = 0;
        m_editor->clear();
    }
    m_target = target;
    if (m_collapsed)
        setCollapsed(false);
    m_mode->setCurrentIndex(0);
    m_editorStack->setCurrentWidget(m_editor);
    updateTarget();
    updateSubmit();
    m_editor->setFocus();
    m_draftTimer.start();
}

void CommentsPane::editComment(const CommentRecord& c)
{
    m_editingId = c.id;
    m_target = CommentTarget{};
    m_target.anchor = c.anchor;
    m_target.annotationId = c.annotationId;
    m_target.locator = c.locator;
    m_target.unitIndex = c.unitIndex;
    m_target.quote = c.quote;
    m_target.where = commentLocation(c);
    m_target.color = c.annotationColor;
    if (m_collapsed)
        setCollapsed(false);
    m_restoring = true;
    m_editor->setPlainText(c.body);
    m_restoring = false;
    setKind(c.kind);
    m_mode->setCurrentIndex(0);
    m_editorStack->setCurrentWidget(m_editor);
    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
    updateTarget();
    updateSubmit();
    setActive(c.id);
    saveDraft();
}

void CommentsPane::cancelEditing()
{
    m_draftTimer.stop();
    m_editingId = 0;
    m_target = CommentTarget{};
    m_restoring = true;
    m_editor->clear();
    m_restoring = false;
    setKind(CommentKind::Reflection);
    m_mode->setCurrentIndex(0);
    m_editorStack->setCurrentWidget(m_editor);
    clearDraft();
    updateTarget();
    updateSubmit();
}

void CommentsPane::submit()
{
    const QString body = m_editor->toPlainText().trimmed();
    if (body.isEmpty())
        return;
    emit submitted(m_editingId, m_target, kind(), body);
}

void CommentsPane::saveDraft()
{
    m_draftTimer.stop();
    const QString body = m_editor->toPlainText();
    if (body.trimmed().isEmpty() && m_editingId == 0) {
        clearDraft();
        return;
    }
    m_settings.setValue(draftKey(m_documentId),
                        QJsonObject{{QStringLiteral("body"), body},
                                    {QStringLiteral("kind"), commentKindKey(kind())},
                                    {QStringLiteral("editing"), double(m_editingId)},
                                    {QStringLiteral("target"), m_target.toJson()}});
}

void CommentsPane::clearDraft()
{
    if (!m_settings.value(draftKey(m_documentId)).isUndefined() && !m_settings.value(draftKey(m_documentId)).isNull())
        m_settings.setValue(draftKey(m_documentId), QJsonValue());
}

void CommentsPane::restoreDraft()
{
    const QJsonObject d = m_settings.value(draftKey(m_documentId)).toObject();
    if (d.isEmpty())
        return;
    m_restoring = true;
    m_editingId = qint64(d.value(QStringLiteral("editing")).toDouble());
    m_target = CommentTarget::fromJson(d.value(QStringLiteral("target")).toObject());
    m_editor->setPlainText(d.value(QStringLiteral("body")).toString());
    setKind(commentKindFromKey(d.value(QStringLiteral("kind")).toString()).value_or(CommentKind::Reflection));
    m_restoring = false;
}

void CommentsPane::showUndo(const QString& message)
{
    m_undoText->setText(message);
    m_undoBar->show();
    m_undoTimer.start();
}

void CommentsPane::hideUndo()
{
    m_undoTimer.stop();
    m_undoBar->hide();
}

}  // namespace studylab::ui
