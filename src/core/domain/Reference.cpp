#include "core/domain/Reference.h"

#include "core/domain/Document.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace studylab {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("Reference", text);
}

QString initials(const QString& given)
{
    QStringList out;
    for (const QString& part : given.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        // « Jean-Paul » → « J.-P. »
        QStringList pieces;
        for (const QString& p : part.split(u'-', Qt::SkipEmptyParts)) {
            const QString clean = QString(p).remove(u'.');
            if (!clean.isEmpty())
                pieces << clean.left(1).toUpper() + u'.';
        }
        if (!pieces.isEmpty())
            out << pieces.join(u'-');
    }
    return out.join(u' ');
}

/// Découpe au niveau 0 des accolades.
QStringList splitTopLevel(const QString& text, const QRegularExpression& separator)
{
    QStringList out;
    int depth = 0;
    qsizetype start = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'{')
            ++depth;
        else if (c == u'}')
            depth = std::max(0, depth - 1);
        else if (depth == 0) {
            const auto m = separator.match(text, i, QRegularExpression::NormalMatch,
                                           QRegularExpression::AnchorAtOffsetMatchOption);
            if (m.hasMatch()) {
                out << text.mid(start, i - start);
                i = m.capturedEnd() - 1;
                start = m.capturedEnd();
            }
        }
    }
    out << text.mid(start);
    return out;
}

bool fullyBraced(const QString& s)
{
    if (s.size() < 2 || !s.startsWith(u'{') || !s.endsWith(u'}'))
        return false;
    int depth = 0;
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s.at(i) == u'{')
            ++depth;
        else if (s.at(i) == u'}' && --depth == 0 && i != s.size() - 1)
            return false;
    }
    return true;
}

QJsonArray namesToJson(const QList<PersonName>& names)
{
    QJsonArray a;
    for (const PersonName& n : names) {
        QJsonObject o;
        if (!n.literal.isEmpty()) {
            o.insert(QStringLiteral("literal"), n.literal);
        } else {
            if (!n.family.isEmpty())
                o.insert(QStringLiteral("family"), n.family);
            if (!n.given.isEmpty())
                o.insert(QStringLiteral("given"), n.given);
        }
        a.append(o);
    }
    return a;
}

QList<PersonName> namesFromJson(const QJsonValue& v)
{
    QList<PersonName> out;
    for (const QJsonValue& item : v.toArray()) {
        const QJsonObject o = item.toObject();
        PersonName n;
        n.family = o.value(QStringLiteral("family")).toString().trimmed();
        n.given = o.value(QStringLiteral("given")).toString().trimmed();
        n.literal = o.value(QStringLiteral("literal")).toString().trimmed();
        const QString particle = o.value(QStringLiteral("non-dropping-particle")).toString().trimmed();
        if (!particle.isEmpty() && !n.family.isEmpty())
            n.family = particle + u' ' + n.family;
        if (n.literal.isEmpty() && n.family.isEmpty() && n.given.isEmpty())
            continue;
        if (n.family.isEmpty() && !n.given.isEmpty() && n.literal.isEmpty())
            std::swap(n.family, n.given);
        out << n;
    }
    return out;
}

QString jsonString(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isDouble())
        return QString::number(v.toInteger());
    return v.toString().trimmed();
}

/// Champs CSL modifiés dans StudyLab (les autres vont dans `extra`).
const QSet<QString>& knownCslKeys()
{
    static const QSet<QString> keys{
        QStringLiteral("id"), QStringLiteral("type"), QStringLiteral("title"), QStringLiteral("author"),
        QStringLiteral("editor"), QStringLiteral("issued"), QStringLiteral("container-title"),
        QStringLiteral("publisher"), QStringLiteral("publisher-place"), QStringLiteral("volume"),
        QStringLiteral("issue"), QStringLiteral("page"), QStringLiteral("edition"), QStringLiteral("DOI"),
        QStringLiteral("ISBN"), QStringLiteral("URL"), QStringLiteral("language"), QStringLiteral("abstract"),
        QStringLiteral("note")};
    return keys;
}

// ───────────────────────── BibTeX ─────────────────────────

struct BibType {
    const char* bib;
    const char* csl;
};

// Ordre : le premier type BibTeX d'un type CSL est celui de l'export.
constexpr BibType kBibTypes[] = {
    {"article", "article-journal"},  {"book", "book"},           {"incollection", "chapter"},
    {"inbook", "chapter"},           {"inproceedings", "paper-conference"},
    {"conference", "paper-conference"}, {"phdthesis", "thesis"}, {"mastersthesis", "thesis"},
    {"thesis", "thesis"},            {"techreport", "report"},   {"report", "report"},
    {"online", "webpage"},           {"electronic", "webpage"},  {"www", "webpage"},
    {"misc", "document"},            {"unpublished", "manuscript"}, {"manual", "book"},
    {"booklet", "book"},             {"proceedings", "book"},    {"collection", "book"},
};

