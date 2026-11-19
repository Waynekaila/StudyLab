add_library(studylab_data STATIC
    sqlite/Database.h sqlite/Database.cpp
    sqlite/Statement.h sqlite/Statement.cpp
    sqlite/Transaction.h sqlite/Transaction.cpp
    migrations/Migrator.h migrations/Migrator.cpp)
target_link_libraries(studylab_data PUBLIC studylab_core studylab_sqlite3 Qt6::Core)
studylab_set_warnings(studylab_data)

qt_add_resources(studylab_data "migrations"
    PREFIX "/migrations"
    BASE migrations
    FILES
        migrations/0001_initial.sql)
