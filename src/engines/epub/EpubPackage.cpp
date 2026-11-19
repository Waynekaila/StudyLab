#include "engines/epub/EpubPackage.h"

#include <private/qzipreader_p.h>

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QXmlStreamReader>

namespace studylab {

namespace {

// Obfuscation de polices autorisée par la norme : ce n'est pas un DRM.
bool isFontObfuscation(QStringView algorithm)
{
    return algorithm == u"http://www.idpf.org/2008/embedding" || algorithm == u"http://ns.adobe.com/pdf/enc#RC";
}

QString resolveHref(const QString& baseDir, const QString& href)
{
    QString decoded = QUrl::fromPercentEncoding(href.section(QLatin1Char('#'), 0, 0).toUtf8());
    QStringList parts = baseDir.isEmpty() ? QStringList{} : baseDir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& seg : decoded.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (seg == QLatin1String(".."))
            parts.isEmpty() ? void() : parts.removeLast();
        else if (seg != QLatin1String("."))
            parts << seg;
    }
    return parts.join(QLatin1Char('/'));
}

QString plainText(const QString& maybeHtml)
{
    if (!maybeHtml.contains(QLatin1Char('<')))
        return maybeHtml.simplified();
    return QTextDocumentFragment::fromHtml(maybeHtml).toPlainText().simplified();
}

}  // namespace

EpubPackage::~EpubPackage() = default;

Result<std::unique_ptr<EpubPackage>> EpubPackage::open(const QString& path)
{
    std::unique_ptr<EpubPackage> pkg(new EpubPackage);
    pkg->m_zip = std::make_unique<QZipReader>(path);
    if (!pkg->m_zip->isReadable() || pkg->m_zip->status() != QZipReader::NoError)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Archive EPUB illisible ou endommagée."));

    const QByteArray mime = pkg->m_zip->fileData(QStringLiteral("mimetype")).trimmed();
    if (mime != "application/epub+zip")
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Ce fichier ZIP n'est pas un EPUB."));

    // DRM : chiffrement autre que l'obfuscation de polices
    const QByteArray enc = pkg->m_zip->fileData(QStringLiteral("META-INF/encryption.xml"));
    if (!enc.isEmpty()) {
        QXmlStreamReader x(enc);
        while (!x.atEnd()) {
            if (x.readNext() == QXmlStreamReader::StartElement && x.name() == u"EncryptionMethod"
                && !isFontObfuscation(x.attributes().value(QStringLiteral("Algorithm"))))
                return Error::make(Error::Code::InvalidArgument,
                                   QStringLiteral("EPUB protégé par un DRM : il ne peut pas être lu."));
        }
    }

    const QByteArray container = pkg->m_zip->fileData(QStringLiteral("META-INF/container.xml"));
    QXmlStreamReader cx(container);
    while (!cx.atEnd()) {
        if (cx.readNext() == QXmlStreamReader::StartElement && cx.name() == u"rootfile") {
            pkg->m_opfPath = cx.attributes().value(QStringLiteral("full-path")).toString();
            break;
        }
    }
    if (pkg->m_opfPath.isEmpty())
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("EPUB invalide : container.xml absent ou vide."));
    const QByteArray opf = pkg->m_zip->fileData(pkg->m_opfPath);
    if (opf.isEmpty())
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("EPUB invalide : fichier OPF introuvable."));
    if (auto r = pkg->parseOpf(opf, titleFromFileName(path)); !r)
        return r.error();
    return pkg;
}

