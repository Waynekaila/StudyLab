#include "core/Clock.h"

#include <QDateTime>

namespace studylab {

QString nowIsoUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

}  // namespace studylab
