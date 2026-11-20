#include "engines/ocr/TesseractOcr.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>

#include <memory>

namespace studylab {

namespace {

// API C de Tesseract (tesseract/capi.h) : seules les fonctions utilisées, résolues à l'exécution.
using TessApi = void;
using TessIter = void;
enum { RIL_TEXTLINE = 2, RIL_PARA = 1, RIL_WORD = 3 };

struct TessFunctions {
    const char* (*version)() = nullptr;
    TessApi* (*create)() = nullptr;
    void (*destroy)(TessApi*) = nullptr;
    void (*end)(TessApi*) = nullptr;
    int (*init3)(TessApi*, const char*, const char*) = nullptr;
    int (*setVariable)(TessApi*, const char*, const char*) = nullptr;
    void (*setImage)(TessApi*, const unsigned char*, int, int, int, int) = nullptr;
    void (*setSourceResolution)(TessApi*, int) = nullptr;
    int (*recognize)(TessApi*, void*) = nullptr;
    void (*clear)(TessApi*) = nullptr;
    TessIter* (*getIterator)(TessApi*) = nullptr;
    void (*iterDelete)(TessIter*) = nullptr;
    int (*iterNext)(TessIter*, int) = nullptr;
    char* (*iterText)(const TessIter*, int) = nullptr;
    float (*iterConfidence)(const TessIter*, int) = nullptr;
    const void* (*pageIter)(const TessIter*) = nullptr;
    int (*boundingBox)(const void*, int, int*, int*, int*, int*) = nullptr;
    int (*atBeginningOf)(const void*, int) = nullptr;
    void (*deleteText)(const char*) = nullptr;
};

struct Loaded {
    std::unique_ptr<QLibrary> lib;
    TessFunctions fn;
    QString version;
    QString dataPath;
    QStringList languages;
    QString reason;
};

QStringList libraryCandidates()
{
    QStringList out;
    const QString env = qEnvironmentVariable("STUDYLAB_TESSERACT");
    if (!env.isEmpty())
        out << env;
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QStringList names = {QStringLiteral("libtesseract-5"), QStringLiteral("tesseract55"),
                               QStringLiteral("tesseract54"),    QStringLiteral("tesseract53"),
                               QStringLiteral("tesseract52"),    QStringLiteral("tesseract51"),
                               QStringLiteral("tesseract50"),    QStringLiteral("libtesseract-4")};
    QStringList dirs = {appDir};
    for (const char* var : {"ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"}) {
        const QString base = qEnvironmentVariable(var);
        if (!base.isEmpty())
            dirs << QDir(base).filePath(QStringLiteral("Tesseract-OCR"))
                 << QDir(base).filePath(QStringLiteral("Programs/Tesseract-OCR"));
    }
    for (const QString& d : dirs)
        for (const QString& n : names)
            out << QDir(d).filePath(n);
    out << names;  // PATH
#else
    if (!appDir.isEmpty())
        out << QDir(appDir).filePath(QStringLiteral("libtesseract.so.5"));
    out << QStringLiteral("libtesseract.so.5") << QStringLiteral("libtesseract.so.4")
        << QStringLiteral("libtesseract.so") << QStringLiteral("libtesseract.5.dylib")
        << QStringLiteral("/opt/homebrew/lib/libtesseract.dylib") << QStringLiteral("/usr/local/lib/libtesseract.dylib");
#endif
    return out;
}

QStringList languagesIn(const QString& dir)
{
    QStringList langs;
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.traineddata")}, QDir::Files, QDir::Name);
    for (const QString& f : files) {
        const QString code = f.chopped(int(QStringLiteral(".traineddata").size()));
        if (code != QLatin1String("osd") && code != QLatin1String("equ") && !code.isEmpty())
            langs << code;
    }
    return langs;
}

QStringList dataCandidates(const QString& libDir)
{
    QStringList out;
    const QString prefix = qEnvironmentVariable("TESSDATA_PREFIX");
    if (!prefix.isEmpty())
        out << prefix << QDir(prefix).filePath(QStringLiteral("tessdata"));
    if (!libDir.isEmpty())
        out << QDir(libDir).filePath(QStringLiteral("tessdata"));
    out << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tessdata"));
#ifdef Q_OS_WIN
    for (const char* var : {"ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"}) {
        const QString base = qEnvironmentVariable(var);
        if (!base.isEmpty())
            out << QDir(base).filePath(QStringLiteral("Tesseract-OCR/tessdata"))
                << QDir(base).filePath(QStringLiteral("Programs/Tesseract-OCR/tessdata"));
    }
#else
    out << QStringLiteral("/usr/share/tesseract-ocr/5/tessdata") << QStringLiteral("/usr/share/tesseract-ocr/4.00/tessdata")
        << QStringLiteral("/usr/share/tessdata") << QStringLiteral("/usr/local/share/tessdata")
        << QStringLiteral("/opt/homebrew/share/tessdata");
#endif
    return out;
}

template <typename F>
bool resolve(QLibrary& lib, F& target, const char* name)
{
    target = reinterpret_cast<F>(lib.resolve(name));
    return target != nullptr;
}

Loaded& loaded()
{
    static QMutex mutex;
    static std::unique_ptr<Loaded> state;
    QMutexLocker lock(&mutex);
    if (state)
        return *state;
    state = std::make_unique<Loaded>();
    Loaded& s = *state;
    // Une page à la fois sur un seul cœur : Tesseract compilé avec OpenMP prendrait sinon tous les processeurs.
    if (!qEnvironmentVariableIsSet("OMP_THREAD_LIMIT"))
        qputenv("OMP_THREAD_LIMIT", "1");
    for (const QString& candidate : libraryCandidates()) {
        auto lib = std::make_unique<QLibrary>(candidate);
        if (!lib->load())
            continue;
        TessFunctions f;
        const bool ok = resolve(*lib, f.version, "TessVersion") && resolve(*lib, f.create, "TessBaseAPICreate")
                        && resolve(*lib, f.destroy, "TessBaseAPIDelete") && resolve(*lib, f.end, "TessBaseAPIEnd")
                        && resolve(*lib, f.init3, "TessBaseAPIInit3")
                        && resolve(*lib, f.setVariable, "TessBaseAPISetVariable")
                        && resolve(*lib, f.setImage, "TessBaseAPISetImage")
                        && resolve(*lib, f.setSourceResolution, "TessBaseAPISetSourceResolution")
                        && resolve(*lib, f.recognize, "TessBaseAPIRecognize") && resolve(*lib, f.clear, "TessBaseAPIClear")
                        && resolve(*lib, f.getIterator, "TessBaseAPIGetIterator")
                        && resolve(*lib, f.iterDelete, "TessResultIteratorDelete")
                        && resolve(*lib, f.iterNext, "TessResultIteratorNext")
                        && resolve(*lib, f.iterText, "TessResultIteratorGetUTF8Text")
                        && resolve(*lib, f.iterConfidence, "TessResultIteratorConfidence")
                        && resolve(*lib, f.pageIter, "TessResultIteratorGetPageIteratorConst")
                        && resolve(*lib, f.boundingBox, "TessPageIteratorBoundingBox")
                        && resolve(*lib, f.atBeginningOf, "TessPageIteratorIsAtBeginningOf")
                        && resolve(*lib, f.deleteText, "TessDeleteText");
        if (!ok) {
            lib->unload();
            continue;
        }
        s.fn = f;
        s.version = QString::fromUtf8(f.version());
        s.lib = std::move(lib);
        break;
    }
    if (!s.lib) {
        s.reason = QCoreApplication::translate("TesseractOcr", "Tesseract n'est pas installé sur cet ordinateur.");
        return s;
    }
    const QString libDir = QFileInfo(s.lib->fileName()).absolutePath();
    for (const QString& dir : dataCandidates(libDir)) {
        const QStringList langs = languagesIn(dir);
        if (!langs.isEmpty()) {
            s.dataPath = QDir::cleanPath(dir);
            s.languages = langs;
            break;
        }
    }
    if (s.languages.isEmpty())
        s.reason = QCoreApplication::translate("TesseractOcr", "Tesseract %1 est installé, mais aucun modèle de langue "
                                                               "(fichier .traineddata) n'a été trouvé.").arg(s.version);
    return s;
}

}  // namespace

bool TesseractOcr::isAvailable()
{
    const Loaded& s = loaded();
    return s.lib && !s.languages.isEmpty();
}

QString TesseractOcr::version() { return loaded().version; }

QString TesseractOcr::dataPath() { return loaded().dataPath; }

QStringList TesseractOcr::installedLanguages() { return loaded().languages; }

QStringList TesseractOcr::defaultLanguages()
{
    const QStringList installed = installedLanguages();
    QStringList out;
    for (const QString& code : {QStringLiteral("fra"), QStringLiteral("eng")})
        if (installed.contains(code))
            out << code;
    if (out.isEmpty() && !installed.isEmpty())
        out << installed.first();
    return out;
}

QString TesseractOcr::unavailableReason() { return loaded().reason; }

TesseractOcr::TesseractOcr() = default;

TesseractOcr::~TesseractOcr()
{
    if (m_api) {
        const TessFunctions& f = loaded().fn;
        f.end(m_api);
        f.destroy(m_api);
    }
}

Result<void> TesseractOcr::init(const QStringList& languages)
{
    if (!isAvailable())
        return Error::make(Error::Code::NotFound, unavailableReason());
    const Loaded& s = loaded();
    QStringList langs;
    for (const QString& l : languages)
        if (s.languages.contains(l) && !langs.contains(l))
            langs << l;
    if (langs.isEmpty())
        langs = defaultLanguages();
    if (m_api) {
        s.fn.end(m_api);
        s.fn.destroy(m_api);
    }
    m_api = s.fn.create();
    if (!m_api)
        return Error::make(Error::Code::Generic, QStringLiteral("Tesseract : initialisation impossible"));
    const QByteArray path = QDir::toNativeSeparators(s.dataPath).toLocal8Bit();
    const QByteArray lang = langs.join(u'+').toUtf8();
    if (s.fn.init3(m_api, path.constData(), lang.constData()) != 0) {
        s.fn.destroy(m_api);
        m_api = nullptr;
        return Error::make(Error::Code::Generic,
                           QStringLiteral("Tesseract : langues « %1 » introuvables dans %2").arg(QString::fromUtf8(lang), s.dataPath));
    }
    // Pas de messages de Tesseract sur la sortie d'erreur ; les pages vides ne sont pas une erreur.
#ifdef Q_OS_WIN
    s.fn.setVariable(m_api, "debug_file", "NUL");
#else
    s.fn.setVariable(m_api, "debug_file", "/dev/null");
#endif
    return {};
}

Result<OcrPage> TesseractOcr::recognize(const QImage& image, int dpi)
{
    if (!m_api)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Tesseract n'est pas initialisé"));
    if (image.isNull() || image.width() < 8 || image.height() < 8)
        return OcrPage{};
    const TessFunctions& f = loaded().fn;
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    f.setImage(m_api, gray.constBits(), gray.width(), gray.height(), 1, int(gray.bytesPerLine()));
    f.setSourceResolution(m_api, dpi);
    if (f.recognize(m_api, nullptr) != 0) {
        f.clear(m_api);
        return Error::make(Error::Code::Generic, QStringLiteral("Tesseract : la reconnaissance a échoué"));
    }
    OcrPage page;
    const double w = gray.width();
    const double h = gray.height();
    if (TessIter* it = f.getIterator(m_api)) {
        const void* pit = f.pageIter(it);
        int pendingBreak = OcrWord::None;
        do {
            if (f.atBeginningOf(pit, RIL_PARA))
                pendingBreak = OcrWord::Paragraph;
            else if (f.atBeginningOf(pit, RIL_TEXTLINE) && pendingBreak == OcrWord::None)
                pendingBreak = OcrWord::Line;
            char* raw = f.iterText(it, RIL_WORD);
            if (!raw)
                continue;
            const QString text = QString::fromUtf8(raw).trimmed();
            f.deleteText(raw);
            int l = 0, t = 0, r = 0, b = 0;
            // Bruit d'un scan (taches, bords) : mots vides ou de confiance très faible ignorés.
            if (text.isEmpty() || f.iterConfidence(it, RIL_WORD) < 15.0f || !f.boundingBox(pit, RIL_WORD, &l, &t, &r, &b))
                continue;
            OcrWord word;
            word.text = text;
            word.box = QRectF(l / w, t / h, (r - l) / w, (b - t) / h);
            word.breakBefore = page.words.isEmpty() ? OcrWord::None : pendingBreak;
            pendingBreak = OcrWord::None;
            page.words << word;
        } while (f.iterNext(it, RIL_WORD));
        f.iterDelete(it);
    }
    f.clear(m_api);
    return page;
}

}  // namespace studylab
