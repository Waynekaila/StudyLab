# SQLite embarqué : même version et mêmes options sur toutes les plateformes.
add_library(studylab_sqlite3 STATIC sqlite3.c)
target_include_directories(studylab_sqlite3 SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(studylab_sqlite3
    PUBLIC
        SQLITE_ENABLE_FTS5
        SQLITE_DQS=0
        SQLITE_DEFAULT_FOREIGN_KEYS=1
        SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
        SQLITE_THREADSAFE=1
        SQLITE_OMIT_LOAD_EXTENSION
        SQLITE_OMIT_DEPRECATED
        SQLITE_LIKE_DOESNT_MATCH_BLOBS
        SQLITE_MAX_EXPR_DEPTH=0
        SQLITE_USE_ALLOCA
        SQLITE_ENABLE_DBSTAT_VTAB)
set_target_properties(studylab_sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(studylab_sqlite3 PRIVATE /W0)
else()
    target_compile_options(studylab_sqlite3 PRIVATE -w)
endif()
find_package(Threads REQUIRED)
target_link_libraries(studylab_sqlite3 PUBLIC Threads::Threads)
