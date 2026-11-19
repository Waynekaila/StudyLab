#pragma once

#include "core/AppPaths.h"

namespace studylab {
class AnnotationService;
class BackupService;
class CommentService;
class LibraryService;
class NoteService;
class SearchService;
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
    CommentService& comments;
    SearchService& search;
    BackupService& backups;
    NoteService& notes;
};

}  // namespace studylab
