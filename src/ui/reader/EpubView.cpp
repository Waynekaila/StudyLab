#include "ui/reader/EpubView.h"

#include "ui/reader/EpubWeb.h"

#include <QJsonDocument>
#include <QLoggingCategory>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>

namespace studylab::ui {

Q_LOGGING_CATEGORY(lcEpubWeb, "studylab.epub.web", QtWarningMsg)

/// Objet publié dans la page : le lecteur JS y poste ses événements.
class EpubBridge : public QObject {
    Q_OBJECT
public:
    explicit EpubBridge(EpubView* view) : QObject(view), m_view(view) {}
    Q_INVOKABLE void post(const QString& event, const QString& json)
    {
        m_view->handle(event, QJsonDocument::fromJson(json.toUtf8()).object());
    }

private:
    EpubView* m_view;
};

namespace {

class ReaderPage : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString& message, int line,
                                  const QString& source) override
    {
        if (level == ErrorMessageLevel)
            qCWarning(lcEpubWeb).noquote() << message << source << line;
        else
            qCDebug(lcEpubWeb).noquote() << message << source << line;
    }
    bool acceptNavigationRequest(const QUrl& url, NavigationType, bool isMainFrame) override
    {
        // Le lecteur ne quitte jamais sa page ; les chapitres sont des documents blob: en iframe.
        if (isMainFrame)
            return url.scheme() == QLatin1String(kEpubScheme) && url.path() == QLatin1String("/app/reader.html");
        return url.scheme() == QLatin1String("blob") || url.scheme() == QLatin1String("about");
    }
};

QString rgba(const QColor& c)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF(), 0, 'f', 2);
}

const char* fontKey(EpubSettings::Font f)
{
    switch (f) {
    case EpubSettings::Font::Serif: return "serif";
    case EpubSettings::Font::Sans: return "sans";
    case EpubSettings::Font::Original: break;
    }
    return "original";
}

const char* themeKey(ReadingTheme t)
{
    switch (t) {
    case ReadingTheme::Sepia: return "sepia";
    case ReadingTheme::Green: return "green";
    case ReadingTheme::Dark: return "dark";
    case ReadingTheme::White: break;
    }
    return "white";
}

}  // namespace

QJsonObject EpubSettings::toJson() const
{
    const ReadingColors c = readingColors(theme);
    int maxInline = 720;
    int gap = 6;
    if (margins == Margins::Narrow) {
        maxInline = 880;
        gap = 4;
    } else if (margins == Margins::Wide) {
        maxInline = 600;
        gap = 10;
    }
    return {{QStringLiteral("font"), QString::fromLatin1(fontKey(font))},
            {QStringLiteral("fontSize"), fontSize},
            {QStringLiteral("lineHeight"), lineHeight},
            {QStringLiteral("gap"), gap},
            {QStringLiteral("maxInlineSize"), maxInline},
            {QStringLiteral("columns"), 1},
            {QStringLiteral("flow"), scrolled ? QStringLiteral("scrolled") : QStringLiteral("paginated")},
            {QStringLiteral("dark"), c.dark},
            {QStringLiteral("bg"), c.bg.name()},
            {QStringLiteral("fg"), c.text.name()},
            {QStringLiteral("muted"), c.muted.name()},
            {QStringLiteral("link"), c.link.name()},
            {QStringLiteral("selection"), rgba(c.selection)}};
}

QJsonObject EpubSettings::toStored() const
{
    return {{QStringLiteral("font"), QString::fromLatin1(fontKey(font))},
            {QStringLiteral("fontSize"), fontSize},
            {QStringLiteral("lineHeight"), lineHeight},
            {QStringLiteral("margins"), int(margins)},
            {QStringLiteral("scrolled"), scrolled},
            {QStringLiteral("theme"), QString::fromLatin1(themeKey(theme))}};
}