QString cslTypeFromBib(const QString& bib)
{
    for (const BibType& t : kBibTypes)
        if (bib == QLatin1String(t.bib))
            return QString::fromLatin1(t.csl);
    return QStringLiteral("document");
}

QString bibTypeFromCsl(const QString& csl)
{
    if (csl == QLatin1String("article") || csl == QLatin1String("article-magazine")
        || csl == QLatin1String("article-newspaper"))
        return QStringLiteral("article");
    for (const BibType& t : kBibTypes)
        if (csl == QLatin1String(t.csl))
            return QString::fromLatin1(t.bib);
    return QStringLiteral("misc");
}

const QHash<QString, QChar>& accentMarks()
{
    static const QHash<QString, QChar> m{
        {QStringLiteral("'"), QChar(0x0301)}, {QStringLiteral("`"), QChar(0x0300)}, {QStringLiteral("^"), QChar(0x0302)},
        {QStringLiteral("\""), QChar(0x0308)}, {QStringLiteral("~"), QChar(0x0303)}, {QStringLiteral("="), QChar(0x0304)},
        {QStringLiteral("."), QChar(0x0307)}, {QStringLiteral("c"), QChar(0x0327)}, {QStringLiteral("u"), QChar(0x0306)},
        {QStringLiteral("v"), QChar(0x030C)}, {QStringLiteral("H"), QChar(0x030B)}, {QStringLiteral("k"), QChar(0x0328)},
        {QStringLiteral("r"), QChar(0x030A)}};
    return m;
}

const QHash<QString, QString>& latexSymbols()
{
    static const QHash<QString, QString> m{
        {QStringLiteral("ss"), QStringLiteral("ß")}, {QStringLiteral("oe"), QStringLiteral("œ")},
        {QStringLiteral("OE"), QStringLiteral("Œ")}, {QStringLiteral("ae"), QStringLiteral("æ")},
        {QStringLiteral("AE"), QStringLiteral("Æ")}, {QStringLiteral("o"), QStringLiteral("ø")},
        {QStringLiteral("O"), QStringLiteral("Ø")}, {QStringLiteral("aa"), QStringLiteral("å")},
        {QStringLiteral("AA"), QStringLiteral("Å")}, {QStringLiteral("l"), QStringLiteral("ł")},
        {QStringLiteral("L"), QStringLiteral("Ł")}, {QStringLiteral("i"), QStringLiteral("ı")},
        {QStringLiteral("textendash"), QStringLiteral("–")}, {QStringLiteral("textemdash"), QStringLiteral("—")},
        {QStringLiteral("guillemotleft"), QStringLiteral("«")}, {QStringLiteral("guillemotright"), QStringLiteral("»")},
        {QStringLiteral("og"), QStringLiteral("« ")}, {QStringLiteral("fg"), QStringLiteral(" »")},
        {QStringLiteral("textquoteright"), QStringLiteral("’")}, {QStringLiteral("LaTeX"), QStringLiteral("LaTeX")},
        {QStringLiteral("TeX"), QStringLiteral("TeX")}};
    return m;
}

QString escapeBibtex(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'&':
        case u'%':
        case u'$':
        case u'#':
        case u'_':
            out += u'\\';
            out += c;
            break;
        case u'{':
        case u'}':
            break;  // accolades isolées : rendraient le fichier illisible
        default:
            out += c;
        }
    }
    return out;
}

QString bibName(const PersonName& n)
{
    if (!n.literal.isEmpty())
        return u'{' + escapeBibtex(n.literal) + u'}';
    if (n.given.isEmpty())
        return escapeBibtex(n.family);
    return escapeBibtex(n.family) + QStringLiteral(", ") + escapeBibtex(n.given);
}

QString stripStopWords(const QString& title)
{
    static const QSet<QString> stop{
        QStringLiteral("le"),  QStringLiteral("la"),   QStringLiteral("les"),  QStringLiteral("l"),
        QStringLiteral("un"),  QStringLiteral("une"),  QStringLiteral("des"),  QStringLiteral("de"),
        QStringLiteral("du"),  QStringLiteral("d"),    QStringLiteral("et"),   QStringLiteral("en"),
        QStringLiteral("au"),  QStringLiteral("aux"),  QStringLiteral("the"),  QStringLiteral("a"),
        QStringLiteral("an"),  QStringLiteral("of"),   QStringLiteral("on"),   QStringLiteral("and"),
        QStringLiteral("in"),  QStringLiteral("to"),   QStringLiteral("for"),  QStringLiteral("der"),
        QStringLiteral("die"), QStringLiteral("das"),  QStringLiteral("el"),   QStringLiteral("los"),
        QStringLiteral("sur"), QStringLiteral("pour"), QStringLiteral("par")};
    static const QRegularExpression words(QStringLiteral("[^a-z0-9]+"));
    for (const QString& w : foldForSort(title).split(words, Qt::SkipEmptyParts)) {
        if (!stop.contains(w))
            return w;
    }
    return {};
}

