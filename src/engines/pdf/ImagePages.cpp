#include "engines/pdf/ImagePages.h"

#include <QFileInfo>
#include <QImageReader>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>

#include <algorithm>

namespace studylab::ImagePages {

namespace {

constexpr double kDefaultDpi = 150;
constexpr double kMaxSidePt = 1684;  // A2 : au-delà, la page est réduite (photos d'appareil sans résolution utile)

/// Taille de la page en points pour une image : sa résolution si elle est plausible, sinon 150 dpi.
QSizeF pagePoints(const QImage& img)
{
    double dpiX = img.dotsPerMeterX() * 0.0254;
    double dpiY = img.dotsPerMeterY() * 0.0254;
    if (dpiX < 50 || dpiX > 2400 || dpiY < 50 || dpiY > 2400)
        dpiX = dpiY = kDefaultDpi;
    QSizeF pt(img.width() * 72.0 / dpiX, img.height() * 72.0 / dpiY);
    const double longest = std::max(pt.width(), pt.height());
    if (longest > kMaxSidePt)
        pt *= kMaxSidePt / longest;
    return pt.expandedTo(QSizeF(36, 36));
}

}  // namespace

QStringList extensions()
{
    static const QStringList wanted = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                       QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp"),
                                       QStringLiteral("tif"), QStringLiteral("tiff")};
    QStringList out;
    const auto supported = QImageReader::supportedImageFormats();
    for (const QString& ext : wanted)
        if (supported.contains(ext.toLatin1()))
            out << ext;
    return out;
}

bool isImageFile(const QString& path)
{
    return extensions().contains(QFileInfo(path).suffix().toLower());
}

int pageCount(const QString& path)
{
    QImageReader reader(path);
    if (!reader.canRead())
        return 0;
    const int n = reader.imageCount();
    return n > 0 ? n : 1;
}

QImage page(const QString& path, int index)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);  // orientation EXIF des photos
    if (index > 0 && !reader.jumpToImage(index)) {
        for (int i = 0; i < index; ++i)
            if (!reader.jumpToNextImage())
                return {};
    }
    return reader.read();
}

Result<DocumentMetadata> metadata(const QString& path, int coverWidth)
{
    const int count = pageCount(path);
    if (count <= 0)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Image illisible ou format non pris en charge."));
    DocumentMetadata md;
    md.title = titleFromFileName(path);
    md.pageCount = count;
    if (coverWidth > 0) {
        const QImage first = page(path, 0);
        if (first.isNull())
            return Error::make(Error::Code::InvalidArgument, QStringLiteral("Image illisible."));
        QImage cover(first.size(), QImage::Format_RGB32);
        cover.fill(Qt::white);
        {
            QPainter p(&cover);
            p.drawImage(0, 0, first);
        }
        const int width = std::min(coverWidth, cover.width());
        md.cover = cover.scaledToWidth(width, Qt::SmoothTransformation);
        if (md.cover.height() > coverWidth * 3)
            md.cover = md.cover.copy(0, 0, md.cover.width(), coverWidth * 3);
    }
    return md;
}

Result<void> writePdf(const QString& imagePath, const QString& pdfPath)
{
    const int count = pageCount(imagePath);
    if (count <= 0)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Image illisible : %1").arg(imagePath));
    QSaveFile file(pdfPath);
    if (!file.open(QIODevice::WriteOnly))
        return Error::make(Error::Code::Io, QStringLiteral("Écriture impossible de %1 : %2").arg(pdfPath, file.errorString()));
    {
        QPdfWriter writer(&file);
        writer.setCreator(QStringLiteral("StudyLab"));
        writer.setTitle(QFileInfo(imagePath).fileName());
        writer.setResolution(300);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter painter;
        for (int i = 0; i < count; ++i) {
            const QImage img = page(imagePath, i);
            if (img.isNull())
                continue;
            const QPageSize size(pagePoints(img), QPageSize::Point, QString(), QPageSize::ExactMatch);
            writer.setPageSize(size);
            if (!painter.isActive()) {
                if (!painter.begin(&writer))
                    return Error::make(Error::Code::Io, QStringLiteral("Création du PDF impossible."));
            } else {
                writer.newPage();
            }
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRectF(0, 0, writer.width(), writer.height()), img);
        }
        if (!painter.isActive())
            return Error::make(Error::Code::InvalidArgument, QStringLiteral("Image illisible : %1").arg(imagePath));
        painter.end();
    }
    if (!file.commit())
        return Error::make(Error::Code::Io, QStringLiteral("Écriture impossible de %1 : %2").arg(pdfPath, file.errorString()));
    return {};
}

}  // namespace studylab::ImagePages
