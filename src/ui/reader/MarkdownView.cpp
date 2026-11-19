#include "ui/reader/MarkdownView.h"

#include "ui/reader/EpubWeb.h"

#include <QJsonDocument>
#include <QLoggingCategory>
#include <QWebChannel>
#include <QWebEnginePage>

namespace studylab::ui {

Q_LOGGING_CATEGORY(lcMarkdownWeb, "studylab.markdown.web", QtWarningMsg)

class MarkdownBridge : public QObject {
    Q_OBJECT
public:
    explicit MarkdownBridge(MarkdownView* view) : QObject(view), m_view(view) {}
    Q_INVOKABLE void post(const QString& event, const QString& json)
    {
        m_view->handle(event, QJsonDocument::fromJson(json.toUtf8()).object());
    }

private:
    MarkdownView* m_view;
};

namespace {

const QString kPage = QStringLiteral("/app/markdown/view.html");

class MarkdownPage : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString& message, int line,
                                  const QString& source) override
    {
        if (level == ErrorMessageLevel)
            qCWarning(lcMarkdownWeb).noquote() << message << source << line;
        else
            qCDebug(lcMarkdownWeb).noquote() << message << source << line;
    }
    bool acceptNavigationRequest(const QUrl& url, NavigationType, bool isMainFrame) override
    {
        return isMainFrame && url.scheme() == QLatin1String(kEpubScheme) && url.path() == kPage;
    }
};

}  // namespace

MarkdownView::MarkdownView(QWidget* parent) : QWebEngineView(parent), m_bridge(new MarkdownBridge(this))
{
    auto* page = new MarkdownPage(readerProfile(), this);
    setPage(page);
    auto* channel = new QWebChannel(page);
    channel->registerObject(QStringLiteral("bridge"), m_bridge);
    page->setWebChannel(channel);
    setContextMenuPolicy(Qt::NoContextMenu);
    setAcceptDrops(false);
    load(QUrl(QStringLiteral("%1://reader%2").arg(QLatin1String(kEpubScheme), kPage)));
}

MarkdownView::~MarkdownView()
{
    // La page doit disparaître avant le profil partagé.
    delete page();
}

void MarkdownView::call(const QString& method, const QJsonArray& args)
{
    const QString list = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
    const QString script = QStringLiteral("window.StudyLab && StudyLab.%1(%2)").arg(method, list);
    if (m_ready)
        page()->runJavaScript(script);
    else
        m_pending << script;
}

void MarkdownView::setThemeVars(const QJsonObject& vars)
{
    page()->setBackgroundColor(QColor(vars.value(QStringLiteral("bg")).toString()));
    call(QStringLiteral("setTheme"), {vars});
}

void MarkdownView::setCards(const QJsonArray& cards, const QString& emptyText)
{
    call(QStringLiteral("setCards"), {cards, emptyText});
}

void MarkdownView::setActive(qint64 id, bool scroll) { call(QStringLiteral("setActive"), {double(id), scroll}); }

void MarkdownView::setMarkdown(const QString& text, const QString& emptyText)
{
    call(QStringLiteral("setMarkdown"), {text, emptyText});
}

void MarkdownView::evaluate(const QString& expression, std::function<void(const QVariant&)> callback)
{
    page()->runJavaScript(QStringLiteral("JSON.stringify([%1])").arg(expression), [callback](const QVariant& v) {
        callback(QJsonDocument::fromJson(v.toString().toUtf8()).array().at(0).toVariant());
    });
}

void MarkdownView::handle(const QString& event, const QJsonObject& payload)
{
    const qint64 id = qint64(payload.value(QStringLiteral("id")).toDouble());
    if (event == QLatin1String("ready")) {
        m_ready = true;
        const QStringList pending = std::exchange(m_pending, {});
        for (const QString& script : pending)
            page()->runJavaScript(script);
        emit ready();
    } else if (event == QLatin1String("activate")) {
        emit cardActivated(id);
    } else if (event == QLatin1String("edit")) {
        emit cardEditRequested(id);
    } else if (event == QLatin1String("delete")) {
        emit cardDeleteRequested(id);
    } else if (event == QLatin1String("menu")) {
        const QPoint local(int(payload.value(QStringLiteral("x")).toDouble() * zoomFactor()),
                           int(payload.value(QStringLiteral("y")).toDouble() * zoomFactor()));
        emit cardMenuRequested(id, mapToGlobal(local));
    } else if (event == QLatin1String("link")) {
        const QUrl url(payload.value(QStringLiteral("href")).toString());
        if (url.isValid())
            emit linkActivated(url);
    }
}

}  // namespace studylab::ui

#include "MarkdownView.moc"
