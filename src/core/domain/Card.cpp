#include "core/domain/Card.h"

#include <QRegularExpression>

#include <algorithm>

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

QJsonObject Deck::configJson() const
{
    return {{QStringLiteral("newPerDay"), newPerDay}, {QStringLiteral("reviewsPerDay"), reviewsPerDay}};
}

void Deck::applyConfig(const QJsonObject& config)
{
    newPerDay = std::clamp(config.value(QStringLiteral("newPerDay")).toInt(newPerDay), 0, 9999);
    reviewsPerDay = std::clamp(config.value(QStringLiteral("reviewsPerDay")).toInt(reviewsPerDay), 0, 99999);
}

QJsonObject ScheduleState::toJson() const
{
    return {{QStringLiteral("state"), cardStateKey(state)},
            {QStringLiteral("due"), due.isValid() ? due.toUTC().toString(Qt::ISODate) : QString()},
            {QStringLiteral("interval"), intervalDays},
            {QStringLiteral("ease"), ease},
            {QStringLiteral("reps"), reps},
            {QStringLiteral("lapses"), lapses},
            {QStringLiteral("step"), step}};
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
