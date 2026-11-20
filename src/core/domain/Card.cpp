#include "core/domain/Card.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace studylab {

QString cardTypeKey(CardType type)
{
    switch (type) {
    case CardType::Reversed: return QStringLiteral("reversed");
    case CardType::Cloze: return QStringLiteral("cloze");
    case CardType::Formula: return QStringLiteral("formula");
    case CardType::Basic: break;
    }
    return QStringLiteral("basic");
}

CardType cardTypeFromKey(const QString& key)
{
    if (key == QLatin1String("reversed"))
        return CardType::Reversed;
    if (key == QLatin1String("cloze"))
        return CardType::Cloze;
    if (key == QLatin1String("formula"))
        return CardType::Formula;
    return CardType::Basic;
}

QString cardStateKey(CardState state)
{
    switch (state) {
    case CardState::Learning: return QStringLiteral("learning");
    case CardState::Review: return QStringLiteral("review");
    case CardState::Relearning: return QStringLiteral("relearning");
    case CardState::New: break;
    }
    return QStringLiteral("new");
}

CardState cardStateFromKey(const QString& key)
{
    if (key == QLatin1String("learning"))
        return CardState::Learning;
    if (key == QLatin1String("review"))
        return CardState::Review;
    if (key == QLatin1String("relearning"))
        return CardState::Relearning;
    return CardState::New;
}

QString cardSourceKey(CardSource source)
{
    switch (source) {
    case CardSource::Annotation: return QStringLiteral("annotation");
    case CardSource::Note: return QStringLiteral("note");
    case CardSource::Comment: return QStringLiteral("comment");
    case CardSource::Document: return QStringLiteral("document");
    case CardSource::None: break;
    }
    return {};
}

CardSource cardSourceFromKey(const QString& key)
{
    if (key == QLatin1String("annotation"))
        return CardSource::Annotation;
    if (key == QLatin1String("note"))
        return CardSource::Note;
    if (key == QLatin1String("comment"))
        return CardSource::Comment;
    if (key == QLatin1String("document"))
        return CardSource::Document;
    return CardSource::None;
}

namespace {
QJsonArray stepsArray(const QList<int>& steps)
{
    QJsonArray a;
    for (int m : steps)
        a.append(m);
    return a;
}

QList<int> stepsFrom(const QJsonValue& v, const QList<int>& fallback)
{
    if (!v.isArray())
        return fallback;
    QList<int> out;
    for (const QJsonValue& m : v.toArray()) {
        const int minutes = m.toInt(0);
        if (minutes < 1 || minutes > 1440)
            return fallback;
        out << minutes;
    }
    return out;
}
}  // namespace

QJsonObject Deck::configJson() const
{
    return {{QStringLiteral("newPerDay"), newPerDay},
            {QStringLiteral("reviewsPerDay"), reviewsPerDay},
            {QStringLiteral("learningSteps"), stepsArray(learningSteps)},
            {QStringLiteral("relearningSteps"), stepsArray(relearningSteps)},
            {QStringLiteral("retention"), retention},
            {QStringLiteral("maxInterval"), maxIntervalDays}};
}

void Deck::applyConfig(const QJsonObject& config)
{
    newPerDay = std::clamp(config.value(QStringLiteral("newPerDay")).toInt(newPerDay), 0, 9999);
    reviewsPerDay = std::clamp(config.value(QStringLiteral("reviewsPerDay")).toInt(reviewsPerDay), 0, 99999);
    learningSteps = stepsFrom(config.value(QStringLiteral("learningSteps")), learningSteps);
    relearningSteps = stepsFrom(config.value(QStringLiteral("relearningSteps")), relearningSteps);
    retention = std::clamp(config.value(QStringLiteral("retention")).toDouble(retention), 0.7, 0.99);
    maxIntervalDays = std::clamp(config.value(QStringLiteral("maxInterval")).toInt(maxIntervalDays), 1, 36500);
}

QJsonObject ScheduleState::toJson() const
{
    QJsonObject o{{QStringLiteral("state"), cardStateKey(state)},
                  {QStringLiteral("due"), due.isValid() ? due.toUTC().toString(Qt::ISODate) : QString()},
                  {QStringLiteral("interval"), intervalDays},
                  {QStringLiteral("ease"), ease},
                  {QStringLiteral("reps"), reps},
                  {QStringLiteral("lapses"), lapses},
                  {QStringLiteral("step"), step}};
    if (stability > 0)
        o.insert(QStringLiteral("stability"), stability);
    if (difficulty > 0)
        o.insert(QStringLiteral("difficulty"), difficulty);
    return o;
}

std::optional<QList<int>> parseSteps(const QString& text)
{
    static const QRegularExpression item(QStringLiteral("^(\\d+(?:\\.\\d+)?)(m|min|h|j|d)?$"),
                                         QRegularExpression::CaseInsensitiveOption);
    QList<int> out;
    // Séparateurs : espaces, virgules, points-virgules ; décimales avec un point (« 1.5h »).
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const auto m = item.match(part.trimmed());
        if (!m.hasMatch())
            return std::nullopt;
        double value = m.captured(1).toDouble();
        const QString unit = m.captured(2).toLower();
        if (unit == QLatin1String("h"))
            value *= 60;
        else if (unit == QLatin1String("j") || unit == QLatin1String("d"))
            value *= 1440;
        const int minutes = int(std::lround(value));
        if (minutes < 1 || minutes > 1440)
            return std::nullopt;
        out << minutes;
    }
    return out;
}

QString stepsText(const QList<int>& minutes)
{
    QStringList parts;
    for (int m : minutes)
        parts << (m >= 60 && m % 60 == 0 ? QStringLiteral("%1h").arg(m / 60) : QString::number(m));
    return parts.join(u' ');
}

QList<int> clozeNumbers(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("\\{\\{c(\\d+)::"));
    QList<int> out;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const int n = it.next().captured(1).toInt();
        if (n > 0 && !out.contains(n))
            out << n;
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace studylab
