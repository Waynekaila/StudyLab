#pragma once

#include "core/domain/Card.h"

#include <QDateTime>
#include <QList>
#include <QString>

namespace studylab {

/// Planificateur de répétition espacée (02-architecture : interface IScheduler, SM-2 puis FSRS).
class Scheduler {
public:
    virtual ~Scheduler() = default;
    /// « sm2 », « fsrs » (colonne decks.scheduler).
    virtual QString key() const = 0;
    /// État après la réponse `rating` donnée à `nowUtc`.
    virtual ScheduleState next(const ScheduleState& current, Rating rating, const QDateTime& nowUtc) const = 0;

    /// Délai jusqu'à la prochaine présentation pour chacune des quatre réponses, en secondes (boutons).
    QList<qint64> previewSeconds(const ScheduleState& current, const QDateTime& nowUtc) const;
};

/// SM-2 tel que l'emploient la plupart des logiciels de cartes : étapes d'apprentissage en minutes, puis
/// intervalles en jours multipliés par la facilité (2,5 au départ, 1,3 au minimum). Aucun tirage aléatoire :
/// la même suite de réponses donne toujours les mêmes dates (tests, reproductibilité).
class Sm2Scheduler : public Scheduler {
public:
    struct Config {
        QList<int> learningMinutes{1, 10};
        QList<int> relearningMinutes{10};
        int graduatingDays = 1;
        int easyDays = 4;
        double hardFactor = 1.2;
        double easyBonus = 1.3;
        double minEase = 1.3;
        int maxDays = 36500;
    };

    Sm2Scheduler() = default;
    explicit Sm2Scheduler(Config config) : m_config(std::move(config)) {}

    QString key() const override { return QStringLiteral("sm2"); }
    ScheduleState next(const ScheduleState& current, Rating rating, const QDateTime& nowUtc) const override;
    const Config& config() const { return m_config; }

private:
    ScheduleState learn(ScheduleState s, Rating rating, const QDateTime& now, const QList<int>& steps,
                        bool relearning) const;
    ScheduleState graduate(ScheduleState s, double days, const QDateTime& now) const;

    Config m_config;
};

}  // namespace studylab
