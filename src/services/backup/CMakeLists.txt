add_library(studylab_backup STATIC
    BackupService.h BackupService.cpp)
target_include_directories(studylab_backup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_backup PUBLIC studylab_data)
studylab_set_warnings(studylab_backup)
