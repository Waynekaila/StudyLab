#pragma once

#include "ui/reader/EpubWeb.h"

#include <QtGlobal>

namespace studylab::test {

/// Qt WebEngine (lecteur EPUB, rendu Markdown des commentaires) : le schéma interne se déclare avant
/// la création de QApplication. Les conteneurs de CI s'exécutent en root : le bac à sable de Chromium
/// y est indisponible.
inline const bool kWebEngineReady = [] {
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_DISABLE_SANDBOX"))
        qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
    ui::registerReaderSchemes();
    return true;
}();

}  // namespace studylab::test
