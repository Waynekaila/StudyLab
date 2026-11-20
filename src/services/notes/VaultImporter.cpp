#include "services/notes/VaultImporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace studylab {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("VaultImporter", text);
}

const QStringList& imageSuffixes()
{
    static const QStringList s{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                               QStringLiteral("gif"), QStringLiteral("webp")};
    return s;
}

bool hiddenPath(const QString& relative)
{
    for (const QString& part : relative.split(u'/', Qt::SkipEmptyParts)) {
        if (part.startsWith(u'.'))
            return true;
    }
    return false;
}

NoteType typeFrom(const QJsonObject& fm)
{
    const QString value = fm.value(QStringLiteral("type")).toString().trimmed();
    if (value.isEmpty())
        return NoteType::Personal;
    if (auto t = noteTypeFromKey(value.toLower()))
        return *t;
    for (NoteType t : allNoteTypes()) {
        if (noteTypeLabel(t).compare(value, Qt::CaseInsensitive) == 0)
            return t;
    }
    return NoteType::Personal;
}

/// Retire du front matter les lignes « uuid: » et « type: <clé> » ; le bloc vide disparaît avec la ligne vide qui suit.
QString stripImportKeys(const QString& body, const FrontMatter& fm)
{
    if (!fm.present || fm.lineCount < 2)
        return body;
    QStringList lines = body.split(u'\n');
    const int close = fm.lineCount - 1;
    if (close >= lines.size())
        return body;
    static const QRegularExpression keyLine(QStringLiteral("^(uuid|type)\\s*:\\s*(.*?)\\s*$"));
    int removed = 0;
    for (int i = close - 1; i >= 1; --i) {
        const auto m = keyLine.match(lines.at(i));
        if (!m.hasMatch())
            continue;
        if (m.captured(1) == QLatin1String("type") && !noteTypeFromKey(m.captured(2)))
            continue;
        lines.removeAt(i);
        ++removed;
    }
    if (removed == 0)
        return body;
    const int newClose = close - removed;
    bool empty = true;
    for (int i = 1; i < newClose; ++i)
        empty = empty && lines.at(i).trimmed().isEmpty();
    if (empty) {
        lines.erase(lines.begin(), lines.begin() + newClose + 1);
        if (!lines.isEmpty() && lines.first().trimmed().isEmpty())
            lines.removeFirst();
    }
    return lines.join(u'\n');
}

}  // namespace

QString VaultImporter::imageToken(int index)
{
    return QStringLiteral("⁣studylab-image-%1⁣").arg(index);
}

