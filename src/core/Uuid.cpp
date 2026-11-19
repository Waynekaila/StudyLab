#include "core/Uuid.h"

#include <QUuid>

namespace studylab {

QString newUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

}  // namespace studylab
