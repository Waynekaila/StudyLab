#include "ui/reader/ReaderTab.h"

#include "services/annotations/AnnotationService.h"
#include "services/export/MarkdownExporter.h"
#include "services/library/LibraryService.h"
#include "services/settings/SettingsService.h"
#include "ui/reader/AnnotationPopup.h"
#include "ui/reader/AnnotationStyle.h"
#include "ui/reader/AnnotationsPane.h"
#include "ui/shell/AppContext.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStandardPaths>
#include <QToolButton>
#include <QUrl>

#include <algorithm>

namespace studylab::ui {

ReaderTab::ReaderTab(AppContext& ctx, qint64 documentId, QWidget* parent)
    : QWidget(parent),
      m_ctx(ctx),
      m_library(ctx.library),
      m_annotations(ctx.annotations),
      m_settings(ctx.settings),
      m_theme(ctx.theme),
      m_documentId(documentId)
{
    if (auto r = m_library.document(documentId))
        m_record = r.value();
    m_color = annotationColorFromKey(m_settings.value(QStringLiteral("reader/annotationColor")).toString())
                  .value_or(AnnotationColor::Yellow);
}

ReaderTab::~ReaderTab() = default;

QToolButton* ReaderTab::makeToolButton(const QString& tooltip, QWidget* parent, bool checkable)
{
    auto* b = new QToolButton(parent);
    b->setToolTip(tooltip);
    b->setAccessibleName(tooltip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setProperty("readerTool", true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

void ReaderTab::addColorButtons(QHBoxLayout* layout, QWidget* parent)
{
    auto* colors = new QButtonGroup(parent);
    colors->setExclusive(true);
    for (AnnotationColor c : allAnnotationColors()) {
        auto* b = makeToolButton(tr("Couleur : %1").arg(annotationColorLabel(c).toLower()), parent, true);
        b->setProperty("swatch", true);
        b->setChecked(c == m_color);
        colors->addButton(b);
        connect(b, &QToolButton::clicked, this, [this, c] { setCurrentColor(c); });
        layout->addWidget(b);
        m_colorButtons << b;
    }
}

QToolButton* ReaderTab::addPaneToggle(QHBoxLayout* layout, QWidget* parent)
{
    m_paneToggle = makeToolButton(tr("Volet Annotations"), parent, true);
    connect(m_paneToggle, &QToolButton::clicked, this, [this](bool on) { setPaneVisible(on); });
    layout->addWidget(m_paneToggle);
    return m_paneToggle;
}

void ReaderTab::refreshColorIcons()
{
    const Palette& pal = m_theme.palette();
    const auto colors = allAnnotationColors();
    for (int i = 0; i < m_colorButtons.size(); ++i)
        m_colorButtons[i]->setIcon(colorSwatchIcon(annotationAccent(pal, colors[i]), pal.text2, 20));
    if (m_paneToggle)
        m_paneToggle->setIcon(Icons::stateful(QStringLiteral("panel-right"), pal.text2, pal.primaryStrong, 18));
}

void ReaderTab::initAnnotations()
{
    connect(m_popup, &AnnotationPopup::colorPicked, this, [this](AnnotationColor c) {
        if (m_popup->mode() == AnnotationPopup::Mode::Create) {
            setCurrentColor(c);
            if (auto r = annotateSelection(AnnotationType::Highlight, c); !r)
                reportError(r.error());
        } else if (auto r = m_annotations.setColor(m_popupAnnotation, c); !r) {
            reportError(r.error());
        } else {
            showEditPopup(m_popupAnnotation);
        }
    });
    connect(m_popup, &AnnotationPopup::typePicked, this, [this](AnnotationType t) {
        if (m_popup->mode() == AnnotationPopup::Mode::Create) {
            if (auto r = annotateSelection(t, m_color); !r)
                reportError(r.error());
        } else if (auto r = m_annotations.setType(m_popupAnnotation, t); !r) {
            reportError(r.error());
        } else {
            showEditPopup(m_popupAnnotation);
        }
    });
    connect(m_popup, &AnnotationPopup::noteRequested, this, [this] {
        if (m_popup->mode() == AnnotationPopup::Mode::Create) {
            auto created = annotateSelection(AnnotationType::Highlight, m_color);
            if (!created) {
                reportError(created.error());
                return;
            }
            if (!created.value().isEmpty())
                editNote(created.value().first().id);
        } else {
            editNote(m_popupAnnotation);
        }
    });
    connect(m_popup, &AnnotationPopup::copyRequested, this, [this] {
        if (m_popup->mode() == AnnotationPopup::Mode::Create) {
            copySelection();
        } else if (auto r = m_annotations.find(m_popupAnnotation)) {
            QApplication::clipboard()->setText(r.value().selectedText);
        }
        m_popup->hide();
    });
    connect(m_popup, &AnnotationPopup::deleteRequested, this, [this] { deleteAnnotation(m_popupAnnotation); });

    connect(m_pane, &AnnotationsPane::annotationActivated, this, &ReaderTab::revealAnnotation);
    connect(m_pane, &AnnotationsPane::contextMenuRequested, this, &ReaderTab::showAnnotationMenu);
    connect(m_pane, &AnnotationsPane::exportRequested, this, &ReaderTab::exportWithDialog);
    connect(m_pane, &AnnotationsPane::closeRequested, this, [this] { setPaneVisible(false); });
    connect(m_pane, &AnnotationsPane::undoRequested, this, &ReaderTab::undoDelete);
    m_pane->setGroupResolver([this](int unit) { return chapterOf(unit); });

    connect(&m_annotations, &AnnotationService::annotationAdded, this, [this](const AnnotationRecord& r) {
        if (r.documentId != m_documentId)
            return;
        bool replaced = false;
        for (auto& existing : m_records)
            if (existing.id == r.id) {
                existing = r;
                replaced = true;
            }
        if (!replaced) {
            auto pos = std::upper_bound(m_records.begin(), m_records.end(), r,
                                        [](const AnnotationRecord& a, const AnnotationRecord& b) { return a.sortKey < b.sortKey; });
            m_records.insert(pos, r);
        }
        pushMarks();
        m_pane->setActive(r.id);
        updateBookmarkButton();
    });
    connect(&m_annotations, &AnnotationService::annotationChanged, this, [this](const AnnotationRecord& r) {
        if (r.documentId != m_documentId)
            return;
        for (auto& existing : m_records)
            if (existing.id == r.id)
                existing = r;
        pushMarks();
    });
    connect(&m_annotations, &AnnotationService::annotationRemoved, this, [this](qint64 id, qint64 docId) {
        if (docId != m_documentId)
            return;
        m_records.erase(std::remove_if(m_records.begin(), m_records.end(),
                                       [id](const AnnotationRecord& r) { return r.id == id; }),
                        m_records.end());
        if (m_popupAnnotation == id)
            m_popup->hide();
        annotationRemoved(id);
        pushMarks();
        updateBookmarkButton();
    });

    auto* undo = new QShortcut(QKeySequence::Undo, this, this, [this] { (void)undoDelete(); });
    undo->setContext(Qt::WidgetWithChildrenShortcut);
    auto* bookmark = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this, this, &ReaderTab::toggleBookmark);
    bookmark->setContext(Qt::WidgetWithChildrenShortcut);
    auto* del = new QShortcut(QKeySequence::Delete, this, this, [this] {
        if (m_popup->isVisible() && m_popup->mode() == AnnotationPopup::Mode::Edit)
            deleteAnnotation(m_popupAnnotation);
    });
    del->setContext(Qt::WidgetWithChildrenShortcut);
    setPaneVisible(m_settings.value(QStringLiteral("reader/annotationsPane"), true).toBool());
}

void ReaderTab::reportError(const Error& error)
{
    QMessageBox::warning(window(), tr("Annotations"), error.message);
}

std::optional<QString> ReaderTab::askNote(const QString& quote, const QString& current)
{
    if (m_noteEditor)
        return m_noteEditor(quote, current);
    return NoteDialog::edit(window(), m_theme, quote, current);
}

QToolButton* ReaderTab::colorButton(AnnotationColor color) const
{
    return m_colorButtons.value(int(allAnnotationColors().indexOf(color)));
}

void ReaderTab::setCurrentColor(AnnotationColor color)
{
    m_color = color;
    if (QToolButton* b = colorButton(color))
        b->setChecked(true);
    m_settings.setValue(QStringLiteral("reader/annotationColor"), annotationColorKey(color));
}

void ReaderTab::setPaneVisible(bool on)
{
    m_pane->setVisible(on && !m_focusMode);
    if (m_paneToggle) {
        const QSignalBlocker block(m_paneToggle);
        m_paneToggle->setChecked(on);
    }
    m_settings.setValue(QStringLiteral("reader/annotationsPane"), on);
}

void ReaderTab::reloadAnnotations()
{
    auto list = m_annotations.list(m_documentId);
    m_records = list ? list.value() : QList<AnnotationRecord>{};
    pushMarks();
    updateBookmarkButton();
}

void ReaderTab::deleteAnnotation(qint64 id)
{
    if (id == 0)
        return;
    auto r = m_annotations.find(id);
    if (auto res = m_annotations.remove(id); !res) {
        reportError(res.error());
        return;
    }
    m_popup->hide();
    m_deleted.append(id);
    const QString what = r ? annotationTypeLabel(r.value().type) : tr("Annotation");
    m_pane->showUndo(tr("%1 supprimé%2").arg(what, r && (r.value().type == AnnotationType::Highlight
                                                           || r.value().type == AnnotationType::Underline
                                                           || r.value().type == AnnotationType::Bookmark
                                                           || r.value().type == AnnotationType::Strikeout)
                                                          ? QString()
                                                          : QStringLiteral("e")));
}

bool ReaderTab::undoDelete()
{
    while (!m_deleted.isEmpty()) {
        const qint64 id = m_deleted.takeLast();
        if (m_annotations.restore(id)) {
            m_pane->hideUndo();
            return true;
        }
    }
    return false;
}

void ReaderTab::editNote(qint64 id)
{
    auto r = m_annotations.find(id);
    if (!r)
        return;
    m_popup->hide();
    const auto text = askNote(r.value().selectedText, r.value().note);
    if (!text)
        return;
    if (auto res = m_annotations.setNote(id, *text); !res)
        reportError(res.error());
}

void ReaderTab::showAnnotationMenu(qint64 id, QPoint globalPos)
{
    auto found = m_annotations.find(id);
    if (!found)
        return;
    const AnnotationRecord a = found.value();
    QMenu menu(this);
    const Palette& pal = m_theme.palette();
    if (a.type != AnnotationType::Bookmark) {
        QMenu* colorMenu = menu.addMenu(tr("Couleur"));
        for (AnnotationColor c : allAnnotationColors()) {
            QAction* act = colorMenu->addAction(colorSwatchIcon(annotationAccent(pal, c), pal.text2, 16),
                                                annotationColorLabel(c));
            act->setCheckable(true);
            act->setChecked(c == a.color);
            connect(act, &QAction::triggered, this, [this, id, c] { (void)m_annotations.setColor(id, c); });
        }
    }
    if (a.type == AnnotationType::Highlight || a.type == AnnotationType::Underline || a.type == AnnotationType::Strikeout) {
        QMenu* typeMenu = menu.addMenu(tr("Type"));
        for (AnnotationType t : {AnnotationType::Highlight, AnnotationType::Underline, AnnotationType::Strikeout}) {
            QAction* act = typeMenu->addAction(annotationTypeLabel(t));
            act->setCheckable(true);
            act->setChecked(t == a.type);
            connect(act, &QAction::triggered, this, [this, id, t] { (void)m_annotations.setType(id, t); });
        }
    }
    if (a.type != AnnotationType::Bookmark)
        menu.addAction(a.note.isEmpty() ? tr("Ajouter une note…") : tr("Modifier la note…"), this,
                       [this, id] { editNote(id); });
    if (!a.selectedText.isEmpty())
        menu.addAction(tr("Copier le texte"), this,
                       [text = a.selectedText] { QApplication::clipboard()->setText(normalizeQuote(text)); });
    menu.addAction(tr("Aller au passage"), this, [this, id] { revealAnnotation(id); });
    menu.addSeparator();
    QAction* del = menu.addAction(Icons::get(QStringLiteral("trash"), pal.danger, 16), tr("Supprimer"), this,
                                  [this, id] { deleteAnnotation(id); });
    del->setShortcut(QKeySequence::Delete);
    menu.exec(globalPos);
}

Result<QString> ReaderTab::exportMarkdown(const QString& directory)
{
    auto doc = m_library.document(m_documentId);
    if (!doc)
        return doc.error();
    auto list = m_annotations.list(m_documentId);
    if (!list)
        return list.error();
    MarkdownExporter::Options opt;
    opt.chapterOf = [this](int unit) { return chapterOf(unit); };
    return MarkdownExporter::exportDocument(directory, doc.value(), list.value(), opt);
}

void ReaderTab::exportWithDialog()
{
    QString start = m_settings.value(QStringLiteral("export/lastDirectory")).toString();
    if (start.isEmpty() || !QDir(start).exists())
        start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString dir = QFileDialog::getExistingDirectory(window(), tr("Exporter les annotations en Markdown"), start);
    if (dir.isEmpty())
        return;
    m_settings.setValue(QStringLiteral("export/lastDirectory"), dir);
    auto res = exportMarkdown(dir);
    if (!res) {
        reportError(res.error());
        return;
    }
    QMessageBox box(window());
    box.setWindowTitle(tr("Export terminé"));
    box.setText(tr("Les annotations ont été exportées dans :\n%1").arg(QDir::toNativeSeparators(res.value())));
    QPushButton* open = box.addButton(tr("Ouvrir le dossier"), QMessageBox::AcceptRole);
    box.addButton(tr("Fermer"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == open)
        QDesktopServices::openUrl(QUrl::fromLocalFile(res.value()));
}

}  // namespace studylab::ui
