#include "core/domain/Note.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>

namespace studylab {

namespace {

struct TypeInfo {
    NoteType type;
    const char* key;
    const char* label;
    const char* icon;
};
const TypeInfo kTypes[] = {
    {NoteType::Personal, "personal", QT_TRANSLATE_NOOP("Note", "Note personnelle"), "note"},
    {NoteType::Reading, "reading", QT_TRANSLATE_NOOP("Note", "Note de lecture"), "book-open"},
    {NoteType::Course, "course", QT_TRANSLATE_NOOP("Note", "Fiche de cours"), "layers"},
    {NoteType::Concept, "concept", QT_TRANSLATE_NOOP("Note", "Concept"), "lightbulb"},
    {NoteType::Formula, "formula", QT_TRANSLATE_NOOP("Note", "Formules"), "sigma"},
    {NoteType::Revision, "revision", QT_TRANSLATE_NOOP("Note", "Fiche de révision"), "cards"},
    {NoteType::Research, "research", QT_TRANSLATE_NOOP("Note", "Recherche"), "search"},
    {NoteType::Report, "report", QT_TRANSLATE_NOOP("Note", "Compte rendu"), "list"},
};

QString tr(const char* text) { return QCoreApplication::translate("Note", text); }

/// Valeur scalaire YAML : guillemets retirés, booléens reconnus, le reste en texte.
QJsonValue scalar(QString v)
{
    v = v.trimmed();
    if (v.size() >= 2 && ((v.startsWith(u'"') && v.endsWith(u'"')) || (v.startsWith(u'\'') && v.endsWith(u'\''))))
        return v.mid(1, v.size() - 2);
    if (v == QLatin1String("true"))
        return true;
    if (v == QLatin1String("false"))
        return false;
    return v;
}

QJsonArray flowList(const QString& inner)
{
    QJsonArray out;
    for (const QString& part : inner.split(u',', Qt::SkipEmptyParts)) {
        const QJsonValue v = scalar(part);
        if (!(v.isString() && v.toString().isEmpty()))
            out.append(v);
    }
    return out;
}

/// Liste de chaînes d'une clé du front matter (liste YAML ou texte « a, b » / « a b »).
QStringList stringList(const QJsonObject& fm, const QStringList& keys)
{
    QStringList out;
    for (const QString& key : keys) {
        const QJsonValue v = fm.value(key);
        if (v.isArray()) {
            for (const QJsonValue& item : v.toArray())
                if (item.isString())
                    out << item.toString();
        } else if (v.isString()) {
            static const QRegularExpression sep(QStringLiteral("[,\\s]+"));
            out << v.toString().split(sep, Qt::SkipEmptyParts);
        }
    }
    return out;
}

bool isFence(const QString& line)
{
    const QString t = line.trimmed();
    return t.startsWith(QLatin1String("```")) || t.startsWith(QLatin1String("~~~"));
}

}  // namespace

QString noteTypeKey(NoteType t)
{
    for (const auto& i : kTypes)
        if (i.type == t)
            return QString::fromLatin1(i.key);
    return QStringLiteral("personal");
}

std::optional<NoteType> noteTypeFromKey(const QString& key)
{
    for (const auto& i : kTypes)
        if (key == QLatin1String(i.key))
            return i.type;
    return std::nullopt;
}

QString noteTypeLabel(NoteType t)
{
    for (const auto& i : kTypes)
        if (i.type == t)
            return tr(i.label);
    return {};
}

QString noteTypeIcon(NoteType t)
{
    for (const auto& i : kTypes)
        if (i.type == t)
            return QString::fromLatin1(i.icon);
    return QStringLiteral("note");
}

QList<NoteType> allNoteTypes()
{
    QList<NoteType> out;
    for (const auto& i : kTypes)
        out << i.type;
    return out;
}

QString noteTemplate(NoteType t, const QString& title)
{
    Q_UNUSED(title)
    const QString tags = QStringLiteral("---\ntags: []\n---\n\n");
    switch (t) {
    case NoteType::Personal:
        return {};
    case NoteType::Reading:
        return QStringLiteral("---\nsource: \ntags: [lecture]\n---\n\n") + tr("## Idées principales\n\n\n## Citations\n\n\n## Questions\n\n");
    case NoteType::Course:
        return QStringLiteral("---\nmatière: \ntags: [cours]\n---\n\n") + tr("## Définitions\n\n\n## À retenir\n\n");
    case NoteType::Concept:
        return tags + tr("## Définition\n\n\n## Exemples\n\n\n## Voir aussi\n\n");
    case NoteType::Formula:
        return tags + tr("## Formule\n\n$$\n\n$$\n\n## Variables\n\n\n## Conditions d'application\n\n");
    case NoteType::Revision:
        return QStringLiteral("---\ntags: [révision]\n---\n\n") + tr("## Points clés\n\n\n## Questions\n\n");
    case NoteType::Research:
        return tags + tr("## Question\n\n\n## Sources\n\n\n## Pistes\n\n");
    case NoteType::Report:
        return tags + tr("## Contexte\n\n\n## Résultats\n\n\n## Conclusion\n\n");
    }
    return {};
}

FrontMatter parseFrontMatter(const QString& markdown)
{
    FrontMatter fm;
    if (!markdown.startsWith(QLatin1String("---")))
        return fm;
    const QStringList lines = markdown.split(u'\n');
    if (lines.first().trimmed() != QLatin1String("---"))
        return fm;
    int end = -1;
    for (int i = 1; i < lines.size(); ++i) {
        const QString t = lines.at(i).trimmed();
        if (t == QLatin1String("---") || t == QLatin1String("...")) {
            end = i;
            break;
        }
    }
    if (end < 0)
        return fm;

    static const QRegularExpression keyLine(QStringLiteral("^([^\\s:#-][^:]*?)\\s*:(?:\\s+(.*))?\\s*$"));
    static const QRegularExpression itemLine(QStringLiteral("^\\s*-\\s+(.*)$"));
    QString currentKey;
    QJsonArray currentList;
    auto flush = [&] {
        if (!currentKey.isEmpty() && !currentList.isEmpty())
            fm.values.insert(currentKey, currentList);
        currentList = {};
    };
    for (int i = 1; i < end; ++i) {
        const QString line = lines.at(i);
        if (line.trimmed().isEmpty() || line.trimmed().startsWith(u'#'))
            continue;
        if (const auto item = itemLine.match(line); item.hasMatch() && !currentKey.isEmpty()) {
            currentList.append(scalar(item.captured(1)));
            continue;
        }
        const auto m = keyLine.match(line);
        if (!m.hasMatch())
            continue;
        flush();
        currentKey = m.captured(1).trimmed();
        const QString value = m.captured(2).trimmed();
        if (value.startsWith(u'[') && value.endsWith(u']'))
            fm.values.insert(currentKey, flowList(value.mid(1, value.size() - 2)));
        else
            fm.values.insert(currentKey, scalar(value));
    }
    flush();

    int pos = 0;
    for (int i = 0; i <= end; ++i)
        pos += int(lines.at(i).size()) + 1;
    fm.bodyStart = std::min(pos, int(markdown.size()));
    fm.lineCount = end + 1;
    fm.present = true;
    return fm;
}

QString stripFrontMatter(const QString& markdown)
{
    const FrontMatter fm = parseFrontMatter(markdown);
    return fm.present ? markdown.mid(fm.bodyStart) : markdown;
}

QStringList noteTags(const QString& markdown)
{
    const FrontMatter fm = parseFrontMatter(markdown);
    QStringList out;
    auto add = [&out](QString tag) {
        while (tag.startsWith(u'#'))
            tag.remove(0, 1);
        tag = tag.trimmed();
        if (!tag.isEmpty() && !out.contains(tag, Qt::CaseInsensitive))
            out << tag;
    };
    for (const QString& t : stringList(fm.values, {QStringLiteral("tags"), QStringLiteral("tag")}))
        add(t);

    // #tags du texte, hors blocs de code, code en ligne et titres.
    static const QRegularExpression inlineCode(QStringLiteral("`[^`\\n]*`"));
    static const QRegularExpression tagRe(QStringLiteral("(?:^|[\\s(])#([\\p{L}\\p{N}_/\\-]*[\\p{L}_][\\p{L}\\p{N}_/\\-]*)"),
                                          QRegularExpression::UseUnicodePropertiesOption);
    bool inCode = false;
    const QStringList lines = markdown.mid(fm.bodyStart).split(u'\n');
    for (QString line : lines) {
        if (isFence(line)) {
            inCode = !inCode;
            continue;
        }
        if (inCode)
            continue;
        line.remove(inlineCode);
        auto it = tagRe.globalMatch(line);
        while (it.hasNext())
            add(it.next().captured(1));
    }
    return out;
}

QStringList noteAliases(const QString& markdown)
{
    const QJsonObject fm = parseFrontMatter(markdown).values;
    QStringList candidates;
    for (const QString& key : {QStringLiteral("aliases"), QStringLiteral("alias")}) {
        const QJsonValue v = fm.value(key);
        if (v.isArray()) {
            for (const QJsonValue& item : v.toArray())
                candidates << item.toString();
        } else if (v.isString()) {
            candidates << v.toString().split(u',');  // « a, b » : un alias peut contenir des espaces
        }
    }
    QStringList out;
    for (const QString& a : std::as_const(candidates)) {
        const QString t = a.trimmed();
        if (!t.isEmpty() && !out.contains(t, Qt::CaseInsensitive))
            out << t;
    }
    return out;
}

QList<OutlineEntry> noteOutline(const QString& markdown)
{
    static const QRegularExpression heading(QStringLiteral("^(#{1,6})\\s+(.+?)\\s*#*\\s*$"));
    const FrontMatter fm = parseFrontMatter(markdown);
    const QStringList lines = markdown.split(u'\n');
    QList<OutlineEntry> out;
    bool inCode = false;
    for (int i = fm.present ? fm.lineCount : 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        if (isFence(line)) {
            inCode = !inCode;
            continue;
        }
        if (inCode)
            continue;
        if (const auto m = heading.match(line); m.hasMatch())
            out.append({int(m.captured(1).size()), m.captured(2), i});
    }
    return out;
}

QString noteSlug(const QString& title)
{
    static const QRegularExpression forbidden(QStringLiteral("[\\\\/:*?\"<>|#^\\[\\]\\x00-\\x1f]"));
    QString s = title;
    s.replace(forbidden, QStringLiteral(" "));
    s = s.simplified();
    while (s.endsWith(u'.') || s.endsWith(u' '))
        s.chop(1);
    while (s.startsWith(u'.'))
        s.remove(0, 1);
    if (s.size() > 120)
        s = s.left(120).trimmed();
    if (s.isEmpty())
        s = tr("Sans titre");
    static const QRegularExpression reserved(QStringLiteral("^(con|prn|aux|nul|com\\d|lpt\\d)$"),
                                             QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(s).hasMatch())
        s += QStringLiteral(" (note)");
    return s;
}

QString wikiLinkKey(const QString& target)
{
    QString t = target;
    const int bar = int(t.indexOf(u'|'));
    if (bar >= 0)
        t = t.left(bar);
    for (const QChar c : {QChar(u'#'), QChar(u'^')}) {
        const int i = int(t.indexOf(c));
        if (i >= 0)
            t = t.left(i);
    }
    t = t.trimmed();
    if (t.endsWith(QLatin1String(".md"), Qt::CaseInsensitive))
        t.chop(3);
    return t.normalized(QString::NormalizationForm_C).toCaseFolded().simplified();
}

QString wikiLinkSnippet(const QString& markdown, const QStringList& keys, int radius)
{
    static const QRegularExpression link(QStringLiteral("\\[\\[([^\\[\\]\\n]+)\\]\\]"));
    const QString text = stripFrontMatter(markdown);
    auto it = link.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        if (!keys.contains(wikiLinkKey(m.captured(1))))
            continue;
        const int start = std::max(0, int(m.capturedStart()) - radius);
        const int end = std::min(int(text.size()), int(m.capturedEnd()) + radius);
        QString s = text.mid(start, end - start).simplified();
        if (start > 0)
            s.prepend(QStringLiteral("…"));
        if (end < text.size())
            s.append(QStringLiteral("…"));
        return s;
    }
    return {};
}

}  // namespace studylab
