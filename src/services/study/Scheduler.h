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

/// FSRS-5 (Free Spaced Repetition Scheduler, J. Ye) avec ses 19 paramètres par défaut. Chaque carte a une
/// stabilité S (jours au bout desquels la probabilité de rappel tombe à 90 %) et une difficulté D (1 à 10) ; la
/// prochaine échéance est le jour où le rappel prévu atteint la rétention visée. Étapes d'apprentissage en minutes
/// comme SM-2, aucun tirage aléatoire.
class FsrsScheduler : public Scheduler {
public:
    struct Config {
        QList<int> learningMinutes{1, 10};
        QList<int> relearningMinutes{10};
        double retention = 0.9;
        int maxDays = 36500;
        QList<double> w = defaultWeights();
    };
    static QList<double> defaultWeights();

    FsrsScheduler() = default;
    explicit FsrsScheduler(Config config) : m_config(std::move(config)) {}

    QString key() const override { return QStringLiteral("fsrs"); }
    ScheduleState next(const ScheduleState& current, Rating rating, const QDateTime& nowUtc) const override;
    const Config& config() const { return m_config; }

    // Formules (exposées pour les tests)
    double initialStability(Rating rating) const;
    double initialDifficulty(Rating rating) const;
    double nextDifficulty(double d, Rating rating) const;
    /// Probabilité de rappel après `days` jours pour une stabilité `s`.
    static double retrievability(double days, double s);
    double recallStability(double d, double s, double r, Rating rating) const;
    double forgetStability(double d, double s, double r) const;
    double shortTermStability(double s, Rating rating) const;
    /// Intervalle (jours, arrondi, entre 1 et le maximum) pour la rétention visée.
    double interval(double s) const;

private:
    ScheduleState learn(ScheduleState s, Rating rating, const QDateTime& now, const QList<int>& steps,
                        bool relearning) const;
    ScheduleState graduate(ScheduleState s, double days, const QDateTime& now) const;

    Config m_config;
};

}  // namespace studylab
