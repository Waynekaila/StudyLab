#pragma once

#include "core/AppPaths.h"

namespace studylab {
class AnnotationService;
class LibraryService;
class SettingsService;
namespace db {
class Database;
}
namespace ui {
class ThemeManager;
}

/// Services partagés transmis aux écrans (composition manuelle, sans singleton).
struct AppContext {
    AppPaths paths;
    db::Database& db;
    SettingsService& settings;
    ui::ThemeManager& theme;
    LibraryService& library;
    AnnotationService& annotations;
};

}  // namespace studylab
