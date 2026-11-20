#pragma once

#include "core/Result.h"
#include "data/repositories/ActivityRepository.h"

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QSet>

#include <functional>

namespace studylab {

namespace db {
class Database;
}

/// Ce qui a été fait un jour donné (journée de 4 h à 4 h, comme les révisions).
struct DayActivity {
    QDate day;
    int seconds = 0;      ///< temps de lecture actif
    int pages = 0;        ///< pages distinctes vues (par document)
    int annotations = 0;
    int comments = 0;
    int notes = 0;        ///< notes créées
    int reviews = 0;      ///< cartes révisées
    int imported = 0;
    bool any() const { return seconds > 0 || pages > 0 || annotations + comments + notes + reviews + imported > 0; }
};

/// Totaux d'une période.
struct PeriodTotals {
    int seconds = 0;
    int pages = 0;
    int annotations = 0;
    int comments = 0;
    int notes = 0;
    int reviews = 0;
    int reviewsPassed = 0;  ///< réponses autres que « À revoir »
    int imported = 0;
    int activeDays = 0;
    int documents = 0;      ///< documents lus
};

/// Objectifs généraux ; 0 = pas d'objectif.
struct Goals {
    int pagesPerWeek = 0;
    int minutesPerDay = 0;
    int reviewsPerDay = 0;
    bool any() const { return pagesPerWeek > 0 || minutesPerDay > 0 || reviewsPerDay > 0; }
};

/// Semaine en cours (du lundi à aujourd'hui) comparée à la même période de la semaine précédente.
struct WeekProgress {
    QDate monday;
    QDate today;
    int pages = 0;
    int seconds = 0;
    int previousPages = 0;
    int previousSeconds = 0;
    int todaySeconds = 0;
    int todayReviews = 0;
    Goals goals;
};

/// Entrée du journal, regroupée : une lecture par document et par jour, les annotations d'un document, les
/// révisions du jour…
struct JournalEntry {
    enum class Kind { Reading, Opened, Annotations, Comments, Note, Reviews, Imported };
    Kind kind = Kind::Reading;
    QDateTime at;          ///< dernière action du groupe (heure locale)
    QDate day;
    qint64 documentId = 0; ///< document lu, annoté, commenté ou importé (un seul pour Imported si count = 1)
    qint64 noteId = 0;
    qint64 itemId = 0;     ///< dernière annotation ou dernier commentaire du groupe
    QString title;         ///< titre du document ou de la note
    int count = 1;
    int seconds = 0;
    int pages = 0;
    int passed = 0;        ///< révisions réussies
};

/// Journal d'activité, temps de lecture et objectifs (07-versions V2-4).
///
/// Les actions (ouverture, import, annotation, commentaire, note, révision) sont écrites dans le journal par les
/// services qui les font ; ce service y ajoute les séances de lecture (temps actif et pages vues), les agrège par
/// jour et gère les objectifs. La journée commence à 4 h (heure locale), comme celle des révisions.
class ActivityService : public QObject {
    Q_OBJECT
public:
    using Clock = std::function<QDateTime()>;
    explicit ActivityService(db::Database& db, QObject* parent = nullptr);

    void setClock(Clock clock) { m_clock = std::move(clock); }
    QDateTime now() const;
    /// Jour d'activité (4 h → 4 h) d'un instant UTC ; début d'un jour d'activité, en UTC.
    static QDate activityDay(const QDateTime& utc);
    static QDateTime dayStartUtc(const QDate& day);
    QDate today() const { return activityDay(now()); }

    // Séances de lecture
    /// Enregistre ou prolonge une séance : `session.id` est renseigné à la première écriture.
    Result<void> saveSession(db::ReadingSessionRow& session);

    // Objectifs
    Result<Goals> goals();
    Result<void> setGoals(const Goals& goals);

    // Agrégats
    /// Un élément par jour, de `from` à `to` inclus.
    Result<QList<DayActivity>> days(const QDate& from, const QDate& to);
    Result<PeriodTotals> totals(const QDate& from, const QDate& to);
    Result<WeekProgress> week();
    /// Jours consécutifs avec de l'activité jusqu'à aujourd'hui (ou hier si rien encore aujourd'hui).
    Result<int> streak();
    /// Journal groupé des jours `from` à `to`, du plus récent au plus ancien.
    Result<QList<JournalEntry>> journal(const QDate& from, const QDate& to);

signals:
    /// Séance de lecture enregistrée ou objectifs modifiés.
    void changed();

private:
    QString iso(const QDateTime& utc) const;

    db::Database& m_db;
    db::ActivityRepository m_repo;
    Clock m_clock;
};

/// « 45 min », « 1 h 05 », « 12 h » ; « 0 min » pour moins d'une minute.
QString formatDuration(int seconds);

}  // namespace studylab
