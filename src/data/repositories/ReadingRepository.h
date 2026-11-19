#pragma once

#include "core/Result.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace studylab::db {

class Database;

/// Dernière position de lecture d'un document (table reading_progress).
struct ReadingPosition {
    QJsonObject locator;  ///< position exacte, ex. {"type":"pdf","page":11,"y":240.5}
    double progress = 0.0;  ///< 0..1
    QJsonObject view;     ///< réglages d'affichage : zoom, rotation, double page…
    QString updatedAt;
};

class ReadingRepository {
public:
    explicit ReadingRepository(Database& db);

    Result<std::optional<ReadingPosition>> position(qint64 documentId);
    Result<void> savePosition(qint64 documentId, const ReadingPosition& position);

private:
    Database& m_db;
};

}  // namespace studylab::db
