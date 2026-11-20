#pragma once

#include "core/Result.h"
#include "engines/document/DocumentMetadata.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <memory>

class QBuffer;
class QZipReader;

namespace studylab {

/// Élément du manifeste OPF.
struct EpubManifestItem {
    QString id;
    QString href;       ///< chemin complet dans l'archive (résolu depuis le dossier de l'OPF)
    QString mediaType;
    QString properties;
};

/// Entrée de la table des matières (nav EPUB 3 ou NCX EPUB 2).
struct EpubNavPoint {
    QString label;
    QString href;     ///< chemin complet dans l'archive, sans fragment
    int depth = 0;    ///< 0 = premier niveau
    int section = -1; ///< index dans le spine, -1 si le fichier n'y figure pas
};

/// Paquet EPUB 2/3 : conteneur ZIP, fichier OPF (métadonnées, manifeste, spine).
/// Lecture seule ; utilisable depuis un thread de travail.
class EpubPackage {
public:
    /// Ouvre et vérifie le paquet (type MIME, container.xml, OPF, absence de DRM). Un fichier .txt ou .md est
    /// d'abord mis en forme de livre, en mémoire (TextBook).
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
    /// Table des matières lue dans le document nav (EPUB 3) ou, à défaut, le NCX (EPUB 2).
    QList<EpubNavPoint> toc() const;

    /// Taille décompressée de chaque fichier de l'archive (chemin complet → octets).
    QHash<QString, qint64> fileSizes() const;

private:
    EpubPackage() = default;
    Result<void> parseOpf(const QByteArray& opf, const QString& title);

    std::unique_ptr<QBuffer> m_buffer;  // livre construit en mémoire (texte, Markdown)
    std::unique_ptr<QZipReader> m_zip;
    QString m_opfPath;
    QString m_version;
    DocumentMetadata m_md;
    QList<EpubManifestItem> m_manifest;
    QList<EpubManifestItem> m_spine;
    QString m_coverHref;
};

}  // namespace studylab
