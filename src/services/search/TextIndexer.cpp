#include "services/search/TextIndexer.h"

#include "engines/pdf/ImagePages.h"

#include "core/Log.h"
#include "engines/epub/EpubTextExtractor.h"
#include "engines/pdf/PdfTextExtractor.h"

#include <QDir>
#include <QtConcurrent/QtConcurrentRun>

namespace studylab {

TextIndexer::TextIndexer(db::Database& db, QString libraryDir, QObject* parent)
    : QObject(parent), m_repo(db), m_libraryDir(std::move(libraryDir)),
      m_watcher(new QFutureWatcher<Result<QStringList>>(this))
{
    m_start.setSingleShot(true);
    connect(&m_start, &QTimer::timeout, this, &TextIndexer::next);
    connect(m_watcher, &QFutureWatcher<Result<QStringList>>::finished, this, &TextIndexer::onExtracted);
}

TextIndexer::~TextIndexer()
{
    if (m_cancel)
        m_cancel->store(true);
    m_watcher->waitForFinished();
}

void TextIndexer::schedule(int delayMs)
{
    if (m_running || m_start.isActive())
        return;
    m_start.start(std::max(0, delayMs));
}

Result<void> TextIndexer::reindexAll()
{
    if (auto r = m_repo.resetAll(); !r)
        return r;
    const auto p = progress();
    emit progressChanged(p.first, p.second);
    schedule(0);
    return {};
}

void TextIndexer::stop()
{
    m_start.stop();
    if (m_cancel)
        m_cancel->store(true);
}

QPair<int, int> TextIndexer::progress()
{
    auto p = m_repo.progress();
    return p ? p.value() : qMakePair(0, 0);
}

Result<QStringList> TextIndexer::extract(const QString& path, const QString& format, const std::atomic_bool* cancel)
{
    if (format == QLatin1String("pdf"))
        return PdfTextExtractor::extract(path, cancel);
    if (format == QLatin1String("epub") || format == QLatin1String("txt") || format == QLatin1String("md") ||
        format == QLatin1String("docx"))
        return EpubTextExtractor::extract(path, cancel);  // TXT / Markdown / DOCX : mêmes sections que le lecteur
    if (format == QLatin1String("image")) {
        // Pas de texte : une page vide par image, que l'OCR remplira.
        const int count = ImagePages::pageCount(path);
        if (count <= 0)
            return Error::make(Error::Code::Io, QStringLiteral("Image illisible : %1").arg(path));
        return QStringList(count, QString());
    }
    return Error::make(Error::Code::InvalidArgument, QStringLiteral("Format non pris en charge : %1").arg(format));
}

void TextIndexer::next()
{
    if (m_running)
        return;
    auto pending = m_repo.pending();
    if (!pending || pending.value().isEmpty()) {
        if (!pending)
            qCWarning(lcDb) << "Indexation du texte :" << pending.error().message;
        emit finished();
        return;
    }
    const db::PendingTextDocument doc = pending.value().first();
    m_running = true;
    m_current = doc.id;
    const QString path = doc.linked ? doc.filePath : QDir(m_libraryDir).filePath(doc.filePath);
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancel;
    const QString format = doc.format;
    m_watcher->setFuture(QtConcurrent::run([path, format, cancel] { return extract(path, format, cancel.get()); }));
}

void TextIndexer::onExtracted()
{
    m_running = false;
    const bool cancelled = m_cancel && m_cancel->load();
    const Result<QStringList> text = m_watcher->result();
    if (cancelled)
        return;  // arrêt demandé : le document reste en attente
    Result<void> stored = text ? m_repo.replace(m_current, text.value()) : m_repo.markFailed(m_current);
    if (!text)
        qCInfo(lcDb) << "Texte non extrait du document" << m_current << ":" << text.error().message;
    if (!stored)
        qCWarning(lcDb) << "Indexation du texte :" << stored.error().message;
    const auto p = progress();
    emit progressChanged(p.first, p.second);
    if (!stored) {
        emit finished();  // base indisponible : on ne boucle pas
        return;
    }
    m_start.start(0);
}

}  // namespace studylab
