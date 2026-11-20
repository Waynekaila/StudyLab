#pragma once

#include "core/Result.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace studylab::db {

class Database;

/// Ligne du journal d'activité (table activity).
struct ActivityRow {
    qint64 id = 0;
    QString at;          ///< ISO-8601 UTC
    QString action;      ///< opened, annotated, commented, note_created, reviewed, imported
    QString entityType;  ///< document, annotation, comment, note, card
    qint64 entityId = 0;
    QJsonObject detail;  ///< ex. {"document": 12} pour une annotation, {"rating": 3} pour une révision
};

/// Séance de lecture (table reading_sessions) : temps actif et pages vues d'un document.
struct ReadingSessionRow {
    qint64 id = 0;
    qint64 documentId = 0;
    QString startedAt;
    QString endedAt;
    int seconds = 0;
    QList<int> pages;    ///< pages (PDF) ou pages équivalentes (EPUB, texte) vues, sans doublon
};

/// Objectifs généraux (table goals, sans matière) : 0 = pas d'objectif.
struct GoalRow {
    QString kind;        ///< pages_per_week, minutes_per_day, reviews_per_day, documents_finished
    int target = 0;
};

/// Journal d'activité, séances de lecture et objectifs.
class ActivityRepository {
public:
    explicit ActivityRepository(Database& db);

    /// Ajoute une entrée au journal, horodatée maintenant si `atIso` est vide.
    Result<void> log(const QString& action, const QString& entityType, qint64 entityId,
                     const QJsonObject& detail = {}, const QString& atIso = {});
    /// Entrées entre deux instants (ISO UTC, fin exclue), des plus récentes aux plus anciennes.
    Result<QList<ActivityRow>> entries(const QString& fromIso, const QString& toIso, int limit = -1);

    /// Crée la séance si son id est 0 (et le renseigne), sinon met à jour fin, durée et pages.
    Result<void> saveSession(ReadingSessionRow& session);
    /// Séances commencées entre deux instants, des plus anciennes aux plus récentes.
    Result<QList<ReadingSessionRow>> sessions(const QString& fromIso, const QString& toIso);

    Result<QList<GoalRow>> goals();
    /// Fixe un objectif général ; 0 le désactive.
    Result<void> setGoal(const QString& kind, int target);

    /// Titres des documents et des notes (entrées du journal).
    Result<QHash<qint64, QString>> documentTitles(const QList<qint64>& ids);
    Result<QHash<qint64, QString>> noteTitles(const QList<qint64>& ids);

private:
    Result<QHash<qint64, QString>> titles(const QString& table, const QList<qint64>& ids);

    Database& m_db;
};

}  // namespace studylab::db