Result<void> EpubPackage::parseOpf(const QByteArray& opf, const QString& fallbackTitle)
{
    const QString baseDir = m_opfPath.contains(QLatin1Char('/')) ? m_opfPath.section(QLatin1Char('/'), 0, -2) : QString();
    QXmlStreamReader x(opf);
    QString coverId;
    QStringList spineIds;
    QHash<QString, QString> roles;  // id de dc:creator → rôle (EPUB 3 : <meta refines="#id" property="role">)
    QList<QPair<QString, QString>> creators;  // (id, nom) dans l'ordre
    bool inMetadata = false;

    while (!x.atEnd()) {
        const auto token = x.readNext();
        if (token == QXmlStreamReader::EndElement && x.name() == u"metadata")
            inMetadata = false;
        if (token != QXmlStreamReader::StartElement)
            continue;
        const QStringView name = x.name();
        const auto attrs = x.attributes();
        if (name == u"package") {
            m_version = attrs.value(QStringLiteral("version")).toString();
        } else if (name == u"metadata") {
            inMetadata = true;
        } else if (inMetadata && name == u"meta") {
            if (attrs.value(QStringLiteral("name")) == u"cover")
                coverId = attrs.value(QStringLiteral("content")).toString();
            const QString refines = attrs.value(QStringLiteral("refines")).toString();
            const QString property = attrs.value(QStringLiteral("property")).toString();
            if (property == QLatin1String("role") && refines.startsWith(QLatin1Char('#')))
                roles.insert(refines.mid(1), x.readElementText().trimmed());
        } else if (inMetadata && x.namespaceUri() == u"http://purl.org/dc/elements/1.1/") {
            const QString id = attrs.value(QStringLiteral("id")).toString();
            const QString role = attrs.value(QStringLiteral("http://www.idpf.org/2007/opf"), QStringLiteral("role")).toString();
            const QString text = x.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            if (text.isEmpty())
                continue;
            if (name == u"title" && m_md.title.isEmpty())
                m_md.title = text;
            else if (name == u"creator") {
                if (!role.isEmpty())
                    roles.insert(id.isEmpty() ? text : id, role);
                creators.append({id.isEmpty() ? text : id, text});
            } else if (name == u"publisher" && m_md.publisher.isEmpty())
                m_md.publisher = text;
            else if (name == u"language" && m_md.language.isEmpty())
                m_md.language = text.section(QLatin1Char('-'), 0, 0).toLower();
            else if (name == u"description" && m_md.description.isEmpty())
                m_md.description = plainText(text);
            else if (name == u"date" && m_md.year == 0) {
                static const QRegularExpression year(QStringLiteral("\\b(1[5-9]\\d\\d|20\\d\\d)\\b"));
                if (const auto m = year.match(text); m.hasMatch())
                    m_md.year = m.captured(1).toInt();
            } else if (name == u"identifier") {
                if (m_md.isbn.isEmpty())
                    m_md.isbn = extractIsbn(text);
                if (m_md.doi.isEmpty())
                    m_md.doi = extractDoi(text);
            } else if (name == u"subject" && text.size() <= 40)
                m_md.keywords << text;
        } else if (name == u"item") {
            EpubManifestItem item;
            item.id = attrs.value(QStringLiteral("id")).toString();
            item.href = resolveHref(baseDir, attrs.value(QStringLiteral("href")).toString());
            item.mediaType = attrs.value(QStringLiteral("media-type")).toString();
            item.properties = attrs.value(QStringLiteral("properties")).toString();
            m_manifest << item;
        } else if (name == u"itemref") {
            spineIds << attrs.value(QStringLiteral("idref")).toString();
        }
    }
    if (x.hasError())
        return Error::make(Error::Code::InvalidArgument,
                           QStringLiteral("EPUB invalide : OPF mal formé (%1).").arg(x.errorString()));

    for (const auto& [id, nameText] : creators) {
        const QString role = roles.value(id);
        if (role.isEmpty() || role == QLatin1String("aut"))
            m_md.authors << nameText;
    }
    if (m_md.title.isEmpty())
        m_md.title = fallbackTitle;

    QHash<QString, int> byId;
    for (int i = 0; i < m_manifest.size(); ++i)
        byId.insert(m_manifest.at(i).id, i);
    for (const QString& id : spineIds)
        if (const auto it = byId.constFind(id); it != byId.cend())
            m_spine << m_manifest.at(*it);
    if (m_spine.isEmpty())
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("EPUB invalide : aucun chapitre dans le spine."));
    m_md.pageCount = int(m_spine.size());

    // Couverture : EPUB 3 (properties="cover-image"), puis EPUB 2 (<meta name="cover">), puis heuristique
    for (const auto& it : std::as_const(m_manifest))
        if (it.properties.split(QLatin1Char(' ')).contains(QLatin1String("cover-image")))
            m_coverHref = it.href;
    if (m_coverHref.isEmpty() && byId.contains(coverId))
        m_coverHref = m_manifest.at(byId.value(coverId)).href;
    if (m_coverHref.isEmpty())
        for (const auto& it : std::as_const(m_manifest))
            if (it.mediaType.startsWith(QLatin1String("image/")) && it.href.contains(QLatin1String("cover"), Qt::CaseInsensitive)) {
                m_coverHref = it.href;
                break;
            }
    return {};
}

DocumentMetadata EpubPackage::metadata(int coverWidth) const
{
    DocumentMetadata md = m_md;
    if (coverWidth > 0 && !m_coverHref.isEmpty()) {
        const QImage img = QImage::fromData(fileData(m_coverHref));
        if (!img.isNull())
            md.cover = img.width() > coverWidth ? img.scaledToWidth(coverWidth, Qt::SmoothTransformation) : img;
    }
    return md;
}

QByteArray EpubPackage::fileData(const QString& pathInArchive) const
{
    return m_zip->fileData(pathInArchive);
}

}  // namespace studylab
