#include "services/library/ImportWorker.h"

#include "engines/pdf/ImagePages.h"

#include "core/Uuid.h"
#include "engines/epub/EpubPackage.h"
#include "engines/pdf/PdfMetadataReader.h"
#include "services/library/FileInspector.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>

namespace studylab::ImportWorker {

namespace {

constexpr qint64 kDiskMargin = 50LL * 1024 * 1024;  // garder au moins 50 Mo libres

Result<void> copyAtomically(const QString& source, const QString& target)
{
    if (QFileInfo::exists(target) && QFileInfo(target).size() == QFileInfo(source).size())
        return {};  // déjà présent (document retiré puis réimporté)
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        return Error::make(Error::Code::Io, QStringLiteral("Impossible de créer le dossier de la bibliothèque."));
    const QString part = target + QStringLiteral(".part");
    QFile::remove(part);
    if (!QFile::copy(source, part))
        return Error::make(Error::Code::Io, QStringLiteral("Copie du fichier dans la bibliothèque impossible."));
    QFile::remove(target);
    if (!QFile::rename(part, target)) {
        QFile::remove(part);
        return Error::make(Error::Code::Io, QStringLiteral("Copie du fichier dans la bibliothèque impossible."));
    }
    return {};
}

}  // namespace

QStringList expandPaths(const QStringList& inputs)
{
    QStringList out;
    QSet<QString> seen;
    auto add = [&](const QString& p) {
        const QString abs = QFileInfo(p).absoluteFilePath();
        if (!seen.contains(abs)) {
            seen.insert(abs);
            out << abs;
        }
    };
    for (const QString& input : inputs) {
        const QFileInfo fi(input);
        if (fi.isDir()) {
            QStringList files;
            QDirIterator it(fi.absoluteFilePath(), QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString f = it.next();
                if (FileInspector::hasSupportedExtension(f))
                    files << f;
            }
            std::sort(files.begin(), files.end(),
                      [](const QString& a, const QString& b) { return QString::localeAwareCompare(a, b) < 0; });
            for (const QString& f : std::as_const(files))
                add(f);
        } else {
            add(input);
        }
    }
    return out;
}

QString managedRelativePath(const QString& sha256, DocumentFormat format, const QString& sourceSuffix)
{
    // Une image garde son extension (png, jpg…) : les modules d'image s'en servent et l'utilisateur la reconnaît.
    const QString ext = format == DocumentFormat::Image && !sourceSuffix.isEmpty() ? sourceSuffix.toLower()
                                                                                    : documentFormatKey(format);
    return sha256.left(2) + QLatin1Char('/') + sha256 + QLatin1Char('.') + ext;
}

ImportCandidate prepare(const QString& path, const AppPaths& paths, StorageMode mode,
                        const QSet<QString>& libraryHashes, QSet<QString>& batchHashes)
{
    ImportCandidate c;
    c.sourcePath = path;
    const QFileInfo fi(path);
    if (!fi.exists()) {
        c.error = QStringLiteral("Fichier introuvable.");
        return c;
    }
    if (!fi.isReadable()) {
        c.error = QStringLiteral("Fichier illisible (droits d'accès).");
        return c;
    }
    if (fi.size() == 0) {
        c.error = QStringLiteral("Fichier vide.");
        return c;
    }
    const auto format = FileInspector::detectFormat(path);
    if (!format) {
        c.error = QStringLiteral("Format non pris en charge (PDF, EPUB, DOCX, TXT, Markdown et images).");
        return c;
    }
    auto hash = FileInspector::sha256(path);
    if (!hash) {
        c.error = hash.error().message;
        return c;
    }
    if (libraryHashes.contains(hash.value()) || batchHashes.contains(hash.value())) {
        c.duplicateHash = hash.value();
        c.duplicateInSelection = !libraryHashes.contains(hash.value());
        return c;
    }

    DocumentMetadata md;
    if (*format == DocumentFormat::Pdf) {
        auto r = PdfMetadataReader::read(path);
        if (!r) {
            c.error = r.error().message;
            return c;
        }
        md = std::move(r.value());
    } else if (*format == DocumentFormat::Image) {
        auto r = ImagePages::metadata(path);
        if (!r) {
            c.error = r.error().message;
            return c;
        }
        md = std::move(r.value());
    } else {
        auto pkg = EpubPackage::open(path);
        if (!pkg) {
            c.error = pkg.error().message;
            return c;
        }
        md = pkg.value()->metadata();
    }

    DocumentRecord r;
    r.uuid = newUuid();
    r.title = md.title.isEmpty() ? titleFromFileName(path) : md.title;
    r.subtitle = md.subtitle;
    r.authors = md.authors;
    r.publisher = md.publisher;
    r.year = md.year;
    r.language = md.language;
    r.description = md.description;
    r.isbn = md.isbn;
    r.doi = md.doi;
    r.tags = md.keywords;
    r.format = *format;
    r.storageMode = mode;
    r.fileHash = hash.value();
    r.fileSize = fi.size();
    r.pageCount = md.pageCount;

    if (mode == StorageMode::Managed) {
        const QStorageInfo storage(paths.libraryDir());
        if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < fi.size() + kDiskMargin) {
            c.error = QStringLiteral("Espace disque insuffisant pour copier ce fichier.");
            return c;
        }
        r.filePath = managedRelativePath(r.fileHash, r.format, fi.suffix());
        if (auto copied = copyAtomically(path, QDir(paths.libraryDir()).filePath(r.filePath)); !copied) {
            c.error = copied.error().message;
            return c;
        }
    } else {
        r.filePath = fi.canonicalFilePath();
    }

    if (!md.cover.isNull()) {
        const QString coverName = r.uuid + QStringLiteral(".jpg");
        QDir().mkpath(paths.coversDir());
        if (md.cover.save(QDir(paths.coversDir()).filePath(coverName), "JPG", 85))
            r.coverPath = coverName;
    }

    batchHashes.insert(r.fileHash);
    c.record = std::move(r);
    return c;
}

}  // namespace studylab::ImportWorker
