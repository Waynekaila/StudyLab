add_library(studylab_settings STATIC
    SettingsService.h SettingsService.cpp)
target_include_directories(studylab_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_settings PUBLIC studylab_data Qt6::Core)
studylab_set_warnings(studylab_settings)