QString asciiKeyPart(const QString& text)
{
    QString out;
    for (const QChar c : foldForSort(text)) {
        if ((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9'))
            out += c;
    }
    return out;
}

QString yearLabel(const Reference& r)
{
    return r.year > 0 ? QString::number(r.year) : tr("s. d.");
}

QString joinNames(const QStringList& names)
{
    if (names.size() <= 1)
        return names.value(0);
    return QStringList(names.mid(0, names.size() - 1)).join(QStringLiteral(", ")) + tr(" et ") + names.last();
}

}  // namespace

// ───────────────────────── Noms ─────────────────────────

QString PersonName::sortName() const
{
    if (!literal.isEmpty())
        return literal;
    return given.isEmpty() ? family : family + QStringLiteral(", ") + given;
}

QString PersonName::displayName() const
{
    if (!literal.isEmpty())
        return literal;
    return given.isEmpty() ? family : given + u' ' + family;
}

QString PersonName::initialsName() const
{
    if (!literal.isEmpty())
        return literal;
    const QString i = initials(given);
    return i.isEmpty() ? family : family + QStringLiteral(", ") + i;
}

QString PersonName::shortName() const
{
    return literal.isEmpty() ? family : literal;
}

PersonName parsePersonName(const QString& input)
{
    PersonName n;
    const QString text = input.simplified();
    if (text.isEmpty())
        return n;
    if (fullyBraced(text)) {
        n.literal = latexToUnicode(text);
        return n;
    }
    static const QRegularExpression comma(QStringLiteral(",\\s*"));
    const QStringList parts = splitTopLevel(text, comma);
    if (parts.size() >= 2) {
        // « von Last, First » ou « von Last, Jr, First »
        n.family = latexToUnicode(parts.first()).trimmed();
        n.given = latexToUnicode(parts.last()).trimmed();
        return n;
    }
    static const QRegularExpression space(QStringLiteral("\\s+"));
    const QStringList words = splitTopLevel(text, space);
    if (words.size() == 1) {
        n.family = latexToUnicode(words.first());
        return n;
    }
    // « Prénom de la Nom » : le nom commence à la première particule en minuscules, sinon au dernier mot.
    qsizetype familyStart = words.size() - 1;
    for (qsizetype i = 1; i < words.size() - 1; ++i) {
        const QString w = latexToUnicode(words.at(i));
        if (!w.isEmpty() && w.at(0).isLower()) {
            familyStart = i;
            break;
        }
    }
    n.given = latexToUnicode(QStringList(words.mid(0, familyStart)).join(u' ')).trimmed();
    n.family = latexToUnicode(QStringList(words.mid(familyStart)).join(u' ')).trimmed();
    return n;
}

// ───────────────────────── Types ─────────────────────────

QStringList referenceTypes()
{
    return {QStringLiteral("book"),    QStringLiteral("article-journal"), QStringLiteral("chapter"),
            QStringLiteral("paper-conference"), QStringLiteral("thesis"), QStringLiteral("report"),
            QStringLiteral("webpage"), QStringLiteral("document")};
}

QString referenceTypeLabel(const QString& t)
{
    if (t == QLatin1String("book"))
        return tr("Livre");
    if (t == QLatin1String("article-journal") || t == QLatin1String("article") || t == QLatin1String("article-magazine")
        || t == QLatin1String("article-newspaper"))
        return tr("Article");
    if (t == QLatin1String("chapter"))
        return tr("Chapitre d'ouvrage");
    if (t == QLatin1String("paper-conference"))
        return tr("Communication");
    if (t == QLatin1String("thesis"))
        return tr("Thèse ou mémoire");
    if (t == QLatin1String("report"))
        return tr("Rapport");
    if (t == QLatin1String("webpage") || t == QLatin1String("post-weblog"))
        return tr("Page web");
    return tr("Autre document");
}

// ───────────────────────── CSL-JSON ─────────────────────────

QJsonObject toCslJson(const Reference& r)
{
    QJsonObject o = r.extra;
    o.insert(QStringLiteral("id"), r.citekey);
    o.insert(QStringLiteral("type"), r.type.isEmpty() ? QStringLiteral("document") : r.type);
    auto put = [&o](const QString& key, const QString& value) {
        if (value.trimmed().isEmpty())
            o.remove(key);
        else
            o.insert(key, value.trimmed());
    };
    put(QStringLiteral("title"), r.title);
    if (r.authors.isEmpty())
        o.remove(QStringLiteral("author"));
    else
        o.insert(QStringLiteral("author"), namesToJson(r.authors));
    if (r.editors.isEmpty())
        o.remove(QStringLiteral("editor"));
    else
        o.insert(QStringLiteral("editor"), namesToJson(r.editors));
    if (r.year > 0) {
        QJsonArray parts{r.year};
        if (r.month >= 1 && r.month <= 12)
            parts.append(r.month);
        // Tableau imbriqué construit explicitement : selon la version de Qt, QJsonArray{parts} copie au lieu
        // d'imbriquer.
        QJsonArray dateParts;
        dateParts.append(parts);
        o.insert(QStringLiteral("issued"), QJsonObject{{QStringLiteral("date-parts"), dateParts}});
    } else {
        o.remove(QStringLiteral("issued"));
    }
    put(QStringLiteral("container-title"), r.containerTitle);
    put(QStringLiteral("publisher"), r.publisher);
    put(QStringLiteral("publisher-place"), r.place);
    put(QStringLiteral("volume"), r.volume);
    put(QStringLiteral("issue"), r.issue);
    put(QStringLiteral("page"), r.pages);
    put(QStringLiteral("edition"), r.edition);
    put(QStringLiteral("DOI"), r.doi);
    put(QStringLiteral("ISBN"), r.isbn);
    put(QStringLiteral("URL"), r.url);
    put(QStringLiteral("language"), r.language);
    put(QStringLiteral("abstract"), r.abstract);
    put(QStringLiteral("note"), r.note);
    return o;
}

Reference fromCslJson(const QJsonObject& o)
{
    Reference r;
    r.citekey = jsonString(o, "id");
    r.type = jsonString(o, "type");
    if (r.type.isEmpty())
        r.type = QStringLiteral("document");
    r.title = jsonString(o, "title");
    r.authors = namesFromJson(o.value(QStringLiteral("author")));
    r.editors = namesFromJson(o.value(QStringLiteral("editor")));
    const QJsonObject issued = o.value(QStringLiteral("issued")).toObject();
    const QJsonArray first = issued.value(QStringLiteral("date-parts")).toArray().at(0).toArray();
    if (!first.isEmpty()) {
        r.year = first.at(0).isString() ? first.at(0).toString().toInt() : first.at(0).toInt();
        r.month = first.size() > 1 ? (first.at(1).isString() ? first.at(1).toString().toInt() : first.at(1).toInt()) : 0;
    } else {
        static const QRegularExpression year(QStringLiteral("\\b(\\d{4})\\b"));
        const QString raw = issued.value(QStringLiteral("raw")).toString() + u' '
                            + issued.value(QStringLiteral("literal")).toString();
        const auto m = year.match(raw);
        if (m.hasMatch())
            r.year = m.captured(1).toInt();
    }
    if (r.month < 0 || r.month > 12)
        r.month = 0;
    r.containerTitle = jsonString(o, "container-title");
    r.publisher = jsonString(o, "publisher");
    r.place = jsonString(o, "publisher-place");
    r.volume = jsonString(o, "volume");
    r.issue = jsonString(o, "issue");
    r.pages = jsonString(o, "page");
    r.edition = jsonString(o, "edition");
    r.doi = jsonString(o, "DOI");
    r.isbn = jsonString(o, "ISBN");
    r.url = jsonString(o, "URL");
    r.language = jsonString(o, "language");
    r.abstract = jsonString(o, "abstract");
    r.note = jsonString(o, "note");
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (!knownCslKeys().contains(it.key()))
            r.extra.insert(it.key(), it.value());
    }
    return r;
}