Result<VaultScan> VaultImporter::scan(const QString& rootPath, const QString& folderPrefix)
{
    const QDir root(rootPath);
    if (!root.exists())
        return Error::make(Error::Code::NotFound, tr("Dossier introuvable : %1").arg(rootPath));

    VaultScan scan;
    scan.studylabExport = QFileInfo(root.filePath(QStringLiteral("README.md"))).isFile()
        && QFileInfo(root.filePath(QStringLiteral("Bibliothèque"))).isDir()
        && QFileInfo(root.filePath(QStringLiteral("Notes"))).isDir();
    const QDir notesRoot = scan.studylabExport ? QDir(root.filePath(QStringLiteral("Notes"))) : root;
    QStringList markdown;
    QHash<QString, QStringList> byName;  // nom de fichier en minuscules → chemins absolus
    QDirIterator it(root.absolutePath(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = root.relativeFilePath(path);
        if (hiddenPath(rel))
            continue;
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) {
            if (!scan.studylabExport || rel.startsWith(QLatin1String("Notes/")))
                markdown << path;
        } else if (suffix == QLatin1String("bib")) {
            scan.bibFiles << path;
        }
        else if (imageSuffixes().contains(suffix))
            byName[QFileInfo(path).fileName().toLower()] << path;
    }
    std::sort(markdown.begin(), markdown.end());
    std::sort(scan.bibFiles.begin(), scan.bibFiles.end());
    for (QStringList& paths : byName)
        std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
            // Le chemin le plus court d'abord, comme la résolution d'Obsidian.
            return a.count(u'/') != b.count(u'/') ? a.count(u'/') < b.count(u'/') : a < b;
        });

    const QString prefix = folderPrefix.trimmed();
    static const QRegularExpression wikiImage(QStringLiteral("!\\[\\[([^\\]|#]+)(?:#[^\\]|]*)?(?:\\|([^\\]]*))?\\]\\]"));
    static const QRegularExpression mdImage(QStringLiteral("!\\[([^\\]]*)\\]\\(<?([^)>]+?)>?(?:\\s+\"[^\"]*\")?\\)"));

    for (const QString& path : std::as_const(markdown)) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            scan.warnings << tr("%1 : lecture impossible.").arg(root.relativeFilePath(path));
            continue;
        }
        NoteImport note;
        note.sourcePath = root.relativeFilePath(path);
        note.title = QFileInfo(path).completeBaseName().simplified();
        const QString relDir = QFileInfo(notesRoot.relativeFilePath(path)).path();
        QStringList folder;
        if (!prefix.isEmpty())
            folder << prefix;
        if (relDir != QLatin1String("."))
            folder << relDir;
        note.folder = folder.join(u'/');
        QString body = QString::fromUtf8(f.readAll());
        body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        if (body.startsWith(QChar(0xFEFF)))
            body.remove(0, 1);
        const FrontMatter fm = parseFrontMatter(body);
        note.type = typeFrom(fm.values);
        note.uuid = fm.values.value(QStringLiteral("uuid")).toString().trimmed();
        body = stripImportKeys(body, fm);

        const QDir noteDir = QFileInfo(path).absoluteDir();
        auto resolve = [&](const QString& target) -> QString {
            QString t = QUrl::fromPercentEncoding(target.trimmed().toUtf8());
            if (t.isEmpty() || t.contains(QLatin1String("://")) || t.startsWith(QLatin1String("attachments/")))
                return {};
            if (!imageSuffixes().contains(QFileInfo(t).suffix().toLower()))
                return {};
            for (const QString& candidate : {noteDir.absoluteFilePath(t), root.absoluteFilePath(t)}) {
                if (QFileInfo::exists(candidate) && !hiddenPath(root.relativeFilePath(candidate)))
                    return QFileInfo(candidate).absoluteFilePath();
            }
            const QStringList found = byName.value(QFileInfo(t).fileName().toLower());
            return found.isEmpty() ? QString() : found.first();
        };
        auto isImage = [](const QString& target) {
            return imageSuffixes().contains(QFileInfo(target.trimmed()).suffix().toLower());
        };

        // ![[image.png|300]] → ![image](jeton)
        QString out;
        qsizetype last = 0;
        auto m1 = wikiImage.globalMatch(body);
        while (m1.hasNext()) {
            const auto m = m1.next();
            out += body.mid(last, m.capturedStart() - last);
            last = m.capturedEnd();
            const QString target = m.captured(1).trimmed();
            if (!isImage(target)) {
                out += m.captured(0);  // note intégrée : laissée telle quelle (lien)
                continue;
            }
            const QString file = resolve(target);
            if (file.isEmpty()) {
                ++scan.missingImages;
                scan.warnings << tr("%1 : image introuvable « %2 ».").arg(note.sourcePath, target);
                out += m.captured(0);
                continue;
            }
            const QString token = imageToken(int(note.images.size()));
            note.images.append({token, file});
            out += QStringLiteral("![%1](%2)").arg(QFileInfo(target).completeBaseName(), token);
        }
        out += body.mid(last);
        body = out;

        // ![texte](chemin/relatif.png)
        out.clear();
        last = 0;
        auto m2 = mdImage.globalMatch(body);
        while (m2.hasNext()) {
            const auto m = m2.next();
            out += body.mid(last, m.capturedStart() - last);
            last = m.capturedEnd();
            const QString target = m.captured(2).trimmed();
            if (target.startsWith(u'⁣') || target.contains(QLatin1String("://"))
                || target.startsWith(QLatin1String("attachments/")) || !isImage(target)) {
                out += m.captured(0);
                continue;
            }
            const QString file = resolve(target);
            if (file.isEmpty()) {
                ++scan.missingImages;
                scan.warnings << tr("%1 : image introuvable « %2 ».").arg(note.sourcePath, target);
                out += m.captured(0);
                continue;
            }
            const QString token = imageToken(int(note.images.size()));
            note.images.append({token, file});
            out += QStringLiteral("![%1](%2)").arg(m.captured(1), token);
        }
        out += body.mid(last);
        note.body = out;
        scan.notes << note;
    }
    return scan;
}

}  // namespace studylab
