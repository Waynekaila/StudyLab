#include "ui/reader/EpubWeb.h"

#include "engines/epub/EpubPackage.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QPointer>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

namespace studylab::ui {

namespace {

/// Refuse toute requête hors du schéma du lecteur et des contenus locaux (blob:, data:).
class OfflineInterceptor : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo& info) override
    {
        const QString scheme = info.requestUrl().scheme();
        if (scheme != QLatin1String(kEpubScheme) && scheme != QLatin1String("blob")
            && scheme != QLatin1String("data") && scheme != QLatin1String("qrc"))
            info.block(true);
    }
};

QPointer<QWebEngineProfile> g_profile;
EpubSchemeHandler* g_handler = nullptr;

void reply(QWebEngineUrlRequestJob* job, const QByteArray& data, const QByteArray& mime)
{
    auto* buffer = new QBuffer(job);
    buffer->setData(data);
    buffer->open(QIODevice::ReadOnly);
    job->reply(mime, buffer);
}

}  // namespace

void registerReaderSchemes()
{
    QWebEngineUrlScheme scheme(kEpubScheme);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::CorsEnabled);
    QWebEngineUrlScheme::registerScheme(scheme);
}

EpubSchemeHandler::EpubSchemeHandler(QObject* parent) : QWebEngineUrlSchemeHandler(parent) {}

EpubSchemeHandler::~EpubSchemeHandler() = default;

void EpubSchemeHandler::registerBook(const QString& token, std::shared_ptr<EpubPackage> package)
{
    Book book;
    book.sizes = package->fileSizes();
    book.package = std::move(package);
    m_books.insert(token, std::move(book));
}

void EpubSchemeHandler::unregisterBook(const QString& token)
{
    m_books.remove(token);
}

QByteArray EpubSchemeHandler::mimeTypeFor(const QString& path)
{
    const QString p = path.toLower();
    if (p.endsWith(QLatin1String(".js")))
        return "text/javascript";
    if (p.endsWith(QLatin1String(".html")) || p.endsWith(QLatin1String(".htm")))
        return "text/html";
    if (p.endsWith(QLatin1String(".xhtml")))
        return "application/xhtml+xml";
    if (p.endsWith(QLatin1String(".css")))
        return "text/css";
    if (p.endsWith(QLatin1String(".svg")))
        return "image/svg+xml";
    if (p.endsWith(QLatin1String(".png")))
        return "image/png";
    if (p.endsWith(QLatin1String(".jpg")) || p.endsWith(QLatin1String(".jpeg")))
        return "image/jpeg";
    if (p.endsWith(QLatin1String(".xml")) || p.endsWith(QLatin1String(".opf")) || p.endsWith(QLatin1String(".ncx")))
        return "application/xml";
    return "application/octet-stream";
}

void EpubSchemeHandler::requestStarted(QWebEngineUrlRequestJob* job)
{
    const QUrl url = job->requestUrl();
    if (url.host() != QLatin1String("reader") || job->requestMethod() != "GET") {
        job->fail(QWebEngineUrlRequestJob::RequestDenied);
        return;
    }
    const QString path = url.path(QUrl::FullyDecoded);
    if (path.startsWith(QLatin1String("/app/"))) {
        const QString file = path.mid(5);
        if (file.contains(QLatin1String(".."))) {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        const QString resource = file == QLatin1String("qwebchannel.js")
                                     ? QStringLiteral(":/qtwebchannel/qwebchannel.js")
                                     : QStringLiteral(":/epub-reader/") + file;
        QFile f(resource);
        if (!f.open(QIODevice::ReadOnly)) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        reply(job, f.readAll(), mimeTypeFor(file));
        return;
    }
    if (path.startsWith(QLatin1String("/book/"))) {
        const int slash = path.indexOf(QLatin1Char('/'), 6);
        if (slash < 0) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        const auto it = m_books.constFind(path.mid(6, slash - 6));
        const QString inArchive = path.mid(slash + 1);
        if (it == m_books.cend() || !it->sizes.contains(inArchive)) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        reply(job, it->package->fileData(inArchive), mimeTypeFor(inArchive));
        return;
    }
    job->fail(QWebEngineUrlRequestJob::UrlNotFound);
}

EpubSchemeHandler* readerSchemeHandler()
{
    (void)readerProfile();
    return g_handler;
}

QWebEngineProfile* readerProfile()
{
    if (g_profile)
        return g_profile;
    // Profil hors ligne (sans nom = « off the record ») : rien n'est écrit sur le disque.
    g_profile = new QWebEngineProfile(QCoreApplication::instance());
    g_profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    g_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    g_profile->setSpellCheckEnabled(false);
    g_handler = new EpubSchemeHandler(g_profile);
    g_profile->installUrlSchemeHandler(kEpubScheme, g_handler);
    g_profile->setUrlRequestInterceptor(new OfflineInterceptor(g_profile));
    QWebEngineSettings* s = g_profile->settings();
    s->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    s->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    s->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    s->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
    s->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    s->setAttribute(QWebEngineSettings::PdfViewerEnabled, false);
    s->setAttribute(QWebEngineSettings::NavigateOnDropEnabled, false);
    s->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, true);
    return g_profile;
}

}  // namespace studylab::ui