Result<QList<Reference>> parseCslJson(const QByteArray& json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError)
        return Error::make(Error::Code::InvalidArgument,
                           tr("CSL-JSON illisible : %1 (position %2).").arg(err.errorString()).arg(err.offset));
    QList<Reference> out;
    if (doc.isObject()) {
        out << fromCslJson(doc.object());
    } else {
        for (const QJsonValue& v : doc.array()) {
            if (v.isObject())
                out << fromCslJson(v.toObject());
        }
    }
    return out;
}

QByteArray writeCslJson(const QList<Reference>& refs)
{
    QJsonArray a;
    for (const Reference& r : refs)
        a.append(toCslJson(r));
    return QJsonDocument(a).toJson(QJsonDocument::Indented);
}

// ───────────────────────── LaTeX ─────────────────────────

QString latexToUnicode(const QString& latex)
{
    QString s = latex;
    // Commandes d'accent : \'e, \'{e}, {\'e}, \c{c}, \c c
    static const QRegularExpression accent(
        QStringLiteral("\\\\([`'^\"~=.]|[cuvHkr](?=[\\s{]))\\s*(?:\\{\\s*(\\\\?[A-Za-z])\\s*\\}|(\\\\?[A-Za-z]))"));
    QString out;
    qsizetype last = 0;
    auto it = accent.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        QString letter = m.captured(2).isEmpty() ? m.captured(3) : m.captured(2);
        if (letter == QLatin1String("\\i"))
            letter = QStringLiteral("i");
        else if (letter == QLatin1String("\\j"))
            letter = QStringLiteral("j");
        letter.remove(u'\\');
        out += (letter + accentMarks().value(m.captured(1))).normalized(QString::NormalizationForm_C);
        last = m.capturedEnd();
    }
    out += s.mid(last);
    s = out;
    // Symboles : \ss, \oe, \o{}…
    static const QRegularExpression symbol(QStringLiteral("\\\\([A-Za-z]+)(?:\\{\\}|\\s+(?=[A-Za-z])|\\b)"));
    out.clear();
    last = 0;
    it = symbol.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        const QString name = m.captured(1);
        out += latexSymbols().contains(name) ? latexSymbols().value(name) : QString();
        last = m.capturedEnd();
    }
    out += s.mid(last);
    s = out;
    s.replace(QStringLiteral("---"), QStringLiteral("—"));
    s.replace(QStringLiteral("--"), QStringLiteral("–"));
    s.replace(QStringLiteral("``"), QStringLiteral("“"));
    s.replace(QStringLiteral("''"), QStringLiteral("”"));
    static const QRegularExpression escaped(QStringLiteral("\\\\([&%$#_{}])"));
    s.replace(escaped, QStringLiteral("\\1"));
    s.replace(QStringLiteral("\\{"), QStringLiteral("{"));
    // Espace insécable LaTeX (sauf échappé, déjà traité)
    s.replace(u'~', QChar(0x00A0));
    s.remove(u'{');
    s.remove(u'}');
    // simplified() avalerait l'espace insécable.
    static const QRegularExpression spaces(QStringLiteral("[ \\t\\r\\n]+"));
    return s.replace(spaces, QStringLiteral(" ")).trimmed();
}

