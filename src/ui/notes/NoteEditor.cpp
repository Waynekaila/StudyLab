#include "ui/notes/NoteEditor.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
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

NoteEditor::NoteEditor(QWidget* parent) : QPlainTextEdit(parent) {}

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
