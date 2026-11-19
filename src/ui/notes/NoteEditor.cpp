#include "ui/notes/NoteEditor.h"

#include <QAbstractItemView>
#include <QBuffer>
#include <QCompleter>
#include <QKeyEvent>
#include <QStringListModel>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QUrl>

namespace studylab::ui {

namespace {

const QStringList kImageSuffixes{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                 QStringLiteral("gif"), QStringLiteral("webp")};

/// Fichiers image locaux d'un glisser-déposer ou d'un copier depuis le gestionnaire de fichiers.
QStringList imageFiles(const QMimeData* source)
{
    QStringList out;
    if (!source || !source->hasUrls())
        return out;
    for (const QUrl& url : source->urls()) {
        if (!url.isLocalFile())
            continue;
        const QFileInfo info(url.toLocalFile());
        if (info.isFile() && kImageSuffixes.contains(info.suffix().toLower()))
            out << info.absoluteFilePath();
    }
    return out;
}

}  // namespace

NoteEditor::NoteEditor(QWidget* parent) : QPlainTextEdit(parent)
{
    m_completions = new QStringListModel(this);
    m_completer = new QCompleter(m_completions, this);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->setMaxVisibleItems(8);
    m_completer->popup()->setObjectName(QStringLiteral("LinkCompletion"));
    connect(m_completer, qOverload<const QString&>(&QCompleter::activated), this, &NoteEditor::insertCompletion);
}

void NoteEditor::setLinkCompletions(std::function<QStringList()> provider)
{
    m_provider = std::move(provider);
}

QString NoteEditor::linkPrefix() const
{
    const QTextCursor c = textCursor();
    if (c.hasSelection())
        return {};
    const QString before = c.block().text().left(c.positionInBlock());
    const qsizetype open = before.lastIndexOf(QLatin1String("[["));
    if (open < 0)
        return {};
    const QString typed = before.mid(open + 2);
    // Le lien est fermé, ou l'on écrit déjà la section, le bloc ou l'alias : plus de proposition.
    static const QRegularExpression stop(QStringLiteral("[\\]\\[|#^]"));
    if (typed.contains(stop))
        return {};
    return typed.isNull() ? QStringLiteral("") : typed;
}

void NoteEditor::updateCompletion()
{
    const QString prefix = linkPrefix();
    if (prefix.isNull() || !m_provider) {
        m_completer->popup()->hide();
        return;
    }
    if (!m_completer->popup()->isVisible())
        m_completions->setStringList(m_provider());
    m_completer->setCompletionPrefix(prefix);
    if (m_completer->completionCount() == 0) {
        m_completer->popup()->hide();
        return;
    }
    m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    QRect r = cursorRect();
    r.setWidth(std::max(240, m_completer->popup()->sizeHintForColumn(0)
                                 + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 16));
    m_completer->complete(r);
}

void NoteEditor::insertCompletion(const QString& title)
{
    const QString prefix = linkPrefix();
    if (prefix.isNull())
        return;
    QTextCursor c = textCursor();
    c.beginEditBlock();
    c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, int(prefix.size()));
    c.insertText(title);
    // « ]] » déjà présent (lien modifié) : on le saute plutôt que de le doubler.
    const QString after = c.block().text().mid(c.positionInBlock(), 2);
    if (after == QLatin1String("]]"))
        c.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 2);
    else
        c.insertText(QStringLiteral("]]"));
    c.endEditBlock();
    setTextCursor(c);
    m_completer->popup()->hide();
}

void NoteEditor::keyPressEvent(QKeyEvent* event)
{
    if (m_completer->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Tab: {
            const QModelIndex index = m_completer->popup()->currentIndex();
            if (index.isValid()) {
                insertCompletion(index.data().toString());
                return;
            }
            break;
        }
        case Qt::Key_Escape:
            m_completer->popup()->hide();
            return;
        default:
            break;
        }
    }
    QPlainTextEdit::keyPressEvent(event);
    if (!event->text().isEmpty() || event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Left
        || event->key() == Qt::Key_Right)
        updateCompletion();
}

void NoteEditor::goToLine(int line)
{
    const QTextBlock block = document()->findBlockByNumber(std::max(0, line));
    if (!block.isValid())
        return;
    QTextCursor c(block);
    setTextCursor(c);
    // Ligne visée en haut de la zone visible, avec un peu de contexte au-dessus.
    verticalScrollBar()->setValue(std::max(0, line - 2));
    ensureCursorVisible();
}

bool NoteEditor::canInsertFromMimeData(const QMimeData* source) const
{
    return (source && source->hasImage()) || !imageFiles(source).isEmpty() || QPlainTextEdit::canInsertFromMimeData(source);
}

void NoteEditor::insertFromMimeData(const QMimeData* source)
{
    const QStringList files = imageFiles(source);
    if (!files.isEmpty()) {
        for (const QString& path : files) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly))
                emit imageInserted(f.readAll(), QFileInfo(path).suffix().toLower());
        }
        return;
    }
    if (source && source->hasImage()) {
        const QImage image = qvariant_cast<QImage>(source->imageData());
        if (!image.isNull()) {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            emit imageInserted(png, QStringLiteral("png"));
            return;
        }
    }
    QPlainTextEdit::insertFromMimeData(source);
}

}  // namespace studylab::ui