// ───────────────────────── BibTeX ─────────────────────────

BibtexParse parseBibtex(const QString& text)
{
    BibtexParse result;
    QHash<QString, QString> strings{
        {QStringLiteral("jan"), QStringLiteral("1")},  {QStringLiteral("feb"), QStringLiteral("2")},
        {QStringLiteral("mar"), QStringLiteral("3")},  {QStringLiteral("apr"), QStringLiteral("4")},
        {QStringLiteral("may"), QStringLiteral("5")},  {QStringLiteral("jun"), QStringLiteral("6")},
        {QStringLiteral("jul"), QStringLiteral("7")},  {QStringLiteral("aug"), QStringLiteral("8")},
        {QStringLiteral("sep"), QStringLiteral("9")},  {QStringLiteral("oct"), QStringLiteral("10")},
        {QStringLiteral("nov"), QStringLiteral("11")}, {QStringLiteral("dec"), QStringLiteral("12")}};
    const qsizetype n = text.size();
    qsizetype i = 0;
    auto lineAt = [&text](qsizetype pos) { return int(text.left(pos).count(u'\n')) + 1; };
    auto skipSpace = [&] {
        while (i < n && text.at(i).isSpace())
            ++i;
    };
    auto readIdent = [&] {
        const qsizetype start = i;
        while (i < n && (text.at(i).isLetterOrNumber() || QStringLiteral("_-:.+/'").contains(text.at(i))))
            ++i;
        return text.mid(start, i - start);
    };
    // Valeur entre accolades (imbriquées) ou guillemets ; i est sur le délimiteur ouvrant.
    auto readDelimited = [&](bool& ok) {
        const QChar open = text.at(i);
        const QChar close = open == u'{' ? u'}' : u'"';
        int depth = 0;
        const qsizetype start = ++i;
        while (i < n) {
            const QChar c = text.at(i);
            if (c == u'\\') {
                i += 2;
                continue;
            }
            if (c == u'{')
                ++depth;
            else if (c == u'}' && (depth > 0 || close != u'}'))
                --depth;
            else if (c == close && depth == 0)
                break;
            ++i;
        }
        ok = i < n;
        const QString v = text.mid(start, i - start);
        ++i;
        return v;
    };
    auto readValue = [&](bool& ok) {
        QString value;
        ok = true;
        while (true) {
            skipSpace();
            if (i >= n) {
                ok = false;
                return value;
            }
            const QChar c = text.at(i);
            if (c == u'{' || c == u'"') {
                value += readDelimited(ok);
                if (!ok)
                    return value;
            } else if (c.isDigit()) {
                const qsizetype start = i;
                while (i < n && text.at(i).isDigit())
                    ++i;
                value += text.mid(start, i - start);
            } else {
                const QString name = readIdent().toLower();
                if (name.isEmpty()) {
                    ok = false;
                    return value;
                }
                value += strings.value(name);
            }
            skipSpace();
            if (i < n && text.at(i) == u'#') {
                ++i;
                continue;
            }
            return value;
        }
    };

    while (true) {
        const qsizetype at = text.indexOf(u'@', i);
        if (at < 0)
            break;
        i = at + 1;
        const int line = lineAt(at);
        const QString kind = readIdent().toLower();
        skipSpace();
        if (i >= n || (text.at(i) != u'{' && text.at(i) != u'('))
            continue;  // « @ » isolé (adresse dans un commentaire…)
        const QChar close = text.at(i) == u'{' ? u'}' : u')';
        if (kind == QLatin1String("comment") || kind == QLatin1String("preamble")) {
            bool ok = false;
            if (text.at(i) == u'{')
                readDelimited(ok);
            continue;
        }
        ++i;
        if (kind == QLatin1String("string")) {
            skipSpace();
            const QString name = readIdent().toLower();
            skipSpace();
            if (i < n && text.at(i) == u'=') {
                ++i;
                bool ok = false;
                const QString v = readValue(ok);
                if (ok)
                    strings.insert(name, v);
            }
            skipSpace();
            if (i < n && text.at(i) == close)
                ++i;
            continue;
        }
        skipSpace();
        const qsizetype keyStart = i;
        while (i < n && text.at(i) != u',' && text.at(i) != close && !text.at(i).isSpace())
            ++i;
        const QString key = text.mid(keyStart, i - keyStart).trimmed();
        skipSpace();
        if (key.isEmpty() || i >= n || text.at(i) != u',') {
            result.warnings << tr("Ligne %1 : entrée @%2 sans clé, ignorée.").arg(line).arg(kind);
            continue;
        }
        ++i;
        QHash<QString, QString> fields;
        bool broken = false;
        while (true) {
            skipSpace();
            if (i >= n) {
                broken = true;
                break;
            }
            if (text.at(i) == close) {
                ++i;
                break;
            }
            const QString field = readIdent().toLower();
            skipSpace();
            if (field.isEmpty() || i >= n || text.at(i) != u'=') {
                broken = true;
                break;
            }
            ++i;
            bool ok = false;
            const QString value = readValue(ok);
            if (!ok) {
                broken = true;
                break;
            }
            fields.insert(field, value);
            skipSpace();
            if (i < n && text.at(i) == u',')
                ++i;
        }
        if (broken) {
            result.warnings << tr("Ligne %1 : entrée « %2 » mal formée, ignorée.").arg(line).arg(key);
            // Reprendre à la prochaine entrée.
            continue;
        }

        Reference r;
        r.citekey = key;
        r.type = cslTypeFromBib(kind);
        auto take = [&fields](const char* name) { return latexToUnicode(fields.take(QLatin1String(name))); };
        auto names = [&fields](const char* name) {
            QList<PersonName> out;
            static const QRegularExpression andSep(QStringLiteral("\\s+and\\s+"),
                                                   QRegularExpression::CaseInsensitiveOption);
            for (const QString& part : splitTopLevel(fields.take(QLatin1String(name)).simplified(), andSep)) {
                if (part.trimmed() == QLatin1String("others"))
                    continue;
                const PersonName p = parsePersonName(part);
                if (!p.isEmpty())
                    out << p;
            }
            return out;
        };
        r.title = take("title");
        r.authors = names("author");
        r.editors = names("editor");
        const QString date = fields.take(QStringLiteral("date"));
        const QString year = fields.take(QStringLiteral("year"));
        static const QRegularExpression yearRe(QStringLiteral("(\\d{4})(?:-(\\d{1,2}))?"));
        if (const auto m = yearRe.match(year.isEmpty() ? date : year); m.hasMatch()) {
            r.year = m.captured(1).toInt();
            r.month = m.captured(2).toInt();
        }
        const QString month = latexToUnicode(fields.take(QStringLiteral("month")));
        if (r.month == 0 && month.toInt() >= 1 && month.toInt() <= 12)
            r.month = month.toInt();
        r.containerTitle = take("journal");
        if (r.containerTitle.isEmpty())
            r.containerTitle = take("journaltitle");
        if (r.containerTitle.isEmpty())
            r.containerTitle = take("booktitle");
        r.publisher = take("publisher");
        for (const char* org : {"school", "institution", "organization"}) {
            const QString v = take(org);
            if (r.publisher.isEmpty())
                r.publisher = v;
        }
        r.place = take("address");
        if (r.place.isEmpty())
            r.place = take("location");
        r.volume = take("volume");
        r.issue = take("number");
        if (r.issue.isEmpty())
            r.issue = take("issue");
        r.pages = take("pages");
        r.edition = take("edition");
        r.doi = take("doi");
        r.isbn = take("isbn");
        r.url = fields.take(QStringLiteral("url")).trimmed();
        r.language = take("language");
        if (r.language.isEmpty())
            r.language = take("langid");
        r.abstract = take("abstract");
        r.note = take("note");
        if (kind == QLatin1String("phdthesis"))
            r.extra.insert(QStringLiteral("genre"), tr("Thèse de doctorat"));
        else if (kind == QLatin1String("mastersthesis"))
            r.extra.insert(QStringLiteral("genre"), tr("Mémoire de master"));
        // Le reste (keywords, file, issn…) est gardé pour l'export CSL-JSON.
        for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
            if (it.key() == QLatin1String("keywords"))
                r.extra.insert(QStringLiteral("keyword"), latexToUnicode(it.value()));
            else if (it.key() == QLatin1String("issn"))
                r.extra.insert(QStringLiteral("ISSN"), latexToUnicode(it.value()));
            else if (it.key() == QLatin1String("series"))
                r.extra.insert(QStringLiteral("collection-title"), latexToUnicode(it.value()));
        }
        result.references << r;
    }
    return result;
}

