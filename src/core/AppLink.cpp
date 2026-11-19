#include "core/AppLink.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace studylab {

namespace {

bool isUuid(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    return re.match(s).hasMatch();
}

}  // namespace

QString AppLink::toString() const
{
    QString link = QStringLiteral("studylab://open?doc=") + documentUuid;
    if (!annotationUuid.isEmpty())
        link += QStringLiteral("&annotation=") + annotationUuid;
    else if (!commentUuid.isEmpty())
        link += QStringLiteral("&comment=") + commentUuid;
    return link;
}

bool AppLink::looksLikeLink(const QString& argument)
{
    return argument.startsWith(QLatin1String("studylab:"), Qt::CaseInsensitive);
}

std::optional<AppLink> AppLink::parse(const QString& text)
{
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.scheme().compare(QLatin1String("studylab"), Qt::CaseInsensitive) != 0)
        return std::nullopt;
    // « studylab://open?… » ; certains navigateurs ajoutent une barre oblique : « studylab://open/?… »
    if (url.host().compare(QLatin1String("open"), Qt::CaseInsensitive) != 0 || (!url.path().isEmpty() && url.path() != QLatin1String("/")))
        return std::nullopt;
    const QUrlQuery q(url);
    AppLink link;
    link.documentUuid = q.queryItemValue(QStringLiteral("doc")).toLower();
    link.annotationUuid = q.queryItemValue(QStringLiteral("annotation")).toLower();
    link.commentUuid = q.queryItemValue(QStringLiteral("comment")).toLower();
    if (!isUuid(link.documentUuid))
        return std::nullopt;
    if (!link.annotationUuid.isEmpty() && !isUuid(link.annotationUuid))
        return std::nullopt;
    if (!link.commentUuid.isEmpty() && !isUuid(link.commentUuid))
        return std::nullopt;
    return link;
}

}  // namespace studylab
