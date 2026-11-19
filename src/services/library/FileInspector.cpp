#include "services/library/FileInspector.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace studylab {

std::optional<DocumentFormat> FileInspector::detectFormat(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray head = f.read(1024);
    // La signature PDF peut être précédée de quelques octets parasites (toléré par les lecteurs).
    if (head.indexOf("%PDF-") >= 0)
        return DocumentFormat::Pdf;
    // EPUB : ZIP (« PK\3\4 ») dont la première entrée est « mimetype » = application/epub+zip.
    // Les EPUB mal empaquetés (mimetype ailleurs) sont acceptés si l'extension est .epub ; EpubPackage vérifie ensuite.
    if (head.startsWith("PK\x03\x04")) {
        if (head.indexOf("mimetypeapplication/epub+zip") >= 0
            || QFileInfo(path).suffix().compare(QLatin1String("epub"), Qt::CaseInsensitive) == 0)
            return DocumentFormat::Epub;
    }
    return std::nullopt;
}

bool FileInspector::hasSupportedExtension(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("pdf") || ext == QLatin1String("epub");
}

Result<QString> FileInspector::sha256(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return Error::make(Error::Code::Io, QStringLiteral("Lecture impossible : %1").arg(f.errorString()));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return Error::make(Error::Code::Io, QStringLiteral("Lecture interrompue : %1").arg(f.errorString()));
    return QString::fromLatin1(hash.result().toHex());
}

}  // namespace studylab