QString writeBibtex(const QList<Reference>& refs)
{
    QString out;
    for (const Reference& r : refs) {
        QString kind = bibTypeFromCsl(r.type);
        if (r.type == QLatin1String("thesis"))
            kind = r.extra.value(QStringLiteral("genre")).toString().contains(QStringLiteral("master"), Qt::CaseInsensitive)
                       ? QStringLiteral("mastersthesis")
                       : QStringLiteral("phdthesis");
        QList<QPair<QString, QString>> f;
        auto add = [&f](const char* name, const QString& value) {
            if (!value.trimmed().isEmpty())
                f.append({QString::fromLatin1(name), value.trimmed()});
        };
        auto names = [](const QList<PersonName>& list) {
            QStringList parts;
            for (const PersonName& n : list)
                parts << bibName(n);
            return parts.join(QStringLiteral(" and "));
        };
        add("author", names(r.authors));
        add("editor", names(r.editors));
        add("title", escapeBibtex(r.title));
        if (kind == QLatin1String("article"))
            add("journal", escapeBibtex(r.containerTitle));
        else if (kind == QLatin1String("online") || kind == QLatin1String("misc"))
            add("howpublished", escapeBibtex(r.containerTitle));
        else
            add("booktitle", escapeBibtex(r.containerTitle));
        if (kind == QLatin1String("phdthesis") || kind == QLatin1String("mastersthesis"))
            add("school", escapeBibtex(r.publisher));
        else if (kind == QLatin1String("techreport"))
            add("institution", escapeBibtex(r.publisher));
        else
            add("publisher", escapeBibtex(r.publisher));
        add("address", escapeBibtex(r.place));
        if (r.year > 0)
            add("year", QString::number(r.year));
        if (r.month > 0)
            add("month", QString::number(r.month));
        add("edition", escapeBibtex(r.edition));
        add("volume", escapeBibtex(r.volume));
        add("number", escapeBibtex(r.issue));
        add("pages", escapeBibtex(QString(r.pages).replace(QChar(0x2013), QStringLiteral("--"))));
        add("doi", escapeBibtex(r.doi));
        add("isbn", escapeBibtex(r.isbn));
        add("url", QString(r.url).remove(u'{').remove(u'}'));
        add("language", escapeBibtex(r.language));
        add("abstract", escapeBibtex(r.abstract));
        add("note", escapeBibtex(r.note));
        out += QStringLiteral("@%1{%2").arg(kind, r.citekey);
        for (const auto& [name, value] : f)
            out += QStringLiteral(",\n  %1 = {%2}").arg(name, value);
        out += QStringLiteral("\n}\n\n");
    }
    return out;
}

