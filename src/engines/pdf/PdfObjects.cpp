#include "engines/pdf/PdfObjects.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QtEndian>

#include <cmath>
#include <cstdlib>

namespace studylab::pdf {

namespace {

bool isWhite(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0'; }
bool isDelim(char c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/'
        || c == '%';
}
bool isRegular(char c) { return !isWhite(c) && !isDelim(c); }

/// PDFDocEncoding : octets 0x80 à 0xA0 qui diffèrent du Latin-1.
const char16_t kPdfDoc80[] = {0x2022, 0x2020, 0x2021, 0x2026, 0x2014, 0x2013, 0x0192, 0x2044, 0x2039, 0x203A, 0x2212,
                              0x2030, 0x201E, 0x201C, 0x201D, 0x2018, 0x2019, 0x201A, 0x2122, 0xFB01, 0xFB02, 0x0141,
                              0x0152, 0x0160, 0x0178, 0x017D, 0x0131, 0x0142, 0x0153, 0x0161, 0x017E, 0xFFFD, 0x20AC};

QByteArray formatReal(double v)
{
    if (std::abs(v - std::round(v)) < 1e-9)
        return QByteArray::number(qint64(std::llround(v)));
    QByteArray s = QByteArray::number(v, 'f', 4);
    while (s.endsWith('0'))
        s.chop(1);
    if (s.endsWith('.'))
        s.chop(1);
    if (s == "-0")
        s = "0";
    return s;
}

}  // namespace

// ----- PdfObject -------------------------------------------------------------------------------------------------

PdfObject PdfObject::fromBool(bool v)
{
    PdfObject o;
    o.type = Type::Bool;
    o.boolean = v;
    return o;
}

PdfObject PdfObject::fromInt(qint64 v)
{
    PdfObject o;
    o.type = Type::Int;
    o.integer = v;
    return o;
}

PdfObject PdfObject::fromReal(double v)
{
    PdfObject o;
    o.type = Type::Real;
    o.real = v;
    return o;
}

PdfObject PdfObject::fromName(const QByteArray& name)
{
    PdfObject o;
    o.type = Type::Name;
    o.bytes = name;
    return o;
}

PdfObject PdfObject::fromString(const QByteArray& bytes, bool hex)
{
    PdfObject o;
    o.type = Type::String;
    o.bytes = bytes;
    o.hex = hex;
    return o;
}

PdfObject PdfObject::fromText(const QString& text)
{
    bool ascii = true;
    for (QChar c : text)
        if (c.unicode() > 0x7E || (c.unicode() < 0x20 && c != QLatin1Char('\n') && c != QLatin1Char('\r') && c != QLatin1Char('\t')))
            ascii = false;
    if (ascii)
        return fromString(text.toLatin1());
    QByteArray b("\xFE\xFF", 2);
    for (QChar c : text) {
        b.append(char(c.unicode() >> 8));
        b.append(char(c.unicode() & 0xFF));
    }
    return fromString(b, true);
}

PdfObject PdfObject::fromRef(PdfRef r)
{
    PdfObject o;
    o.type = Type::Ref;
    o.ref = r;
    return o;
}

PdfObject PdfObject::makeArray(std::vector<PdfObject> items)
{
    PdfObject o;
    o.type = Type::Array;
    o.array = std::move(items);
    return o;
}

PdfObject PdfObject::makeDict()
{
    PdfObject o;
    o.type = Type::Dict;
    return o;
}

bool PdfObject::isName(const char* name) const
{
    return type == Type::Name && (!name || bytes == name);
}

double PdfObject::number(double fallback) const
{
    return type == Type::Int ? double(integer) : type == Type::Real ? real : fallback;
}

qint64 PdfObject::toInt(qint64 fallback) const
{
    return type == Type::Int ? integer : type == Type::Real ? qint64(real) : fallback;
}

const PdfObject* PdfObject::find(const QByteArray& key) const
{
    for (const Entry& e : dict)
        if (e.first == key)
            return &e.second;
    return nullptr;
}

PdfObject PdfObject::value(const QByteArray& key) const
{
    const PdfObject* o = find(key);
    return o ? *o : PdfObject{};
}

void PdfObject::set(const QByteArray& key, PdfObject v)
{
    for (Entry& e : dict) {
        if (e.first == key) {
            e.second = std::move(v);
            return;
        }
    }
    dict.emplace_back(key, std::move(v));
}

void PdfObject::remove(const QByteArray& key)
{
    std::erase_if(dict, [&key](const Entry& e) { return e.first == key; });
}

QString PdfObject::text() const
{
    if (type != Type::String)
        return {};
    const QByteArray& b = bytes;
    if (b.size() >= 2 && uchar(b[0]) == 0xFE && uchar(b[1]) == 0xFF) {
        QString s;
        for (qsizetype i = 2; i + 1 < b.size(); i += 2)
            s.append(QChar(char16_t((uchar(b[i]) << 8) | uchar(b[i + 1]))));
        return s;
    }
    if (b.size() >= 3 && uchar(b[0]) == 0xEF && uchar(b[1]) == 0xBB && uchar(b[2]) == 0xBF)
        return QString::fromUtf8(b.mid(3));
    QString s;
    s.reserve(b.size());
    for (char c : b) {
        const uchar u = uchar(c);
        s.append(u >= 0x80 && u <= 0xA0 ? QChar(kPdfDoc80[u - 0x80]) : QChar(char16_t(u)));
    }
    return s;
}

// ----- Écriture --------------------------------------------------------------------------------------------------

QByteArray serialize(const PdfObject& o)
{
    using T = PdfObject::Type;
    switch (o.type) {
    case T::Null:
        return "null";
    case T::Bool:
        return o.boolean ? "true" : "false";
    case T::Int:
        return QByteArray::number(o.integer);
    case T::Real:
        return formatReal(o.real);
    case T::Name: {
        QByteArray out("/");
        for (char c : o.bytes) {
            const uchar u = uchar(c);
            if (u < 0x21 || u > 0x7E || c == '#' || isDelim(c))
                out += '#' + QByteArray::number(u, 16).rightJustified(2, '0').toUpper();
            else
                out += c;
        }
        return out;
    }
    case T::String: {
        if (o.hex)
            return '<' + o.bytes.toHex().toUpper() + '>';
        QByteArray out("(");
        for (char c : o.bytes) {
            switch (c) {
            case '(':
            case ')':
            case '\\':
                out += '\\';
                out += c;
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                out += c;
            }
        }
        out += ')';
        return out;
    }
    case T::Array: {
        QByteArray out("[");
        for (size_t i = 0; i < o.array.size(); ++i) {
            if (i > 0)
                out += ' ';
            out += serialize(o.array[i]);
        }
        out += ']';
        return out;
    }
    case T::Dict:
    case T::Stream: {
        QByteArray out("<<");
        for (const auto& [key, value] : o.dict) {
            if (o.type == T::Stream && key == "Length")
                continue;
            out += serialize(PdfObject::fromName(key));
            out += ' ';
            out += serialize(value);
        }
        if (o.type == T::Stream)
            out += "/Length " + QByteArray::number(o.stream.size());
        out += ">>";
        if (o.type == T::Stream)
            out += "\nstream\n" + o.stream + "\nendstream";
        return out;
    }
    case T::Ref:
        return QByteArray::number(o.ref.num) + ' ' + QByteArray::number(o.ref.gen) + " R";
    }
    return "null";
}

// ----- Analyse ---------------------------------------------------------------------------------------------------

void PdfParser::skipSpace()
{
    while (m_pos < m_data.size()) {
        const char c = m_data[m_pos];
        if (isWhite(c)) {
            ++m_pos;
        } else if (c == '%') {
            while (m_pos < m_data.size() && m_data[m_pos] != '\n' && m_data[m_pos] != '\r')
                ++m_pos;
        } else {
            break;
        }
    }
}

QByteArray PdfParser::keyword(bool peek)
{
    skipSpace();
    qsizetype p = m_pos;
    while (p < m_data.size() && isRegular(m_data[p]))
        ++p;
    QByteArray k = m_data.mid(m_pos, p - m_pos);
    if (!peek)
        m_pos = p;
    return k;
}

std::optional<PdfObject> PdfParser::parseString()
{
    ++m_pos;  // (
    QByteArray out;
    int depth = 1;
    while (m_pos < m_data.size()) {
        char c = m_data[m_pos++];
        if (c == '\\') {
            if (m_pos >= m_data.size())
                break;
            c = m_data[m_pos++];
            switch (c) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '\r':
                if (m_pos < m_data.size() && m_data[m_pos] == '\n')
                    ++m_pos;
                break;
            case '\n':
                break;
            default:
                if (c >= '0' && c <= '7') {
                    int v = c - '0';
                    for (int i = 0; i < 2 && m_pos < m_data.size() && m_data[m_pos] >= '0' && m_data[m_pos] <= '7'; ++i)
                        v = v * 8 + (m_data[m_pos++] - '0');
                    out += char(v & 0xFF);
                } else {
                    out += c;
                }
            }
        } else if (c == '(') {
            ++depth;
            out += c;
        } else if (c == ')') {
            if (--depth == 0)
                return PdfObject::fromString(out);
            out += c;
        } else {
            out += c;
        }
    }
    return std::nullopt;
}

std::optional<PdfObject> PdfParser::parseHexString()
{
    ++m_pos;  // <
    QByteArray hex;
    while (m_pos < m_data.size() && m_data[m_pos] != '>') {
        const char c = m_data[m_pos++];
        if (std::isxdigit(uchar(c)))
            hex += c;
    }
    if (m_pos >= m_data.size())
        return std::nullopt;
    ++m_pos;  // >
    if (hex.size() % 2)
        hex += '0';
    return PdfObject::fromString(QByteArray::fromHex(hex), true);
}

PdfObject PdfParser::parseName()
{
    ++m_pos;  // /
    QByteArray name;
    while (m_pos < m_data.size() && isRegular(m_data[m_pos])) {
        const char c = m_data[m_pos++];
        if (c == '#' && m_pos + 1 < m_data.size() && std::isxdigit(uchar(m_data[m_pos]))
            && std::isxdigit(uchar(m_data[m_pos + 1]))) {
            name += char(m_data.mid(m_pos, 2).toInt(nullptr, 16));
            m_pos += 2;
        } else {
            name += c;
        }
    }
    return PdfObject::fromName(name);
}

std::optional<PdfObject> PdfParser::parseNumberOrRef()
{
    const QByteArray first = keyword();
    bool ok = false;
    const qint64 n = first.toLongLong(&ok);
    if (!ok) {
        const double d = first.toDouble(&ok);
        if (!ok)
            return std::nullopt;
        return PdfObject::fromReal(d);
    }
    // « N G R » ?
    const qsizetype save = m_pos;
    const QByteArray second = keyword();
    bool ok2 = false;
    const int gen = second.toInt(&ok2);
    if (ok2 && !second.contains('.') && !second.startsWith('-') && !second.startsWith('+') && keyword() == "R")
        return PdfObject::fromRef({int(n), gen});
    m_pos = save;
    return PdfObject::fromInt(n);
}

std::optional<PdfObject> PdfParser::parse(const std::function<qint64(PdfRef)>& lengthResolver)
{
    skipSpace();
    if (m_pos >= m_data.size() || ++m_depth > 64) {
        --m_depth;
        return std::nullopt;
    }
    struct DepthGuard {
        int& d;
        ~DepthGuard() { --d; }
    } guard{m_depth};
    const char c = m_data[m_pos];
    if (c == '/')
        return parseName();
    if (c == '(')
        return parseString();
    if (c == '[') {
        ++m_pos;
        PdfObject arr = PdfObject::makeArray();
        while (true) {
            skipSpace();
            if (m_pos >= m_data.size())
                return std::nullopt;
            if (m_data[m_pos] == ']') {
                ++m_pos;
                return arr;
            }
            auto item = parse(lengthResolver);
            if (!item)
                return std::nullopt;
            arr.array.push_back(std::move(*item));
        }
    }
    if (c == '<') {
        if (m_pos + 1 < m_data.size() && m_data[m_pos + 1] == '<') {
            m_pos += 2;
            PdfObject dict = PdfObject::makeDict();
            while (true) {
                skipSpace();
                if (m_pos + 1 >= m_data.size())
                    return std::nullopt;
                if (m_data[m_pos] == '>' && m_data[m_pos + 1] == '>') {
                    m_pos += 2;
                    break;
                }
                if (m_data[m_pos] != '/')
                    return std::nullopt;
                const QByteArray key = parseName().bytes;
                auto value = parse(lengthResolver);
                if (!value)
                    return std::nullopt;
                dict.dict.emplace_back(key, std::move(*value));
            }
            // Flux ?
            const qsizetype save = m_pos;
            if (keyword() == "stream") {
                if (m_pos < m_data.size() && m_data[m_pos] == '\r')
                    ++m_pos;
                if (m_pos < m_data.size() && m_data[m_pos] == '\n')
                    ++m_pos;
                qint64 length = -1;
                const PdfObject len = dict.value("Length");
                if (len.type == PdfObject::Type::Int)
                    length = len.integer;
                else if (len.type == PdfObject::Type::Ref && lengthResolver)
                    length = lengthResolver(len.ref);
                const qsizetype start = m_pos;
                bool valid = length >= 0 && start + length <= m_data.size();
                if (valid) {
                    PdfParser check(m_data, start + length);
                    valid = check.keyword() == "endstream";
                }
                if (!valid) {  // longueur fausse : jusqu'à « endstream »
                    const qsizetype end = m_data.indexOf("endstream", start);
                    if (end < 0)
                        return std::nullopt;
                    qsizetype e = end;
                    if (e > start && m_data[e - 1] == '\n')
                        --e;
                    if (e > start && m_data[e - 1] == '\r')
                        --e;
                    length = e - start;
                }
                dict.type = PdfObject::Type::Stream;
                dict.stream = m_data.mid(start, length);
                m_pos = start + length;
                keyword();  // endstream
            } else {
                m_pos = save;
            }
            return dict;
        }
        return parseHexString();
    }
    if (c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9'))
        return parseNumberOrRef();
    const QByteArray k = keyword();
    if (k == "true")
        return PdfObject::fromBool(true);
    if (k == "false")
        return PdfObject::fromBool(false);
    if (k == "null")
        return PdfObject::null();
    return std::nullopt;
}

std::optional<std::pair<PdfRef, PdfObject>> PdfParser::parseIndirect(const std::function<qint64(PdfRef)>& lengthResolver)
{
    bool ok1 = false;
    bool ok2 = false;
    const int num = keyword().toInt(&ok1);
    const int gen = keyword().toInt(&ok2);
    if (!ok1 || !ok2 || keyword() != "obj")
        return std::nullopt;
    auto obj = parse(lengthResolver);
    if (!obj)
        return std::nullopt;
    return std::make_pair(PdfRef{num, gen}, std::move(*obj));
}

// ----- Décodage --------------------------------------------------------------------------------------------------

QByteArray inflate(const QByteArray& data)
{
    if (data.isEmpty())
        return {};
    QByteArray framed(4, '\0');
    qToBigEndian<quint32>(quint32(std::min<qint64>(qint64(data.size()) * 6 + 64, 64 * 1024 * 1024)), framed.data());
    framed += data;
    return qUncompress(framed);
}

QByteArray undoPngPredictor(const QByteArray& data, int columns, int colors, int bitsPerComponent)
{
    const int bpp = std::max(1, colors * bitsPerComponent / 8);
    const int rowBytes = (columns * colors * bitsPerComponent + 7) / 8;
    if (rowBytes <= 0)
        return {};
    QByteArray out;
    QByteArray prev(rowBytes, '\0');
    qsizetype pos = 0;
    while (pos + 1 + rowBytes <= data.size()) {
        const int filter = uchar(data[pos]);
        QByteArray row = data.mid(pos + 1, rowBytes);
        for (int i = 0; i < rowBytes; ++i) {
            const int a = i >= bpp ? uchar(row[i - bpp]) : 0;
            const int b = uchar(prev[i]);
            const int c = i >= bpp ? uchar(prev[i - bpp]) : 0;
            int x = uchar(row[i]);
            switch (filter) {
            case 1: x += a; break;
            case 2: x += b; break;
            case 3: x += (a + b) / 2; break;
            case 4: {
                const int p = a + b - c;
                const int pa = std::abs(p - a);
                const int pb = std::abs(p - b);
                const int pc = std::abs(p - c);
                x += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                break;
            }
            default: break;
            }
            row[i] = char(x & 0xFF);
        }
        out += row;
        prev = row;
        pos += 1 + rowBytes;
    }
    return out;
}

// ----- Fichier ---------------------------------------------------------------------------------------------------

Result<PdfFile> PdfFile::load(const QByteArray& data)
{
    PdfFile f;
    f.m_data = data;
    if (!data.startsWith("%PDF-") && data.indexOf("%PDF-") < 0)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Ce fichier n'est pas un PDF."));
    const qsizetype sx = data.lastIndexOf("startxref");
    bool ok = false;
    if (sx >= 0) {
        PdfParser p(f.m_data, sx + 9);
        const qint64 offset = p.keyword().toLongLong(&ok);
        ok = ok && offset > 0 && offset < data.size() && f.readXrefChain(offset);
        if (ok)
            f.m_lastXref = offset;
    }
    if (!ok || !f.m_trailer.find("Root")) {
        f.rebuildXref();
        if (!f.m_trailer.find("Root"))
            return Error::make(Error::Code::InvalidArgument, QStringLiteral("Structure du PDF illisible."));
    }
    int maxNum = 0;
    for (auto it = f.m_xref.cbegin(); it != f.m_xref.cend(); ++it)
        maxNum = std::max(maxNum, it.key());
    f.m_size = std::max<int>(int(f.m_trailer.value("Size").toInt()), maxNum + 1);
    if (!f.encrypted()) {
        const PdfObject root = f.resolve(f.m_trailer.value("Root"));
        const PdfObject pages = root.value("Pages");
        f.collectPages(f.resolve(pages), pages.type == PdfObject::Type::Ref ? pages.ref : PdfRef{}, PdfObject::makeDict(), 0);
    }
    return f;
}

bool PdfFile::readXrefChain(qint64 start)
{
    QSet<qint64> seen;
    qint64 offset = start;
    bool first = true;
    while (offset > 0 && offset < m_data.size() && !seen.contains(offset)) {
        seen.insert(offset);
        PdfObject trailer;
        PdfParser p(m_data, offset);
        const bool table = p.keyword(true) == "xref";
        const bool ok = table ? readXrefTable(offset, trailer) : readXrefStream(offset, trailer);
        if (!ok)
            return false;
        if (first) {
            m_trailer = trailer;
            m_trailer.remove("Prev");
            m_trailer.remove("XRefStm");
            m_xrefStream = !table;
            first = false;
        }
        // Fichier hybride : flux de références complémentaire
        if (table) {
            if (const PdfObject* stm = trailer.find("XRefStm"); stm && stm->type == PdfObject::Type::Int) {
                PdfObject ignored;
                readXrefStream(stm->integer, ignored);
            }
        }
        const PdfObject* prev = trailer.find("Prev");
        offset = prev ? prev->toInt(-1) : -1;
    }
    return !first;
}

bool PdfFile::readXrefTable(qint64 offset, PdfObject& trailer)
{
    PdfParser p(m_data, offset);
    if (p.keyword() != "xref")
        return false;
    while (true) {
        const QByteArray k = p.keyword(true);
        if (k == "trailer") {
            p.keyword();
            auto t = p.parse();
            if (!t || t->type != PdfObject::Type::Dict)
                return false;
            trailer = *t;
            return true;
        }
        bool ok1 = false;
        bool ok2 = false;
        const int startNum = p.keyword().toInt(&ok1);
        const int count = p.keyword().toInt(&ok2);
        if (!ok1 || !ok2 || count < 0)
            return false;
        for (int i = 0; i < count; ++i) {
            bool okA = false;
            bool okB = false;
            const qint64 off = p.keyword().toLongLong(&okA);
            const int gen = p.keyword().toInt(&okB);
            const QByteArray kind = p.keyword();
            if (!okA || !okB)
                return false;
            const int num = startNum + i;
            if (m_xref.contains(num))
                continue;  // révision plus récente déjà lue
            Entry e;
            if (kind == "n") {
                e.type = 1;
                e.offset = off;
                e.gen = gen;
            }
            m_xref.insert(num, e);
        }
    }
}

bool PdfFile::readXrefStream(qint64 offset, PdfObject& trailer)
{
    PdfParser p(m_data, offset);
    auto obj = p.parseIndirect([this](PdfRef r) { return streamLength(r); });
    if (!obj || obj->second.type != PdfObject::Type::Stream || !obj->second.value("Type").isName("XRef"))
        return false;
    const PdfObject& s = obj->second;
    trailer = s;
    trailer.type = PdfObject::Type::Dict;
    trailer.stream.clear();
    const QByteArray data = decodeStream(s);
    const PdfObject w = s.value("W");
    if (w.array.size() != 3)
        return false;
    const int w0 = int(w.array[0].toInt());
    const int w1 = int(w.array[1].toInt());
    const int w2 = int(w.array[2].toInt());
    const int rowSize = w0 + w1 + w2;
    if (rowSize <= 0)
        return false;
    std::vector<std::pair<int, int>> ranges;
    const PdfObject index = s.value("Index");
    if (index.array.size() >= 2) {
        for (size_t i = 0; i + 1 < index.array.size(); i += 2)
            ranges.emplace_back(int(index.array[i].toInt()), int(index.array[i + 1].toInt()));
    } else {
        ranges.emplace_back(0, int(s.value("Size").toInt()));
    }
    auto field = [&data](qsizetype at, int width) {
        qint64 v = 0;
        for (int i = 0; i < width; ++i)
            v = (v << 8) | uchar(data[at + i]);
        return v;
    };
    qsizetype pos = 0;
    for (const auto& [startNum, count] : ranges) {
        for (int i = 0; i < count && pos + rowSize <= data.size(); ++i, pos += rowSize) {
            const int num = startNum + i;
            if (m_xref.contains(num))
                continue;
            const int type = w0 == 0 ? 1 : int(field(pos, w0));
            const qint64 f2 = field(pos + w0, w1);
            const qint64 f3 = field(pos + w0 + w1, w2);
            Entry e;
            e.type = type == 1 || type == 2 ? type : 0;
            if (type == 1) {
                e.offset = f2;
                e.gen = int(f3);
            } else if (type == 2) {
                e.stream = int(f2);
                e.index = int(f3);
            }
            m_xref.insert(num, e);
        }
    }
    return true;
}

void PdfFile::rebuildXref()
{
    m_xref.clear();
    m_trailer = PdfObject::makeDict();
    static const QRegularExpression objRe(QStringLiteral("(\\d+)\\s+(\\d+)\\s+obj\\b"));
    const QString latin = QString::fromLatin1(m_data);
    auto it = objRe.globalMatch(latin);
    while (it.hasNext()) {
        const auto m = it.next();
        const qsizetype start = m.capturedStart();
        if (start > 0 && isRegular(m_data[start - 1]))
            continue;
        Entry e;
        e.type = 1;
        e.offset = start;
        e.gen = m.captured(2).toInt();
        m_xref.insert(m.captured(1).toInt(), e);  // le dernier l'emporte : révision la plus récente
    }
    // Dictionnaire « trailer » le plus récent, sinon un flux de références, sinon le catalogue trouvé.
    const qsizetype t = m_data.lastIndexOf("trailer");
    if (t >= 0) {
        PdfParser p(m_data, t + 7);
        if (auto d = p.parse(); d && d->type == PdfObject::Type::Dict)
            m_trailer = *d;
    }
    if (!m_trailer.find("Root")) {
        for (auto e = m_xref.cbegin(); e != m_xref.cend(); ++e) {
            const PdfObject o = object(e.key());
            if (o.isDict() && o.value("Type").isName("Catalog")) {
                m_trailer.set("Root", PdfObject::fromRef({e.key(), e.value().gen}));
                break;
            }
            if (o.type == PdfObject::Type::Stream && o.value("Type").isName("XRef") && o.find("Root"))
                m_trailer.set("Root", o.value("Root"));
        }
    }
    m_trailer.remove("Prev");
    m_xrefStream = false;
    m_lastXref = 0;
}

qint64 PdfFile::streamLength(PdfRef r) const
{
    const auto it = m_xref.constFind(r.num);
    if (it == m_xref.cend() || it->type != 1)
        return -1;
    PdfParser p(m_data, it->offset);
    auto obj = p.parseIndirect();
    return obj && obj->second.type == PdfObject::Type::Int ? obj->second.integer : -1;
}

PdfObject PdfFile::object(int num) const
{
    const auto it = m_xref.constFind(num);
    if (it == m_xref.cend())
        return {};
    if (it->type == 1) {
        PdfParser p(m_data, it->offset);
        auto obj = p.parseIndirect([this](PdfRef r) { return streamLength(r); });
        return obj && obj->first.num == num ? obj->second : PdfObject{};
    }
    if (it->type == 2) {
        if (!m_objectStreams.contains(it->stream)) {
            std::vector<std::pair<int, PdfObject>> objects;
            const PdfObject s = object(it->stream);
            if (s.type == PdfObject::Type::Stream) {
                const QByteArray data = decodeStream(s);
                const int n = int(s.value("N").toInt());
                const qint64 first = s.value("First").toInt();
                PdfParser header(data);
                std::vector<std::pair<int, qint64>> offsets;
                for (int i = 0; i < n; ++i) {
                    bool ok1 = false;
                    bool ok2 = false;
                    const int objNum = header.keyword().toInt(&ok1);
                    const qint64 off = header.keyword().toLongLong(&ok2);
                    if (!ok1 || !ok2)
                        break;
                    offsets.emplace_back(objNum, off);
                }
                for (const auto& [objNum, off] : offsets) {
                    PdfParser body(data, first + off);
                    auto o = body.parse();
                    objects.emplace_back(objNum, o ? *o : PdfObject{});
                }
            }
            m_objectStreams.insert(it->stream, std::move(objects));
        }
        const auto& objects = m_objectStreams[it->stream];
        if (it->index >= 0 && size_t(it->index) < objects.size() && objects[it->index].first == num)
            return objects[it->index].second;
        for (const auto& [n, o] : objects)
            if (n == num)
                return o;
    }
    return {};
}

PdfObject PdfFile::resolve(const PdfObject& o) const
{
    PdfObject cur = o;
    for (int i = 0; i < 16 && cur.type == PdfObject::Type::Ref; ++i)
        cur = object(cur.ref.num);
    return cur;
}

QByteArray PdfFile::decodeStream(const PdfObject& stream) const
{
    const PdfObject filter = resolve(stream.value("Filter"));
    QByteArray data = stream.stream;
    std::vector<PdfObject> filters;
    if (filter.type == PdfObject::Type::Name)
        filters.push_back(filter);
    else if (filter.type == PdfObject::Type::Array)
        filters = filter.array;
    const PdfObject parms = resolve(stream.value("DecodeParms"));
    for (size_t i = 0; i < filters.size(); ++i) {
        const QByteArray name = filters[i].bytes;
        if (name != "FlateDecode" && name != "Fl")
            return {};
        data = inflate(data);
        const PdfObject p = parms.type == PdfObject::Type::Array ? (i < parms.array.size() ? resolve(parms.array[i]) : PdfObject{})
                                                                 : parms;
        const int predictor = int(p.value("Predictor").toInt(1));
        if (predictor >= 10)
            data = undoPngPredictor(data, int(p.value("Columns").toInt(1)), int(p.value("Colors").toInt(1)),
                                    int(p.value("BitsPerComponent").toInt(8)));
        else if (predictor == 2)
            return {};  // prédicteur TIFF : non pris en charge
    }
    return data;
}

void PdfFile::collectPages(const PdfObject& node, PdfRef ref, const PdfObject& inherited, int depth)
{
    if (depth > 32 || !node.isDict())
        return;
    PdfObject attrs = inherited;
    for (const char* key : {"MediaBox", "CropBox", "Rotate", "Resources"})
        if (const PdfObject* v = node.find(key))
            attrs.set(key, resolve(*v));
    const PdfObject type = node.value("Type");
    const PdfObject* kids = node.find("Kids");
    if (type.isName("Page") || (!kids && !type.isName("Pages"))) {
        PdfObject page = node;
        for (const auto& [key, value] : attrs.dict)
            if (!page.find(key))
                page.set(key, value);
        m_pages.push_back({ref, page});
        return;
    }
    const PdfObject k = kids ? resolve(*kids) : PdfObject{};
    for (const PdfObject& kid : k.array) {
        if (kid.type != PdfObject::Type::Ref)
            continue;
        collectPages(resolve(kid), kid.ref, attrs, depth + 1);
    }
}

}  // namespace studylab::pdf
