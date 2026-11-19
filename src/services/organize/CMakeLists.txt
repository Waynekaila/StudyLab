add_library(studylab_organize STATIC
    OrganizeService.h OrganizeService.cpp)
target_include_directories(studylab_organize PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_organize PUBLIC studylab_data)
studylab_set_warnings(studylab_organize)