// ───────────────────────── Clés et mise en forme ─────────────────────────

bool isValidCitekey(const QString& key)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_:.\\-]*$"));
    return valid.match(key).hasMatch();
}

QString makeCitekey(const Reference& ref, const QSet<QString>& taken)
{
    QString base;
    if (!ref.authors.isEmpty())
        base = asciiKeyPart(ref.authors.first().shortName());
    else if (!ref.editors.isEmpty())
        base = asciiKeyPart(ref.editors.first().shortName());
    if (ref.year > 0)
        base += QString::number(ref.year);
    base += stripStopWords(ref.title);
    if (base.isEmpty())
        base = QStringLiteral("ref");
    base = base.left(40);
    if (!taken.contains(base))
        return base;
    for (char c = 'a'; c <= 'z'; ++c) {
        const QString key = base + QLatin1Char(c);
        if (!taken.contains(key))
            return key;
    }
    for (int k = 2;; ++k) {
        const QString key = base + QString::number(k);
        if (!taken.contains(key))
            return key;
    }
}

QString formatCitation(const Reference& r)
{
    const QList<PersonName>& people = r.authors.isEmpty() ? r.editors : r.authors;
    QString who;
    if (people.size() == 1)
        who = people.first().shortName();
    else if (people.size() == 2)
        who = people.at(0).shortName() + tr(" et ") + people.at(1).shortName();
    else if (people.size() > 2)
        who = people.first().shortName() + tr(" et al.");
    else {
        who = r.title.section(u' ', 0, 3);
        if (who.size() < r.title.size())
            who += QStringLiteral("…");
    }
    if (who.isEmpty())
        who = r.citekey;
    return who + QStringLiteral(", ") + yearLabel(r);
}

