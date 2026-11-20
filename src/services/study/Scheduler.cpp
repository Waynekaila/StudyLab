#include "services/study/Scheduler.h"

#include <algorithm>
#include <cmath>

namespace studylab {

QList<qint64> Scheduler::previewSeconds(const ScheduleState& current, const QDateTime& nowUtc) const
{
    QList<qint64> out;
    for (Rating r : {Rating::Again, Rating::Hard, Rating::Good, Rating::Easy})
        out << std::max<qint64>(0, nowUtc.secsTo(next(current, r, nowUtc).due));
    return out;
}

ScheduleState Sm2Scheduler::graduate(ScheduleState s, double days, const QDateTime& now) const
{
    const double d = std::clamp(std::round(days), 1.0, double(m_config.maxDays));
    s.state = CardState::Review;
    s.step = 0;
    s.intervalDays = d;
    s.due = now.addSecs(qint64(d * 86400));
    return s;
}

ScheduleState Sm2Scheduler::learn(ScheduleState s, Rating rating, const QDateTime& now, const QList<int>& steps,
                                  bool relearning) const
{
    // Intervalle après la dernière étape : 1 jour (nouvelle carte) ou celui fixé à l'oubli (réapprentissage).
    const double graduateDays = relearning ? std::max(1.0, s.intervalDays) : m_config.graduatingDays;
    const double easyDays = relearning ? graduateDays + 1 : m_config.easyDays;
    if (steps.isEmpty())
        return graduate(s, rating == Rating::Easy ? easyDays : graduateDays, now);
    const int step = std::clamp(s.step, 0, int(steps.size()) - 1);
    s.state = relearning ? CardState::Relearning : CardState::Learning;
    switch (rating) {
    case Rating::Again:
        s.step = 0;
        s.due = now.addSecs(steps.first() * 60);
        return s;
    case Rating::Hard: {
        // Même étape ; sur la première, la moyenne des deux premières (1 et 10 min → 6 min).
        const int minutes = step == 0 && steps.size() > 1 ? (steps.at(0) + steps.at(1) + 1) / 2 : steps.at(step);
        s.step = step;
        s.due = now.addSecs(minutes * 60);
        return s;
    }
    case Rating::Good:
        if (step + 1 < steps.size()) {
            s.step = step + 1;
            s.due = now.addSecs(steps.at(step + 1) * 60);
            return s;
        }
        return graduate(s, graduateDays, now);
    case Rating::Easy:
        return graduate(s, easyDays, now);
    }
    return s;
}

ScheduleState Sm2Scheduler::next(const ScheduleState& current, Rating rating, const QDateTime& nowUtc) const
{
    ScheduleState s = current;
    ++s.reps;
    switch (current.state) {
    case CardState::New:
        s.step = 0;
        return learn(s, rating, nowUtc, m_config.learningMinutes, false);
    case CardState::Learning:
        return learn(s, rating, nowUtc, m_config.learningMinutes, false);
    case CardState::Relearning:
        return learn(s, rating, nowUtc, m_config.relearningMinutes, true);
    case CardState::Review:
        break;
    }

    const double interval = std::max(1.0, current.intervalDays);
    // Jours de retard : une carte revue tard et réussie mérite un intervalle plus long.
    const double late = current.due.isValid() ? std::max<qint64>(0, current.due.secsTo(nowUtc)) / 86400.0 : 0.0;
    const double hard = std::max(interval + 1, std::round(interval * m_config.hardFactor));
    const double good = std::max(hard + 1, std::round((interval + late / 2) * current.ease));
    const double easy = std::max(good + 1, std::round((interval + late) * current.ease * m_config.easyBonus));
    switch (rating) {
    case Rating::Again:
        ++s.lapses;
        s.ease = std::max(m_config.minEase, current.ease - 0.2);
        s.intervalDays = 1;  // intervalle repris après le réapprentissage
        s.step = 0;
        if (m_config.relearningMinutes.isEmpty())
            return graduate(s, 1, nowUtc);
        s.state = CardState::Relearning;
        s.due = nowUtc.addSecs(m_config.relearningMinutes.first() * 60);
        return s;
    case Rating::Hard:
        s.ease = std::max(m_config.minEase, current.ease - 0.15);
        return graduate(s, hard, nowUtc);
    case Rating::Good:
        return graduate(s, good, nowUtc);
    case Rating::Easy:
        s.ease = current.ease + 0.15;
        return graduate(s, easy, nowUtc);
    }
    return s;
}

}  // namespace studylab