EpubSettings EpubSettings::fromStored(const QJsonObject& o)
{
    EpubSettings s;
    const QString font = o.value(QStringLiteral("font")).toString();
    s.font = font == QLatin1String("serif") ? Font::Serif : font == QLatin1String("sans") ? Font::Sans : Font::Original;
    s.fontSize = std::clamp(o.value(QStringLiteral("fontSize")).toInt(100), 70, 200);
    s.lineHeight = std::clamp(o.value(QStringLiteral("lineHeight")).toDouble(1.6), 1.2, 2.2);
    s.margins = Margins(std::clamp(o.value(QStringLiteral("margins")).toInt(1), 0, 2));
    s.scrolled = o.value(QStringLiteral("scrolled")).toBool(false);
    const QString theme = o.value(QStringLiteral("theme")).toString();
    s.theme = theme == QLatin1String("sepia")  ? ReadingTheme::Sepia
              : theme == QLatin1String("green") ? ReadingTheme::Green
              : theme == QLatin1String("dark")  ? ReadingTheme::Dark
                                                : ReadingTheme::White;
    return s;
}

EpubView::EpubView(QWidget* parent) : QWebEngineView(parent), m_bridge(new EpubBridge(this))
{
    auto* page = new ReaderPage(readerProfile(), this);
    setPage(page);
    auto* channel = new QWebChannel(page);
    channel->registerObject(QStringLiteral("bridge"), m_bridge);
    page->setWebChannel(channel);
    setContextMenuPolicy(Qt::NoContextMenu);
    setAcceptDrops(false);
}

EpubView::~EpubView()
{
    // La page doit disparaître avant le profil partagé.
    delete page();
}

void EpubView::openBook(const QString& token, const QHash<QString, qint64>& sizes, const QString& lastCfi,
                        const EpubSettings& settings)
{
    QJsonObject sz;
    for (auto it = sizes.cbegin(); it != sizes.cend(); ++it)
        sz.insert(it.key(), double(it.value()));
    m_pendingOpen = {{QStringLiteral("base"), QStringLiteral("%1://reader/book/%2/").arg(QLatin1String(kEpubScheme), token)},
                     {QStringLiteral("sizes"), sz},
                     {QStringLiteral("lastLocation"), lastCfi},
                     {QStringLiteral("settings"), settings.toJson()}};
    m_opened = false;
    m_ready = false;
    page()->setBackgroundColor(readingColors(settings.theme).bg);
    load(QUrl(QStringLiteral("%1://reader/app/reader.html").arg(QLatin1String(kEpubScheme))));
}

