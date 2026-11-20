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

// ---------------------------------------------------------------------------------------------------------------
// FSRS-5

namespace {
constexpr double kDecay = -0.5;
constexpr double kFactor = 19.0 / 81.0;  // R(S, S) = 0,9
}  // namespace

QList<double> FsrsScheduler::defaultWeights()
{
    return {0.40255, 1.18385, 3.173,  15.69105, 7.1949, 0.5345,  1.4604, 0.0046,  1.54575, 0.1192,
            1.01925, 1.9395,  0.11,   0.29605,  2.2698, 0.2315,  2.9898, 0.51655, 0.6621};
}

double FsrsScheduler::initialStability(Rating rating) const
{
    return std::max(0.1, m_config.w.at(int(rating) - 1));
}

double FsrsScheduler::initialDifficulty(Rating rating) const
{
    const auto& w = m_config.w;
    return std::clamp(w[4] - std::exp(w[5] * (int(rating) - 1)) + 1, 1.0, 10.0);
}

double FsrsScheduler::nextDifficulty(double d, Rating rating) const
{
    const auto& w = m_config.w;
    // Variation amortie près de 10, puis retour léger vers la difficulté d'une première réponse « Facile ».
    const double delta = -w[6] * (int(rating) - 3);
    const double damped = d + delta * (10 - d) / 9;
    const double target = std::clamp(w[4] - std::exp(w[5] * 3) + 1, 1.0, 10.0);
    return std::clamp(w[7] * target + (1 - w[7]) * damped, 1.0, 10.0);
}

double FsrsScheduler::retrievability(double days, double s)
{
    return std::pow(1 + kFactor * std::max(0.0, days) / std::max(0.01, s), kDecay);
}

double FsrsScheduler::recallStability(double d, double s, double r, Rating rating) const
{
    const auto& w = m_config.w;
    const double hardPenalty = rating == Rating::Hard ? w[15] : 1.0;
    const double easyBonus = rating == Rating::Easy ? w[16] : 1.0;
    return s * (std::exp(w[8]) * (11 - d) * std::pow(s, -w[9]) * (std::exp(w[10] * (1 - r)) - 1) * hardPenalty * easyBonus
                + 1);
}

double FsrsScheduler::forgetStability(double d, double s, double r) const
{
    const auto& w = m_config.w;
    const double forget = w[11] * std::pow(d, -w[12]) * (std::pow(s + 1, w[13]) - 1) * std::exp(w[14] * (1 - r));
    // FSRS-5 : jamais plus que ce qu'un oubli le jour même laisserait.
    return std::max(0.1, std::min(forget, s / std::exp(w[17] * w[18])));
}

double FsrsScheduler::shortTermStability(double s, Rating rating) const
{
    const auto& w = m_config.w;
    return std::max(0.1, s * std::exp(w[17] * (int(rating) - 3 + w[18])));
}

double FsrsScheduler::interval(double s) const
{
    const double days = s / kFactor * (std::pow(m_config.retention, 1 / kDecay) - 1);
    return std::clamp(std::round(days), 1.0, double(m_config.maxDays));
}

ScheduleState FsrsScheduler::graduate(ScheduleState s, double days, const QDateTime& now) const
{
    const double d = std::clamp(std::round(days), 1.0, double(m_config.maxDays));
    s.state = CardState::Review;
    s.step = 0;
    s.intervalDays = d;
    s.due = now.addSecs(qint64(d * 86400));
    return s;
}

ScheduleState FsrsScheduler::learn(ScheduleState s, Rating rating, const QDateTime& now, const QList<int>& steps,
                                   bool relearning) const
{
    // Même logique d'étapes que SM-2 ; à la sortie, l'intervalle vient de la stabilité.
    const double good = interval(s.stability);
    if (steps.isEmpty())
        return graduate(s, good, now);
    const int step = std::clamp(s.step, 0, int(steps.size()) - 1);
    s.state = relearning ? CardState::Relearning : CardState::Learning;
    switch (rating) {
    case Rating::Again:
        s.step = 0;
        s.due = now.addSecs(steps.first() * 60);
        return s;
    case Rating::Hard: {
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
        return graduate(s, good, now);
    case Rating::Easy:
        return graduate(s, good, now);
    }
    return s;
}

ScheduleState FsrsScheduler::next(const ScheduleState& current, Rating rating, const QDateTime& nowUtc) const
{
    ScheduleState s = current;
    ++s.reps;
    const bool known = current.stability > 0 && current.difficulty > 0;
    switch (current.state) {
    case CardState::New:
        s.step = 0;
        s.stability = initialStability(rating);
        s.difficulty = initialDifficulty(rating);
        return learn(s, rating, nowUtc, m_config.learningMinutes, false);
    case CardState::Learning:
    case CardState::Relearning:
        if (known) {
            s.stability = shortTermStability(current.stability, rating);
            s.difficulty = nextDifficulty(current.difficulty, rating);
        } else {
            // Carte commencée sous SM-2 : état initial d'après cette réponse.
            s.stability = initialStability(rating);
            s.difficulty = initialDifficulty(rating);
        }
        return learn(s, rating, nowUtc, current.state == CardState::Learning ? m_config.learningMinutes
                                                                              : m_config.relearningMinutes,
                     current.state == CardState::Relearning);
    case CardState::Review:
        break;
    }

    // Carte révisée sous SM-2 : stabilité prise égale à l'intervalle (rappel de 90 % à l'échéance), difficulté
    // déduite de la facilité (2,5 → 6 ; 1,3 → 8,4).
    const double stability = known ? current.stability : std::max(0.5, current.intervalDays);
    const double difficulty = known ? current.difficulty : std::clamp(11 - 2 * current.ease, 1.0, 10.0);
    // Jours écoulés depuis la dernière révision : l'échéance est la dernière révision plus l'intervalle.
    const double elapsed =
        std::max(0.0, current.intervalDays + (current.due.isValid() ? current.due.secsTo(nowUtc) / 86400.0 : 0.0));
    const double r = retrievability(elapsed, stability);
    s.difficulty = nextDifficulty(difficulty, rating);
    if (rating == Rating::Again) {
        ++s.lapses;
        s.stability = forgetStability(difficulty, stability, r);
        s.intervalDays = interval(s.stability);
        s.step = 0;
        if (m_config.relearningMinutes.isEmpty())
            return graduate(s, s.intervalDays, nowUtc);
        s.state = CardState::Relearning;
        s.due = nowUtc.addSecs(m_config.relearningMinutes.first() * 60);
        return s;
    }
    const auto stabilityFor = [&](Rating g) {
        return elapsed < 1 ? shortTermStability(stability, g) : recallStability(difficulty, stability, r, g);
    };
    // Ordre garanti entre les réponses : Difficile ≤ Bien < Facile, et Bien au moins un jour de plus que Difficile.
    double hard = interval(stabilityFor(Rating::Hard));
    double good = interval(stabilityFor(Rating::Good));
    double easy = interval(stabilityFor(Rating::Easy));
    hard = std::min(hard, good);
    good = std::max(good, hard + 1);
    easy = std::max(easy, good + 1);
    s.stability = stabilityFor(rating);
    switch (rating) {
    case Rating::Hard:
        return graduate(s, hard, nowUtc);
    case Rating::Good:
        return graduate(s, good, nowUtc);
    case Rating::Easy:
        return graduate(s, easy, nowUtc);
    case Rating::Again:
        break;
    }
    return s;
}

}  // namespace studylab
