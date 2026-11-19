#pragma once

#include "core/Result.h"
#include "engines/document/DocumentMetadata.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <memory>

class QZipReader;

namespace studylab {

/// Élément du manifeste OPF.
struct EpubManifestItem {
    QString id;
    QString href;       ///< chemin complet dans l'archive (résolu depuis le dossier de l'OPF)
    QString mediaType;
    QString properties;
};

/// Paquet EPUB 2/3 : conteneur ZIP, fichier OPF (métadonnées, manifeste, spine).
/// Lecture seule ; utilisable depuis un thread de travail.
class EpubPackage {
public:
    /// Ouvre et vérifie le paquet (type MIME, container.xml, OPF, absence de DRM).
    static Result<std::unique_ptr<EpubPackage>> open(const QString& path);
    ~EpubPackage();
    EpubPackage(const EpubPackage&) = delete;
    EpubPackage& operator=(const EpubPackage&) = delete;

    /// Métadonnées OPF ; la couverture est mise à la largeur demandée (0 = pas de couverture).
    DocumentMetadata metadata(int coverWidth = kCoverWidth) const;

    QString opfPath() const { return m_opfPath; }
    QString version() const { return m_version; }
    const QList<EpubManifestItem>& manifest() const { return m_manifest; }
    const QList<EpubManifestItem>& spine() const { return m_spine; }
    QString coverHref() const { return m_coverHref; }

    /// Contenu d'un fichier de l'archive (chemin complet), vide s'il n'existe pas.
    QByteArray fileData(const QString& pathInArchive) const;
    /// Taille décompressée de chaque fichier de l'archive (chemin complet → octets).
    QHash<QString, qint64> fileSizes() const;

private:
    EpubPackage() = default;
    Result<void> parseOpf(const QByteArray& opf, const QString& title);

    std::unique_ptr<QZipReader> m_zip;
    QString m_opfPath;
    QString m_version;
    DocumentMetadata m_md;
    QList<EpubManifestItem> m_manifest;
    QList<EpubManifestItem> m_spine;
    QString m_coverHref;
};

}  // namespace studylab