void EpubView::call(const QString& method, const QJsonValue& arg)
{
    QString args;
    if (!arg.isUndefined())
        args = QString::fromUtf8(QJsonDocument(QJsonArray{arg}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
    page()->runJavaScript(QStringLiteral("window.StudyLab && StudyLab.%1(%2)").arg(method, args));
}

void EpubView::evaluate(const QString& expression, std::function<void(const QVariant&)> callback)
{
    // Enveloppé dans un tableau : QJsonDocument ne lit pas une valeur JSON isolée.
    page()->runJavaScript(QStringLiteral("JSON.stringify([%1])").arg(expression), [callback](const QVariant& v) {
        callback(QJsonDocument::fromJson(v.toString().toUtf8()).array().at(0).toVariant());
    });
}

void EpubView::setSettings(const EpubSettings& settings)
{
    page()->setBackgroundColor(readingColors(settings.theme).bg);
    if (m_ready)
        call(QStringLiteral("setSettings"), settings.toJson());
    else
        m_pendingOpen.insert(QStringLiteral("settings"), settings.toJson());
}

void EpubView::goTo(const QString& target) { call(QStringLiteral("goTo"), target); }
void EpubView::goToFraction(double fraction) { call(QStringLiteral("goToFraction"), fraction); }
void EpubView::next() { call(QStringLiteral("next")); }
void EpubView::previous() { call(QStringLiteral("prev")); }
void EpubView::back() { call(QStringLiteral("back")); }
void EpubView::forward() { call(QStringLiteral("forward")); }
void EpubView::setAnnotations(const QJsonArray& annotations) { call(QStringLiteral("setAnnotations"), annotations); }
void EpubView::reveal(const QString& cfi) { call(QStringLiteral("reveal"), cfi); }
void EpubView::clearSelection() { call(QStringLiteral("clearSelection")); }

void EpubView::search(const QString& query)
{
    m_searchCount = 0;
    m_searchDone = query.isEmpty();
    call(QStringLiteral("search"), query);
}

void EpubView::clearSearch()
{
    m_searchCount = 0;
    m_searchDone = true;
    call(QStringLiteral("clearSearch"));
}

void EpubView::showSearchResult(int index) { call(QStringLiteral("showResult"), index); }

void EpubView::handle(const QString& event, const QJsonObject& d)
{
    const auto rectOf = [](const QJsonValue& v) {
        const QJsonObject r = v.toObject();
        if (r.isEmpty())
            return QRect();
        return QRectF(r.value(QStringLiteral("x")).toDouble(), r.value(QStringLiteral("y")).toDouble(),
                      r.value(QStringLiteral("w")).toDouble(), r.value(QStringLiteral("h")).toDouble())
            .toAlignedRect();
    };
    if (event == QLatin1String("ready")) {
        m_ready = true;
        call(QStringLiteral("open"), m_pendingOpen);
    } else if (event == QLatin1String("opened")) {
        m_toc.clear();
        for (const QJsonValue& v : d.value(QStringLiteral("toc")).toArray()) {
            const QJsonObject o = v.toObject();
            m_toc.append({o.value(QStringLiteral("label")).toString().simplified(), o.value(QStringLiteral("href")).toString(),
                          o.value(QStringLiteral("depth")).toInt(), o.value(QStringLiteral("section")).toInt(-1)});
        }
        m_sections = d.value(QStringLiteral("sections")).toInt();
        m_opened = true;
        emit opened();
    } else if (event == QLatin1String("error")) {
        emit loadFailed(d.value(QStringLiteral("message")).toString());
    } else if (event == QLatin1String("relocated")) {
        EpubLocation l;
        l.cfi = d.value(QStringLiteral("cfi")).toString();
        l.fraction = d.value(QStringLiteral("fraction")).toDouble();
        l.section = d.value(QStringLiteral("section")).toInt();
        l.sections = d.value(QStringLiteral("sections")).toInt();
        l.tocLabel = d.value(QStringLiteral("tocLabel")).toString().simplified();
        l.tocHref = d.value(QStringLiteral("tocHref")).toString();
        l.page = d.value(QStringLiteral("page")).toInt();
        l.pages = d.value(QStringLiteral("pages")).toInt();
        l.sectionMinutes = d.value(QStringLiteral("sectionMinutes")).toDouble();
        m_location = l;
        emit relocated(l);
    } else if (event == QLatin1String("selection")) {
        EpubSelection s;
        s.cfi = d.value(QStringLiteral("cfi")).toString();
        s.text = d.value(QStringLiteral("text")).toString();
        s.prefix = d.value(QStringLiteral("prefix")).toString();
        s.suffix = d.value(QStringLiteral("suffix")).toString();
        s.section = d.value(QStringLiteral("section")).toInt();
        s.fraction = d.value(QStringLiteral("fraction")).toDouble();
        s.progress = d.value(QStringLiteral("progress")).toDouble();
        s.rect = rectOf(d.value(QStringLiteral("rect")));
        emit selectionMade(s);
    } else if (event == QLatin1String("selectionCleared")) {
        emit selectionCleared();
    } else if (event == QLatin1String("annotationClicked")) {
        emit annotationClicked(qint64(d.value(QStringLiteral("id")).toDouble()), rectOf(d.value(QStringLiteral("rect"))));
    } else if (event == QLatin1String("contextMenu")) {
        const QPoint pos(d.value(QStringLiteral("x")).toInt(), d.value(QStringLiteral("y")).toInt());
        emit contextMenuRequested(qint64(d.value(QStringLiteral("id")).toDouble()), mapToGlobal(pos));
    } else if (event == QLatin1String("searchResults")) {
        m_searchCount = d.value(QStringLiteral("count")).toInt();
        m_searchDone = d.value(QStringLiteral("done")).toBool();
        emit searchProgress(m_searchCount, m_searchDone);
    } else if (event == QLatin1String("bookmarkState")) {
        emit bookmarkStateChanged(qint64(d.value(QStringLiteral("id")).toDouble()));
    } else if (event == QLatin1String("escape")) {
        emit escapePressed();
    } else if (event == QLatin1String("externalLink")) {
        emit externalLinkRequested(d.value(QStringLiteral("href")).toString());
    }
}

}  // namespace studylab::ui

#include "EpubView.moc"