QString formatBibliography(const Reference& r, bool html)
{
    auto esc = [html](const QString& s) { return html ? s.toHtmlEscaped() : s; };
    auto italic = [html, &esc](const QString& s) { return html ? QStringLiteral("<i>%1</i>").arg(esc(s)) : s; };
    QStringList names;
    for (const PersonName& n : r.authors)
        names << n.initialsName();
    QString out;
    const bool editorsLead = r.authors.isEmpty() && !r.editors.isEmpty();
    if (editorsLead) {
        for (const PersonName& n : r.editors)
            names << n.initialsName();
    }
    const auto endSentence = [](QString& s) {
        if (!s.isEmpty() && !s.endsWith(u'.') && !s.endsWith(u'?') && !s.endsWith(u'!'))
            s += u'.';
    };
    if (!names.isEmpty()) {
        out = esc(joinNames(names));
        if (editorsLead)
            out += esc(tr(" (dir.)"));
        out += u' ';
    }
    out += QStringLiteral("(%1). ").arg(esc(yearLabel(r)));
    const bool partOf = !r.containerTitle.isEmpty()
                        && (r.type == QLatin1String("article-journal") || r.type == QLatin1String("chapter")
                            || r.type == QLatin1String("paper-conference") || r.type.startsWith(QLatin1String("article"))
                            || r.type == QLatin1String("webpage"));
    if (!r.title.isEmpty()) {
        QString t = r.title;
        endSentence(t);
        out += (partOf ? esc(t) : italic(r.title) + (t.size() > r.title.size() ? QStringLiteral(".") : QString())) + u' ';
    }
    if (partOf) {
        if (r.type == QLatin1String("article-journal") || r.type.startsWith(QLatin1String("article"))) {
            QString s = italic(r.containerTitle);
            if (!r.volume.isEmpty())
                s += QStringLiteral(", ") + (html ? QStringLiteral("<i>%1</i>").arg(esc(r.volume)) : r.volume);
            if (!r.issue.isEmpty())
                s += QStringLiteral("(%1)").arg(esc(r.issue));
            if (!r.pages.isEmpty())
                s += QStringLiteral(", ") + esc(r.pages);
            out += s + QStringLiteral(". ");
        } else {
            QString s = tr("Dans ");
            if (!r.editors.isEmpty() && !editorsLead) {
                QStringList ed;
                for (const PersonName& n : r.editors)
                    ed << n.initialsName();
                s += esc(joinNames(ed)) + esc(tr(" (dir.), "));
            }
            s += italic(r.containerTitle);
            if (!r.pages.isEmpty())
                s += QStringLiteral(" (p. %1)").arg(esc(r.pages));
            out += s + QStringLiteral(". ");
        }
    } else if (!r.edition.isEmpty()) {
        out += esc(tr("%1ᵉ éd. ").arg(r.edition));
    }
    if (r.type == QLatin1String("thesis")) {
        const QString genre = r.extra.value(QStringLiteral("genre")).toString();
        if (!genre.isEmpty())
            out += esc(genre) + QStringLiteral(", ");
    }
    if (!r.publisher.isEmpty() && !(r.type.startsWith(QLatin1String("article")))) {
        QString p = r.publisher;
        if (!r.place.isEmpty())
            p = r.place + QStringLiteral(" : ") + p;
        endSentence(p);
        out += esc(p) + u' ';
    }
    if (!r.doi.isEmpty())
        out += esc(QStringLiteral("https://doi.org/") + r.doi);
    else if (!r.url.isEmpty())
        out += esc(r.url);
    return out.trimmed();
}

QStringList citedKeys(const QString& markdown)
{
    static const QRegularExpression cite(QStringLiteral("\\[\\[@([^\\]|#\\s]+)[^\\]]*\\]\\]"));
    QStringList keys;
    auto it = cite.globalMatch(markdown);
    while (it.hasNext()) {
        const QString k = it.next().captured(1);
        if (!keys.contains(k))
            keys << k;
    }
    return keys;
}

}  // namespace studylab
