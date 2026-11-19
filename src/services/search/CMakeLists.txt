add_library(studylab_search STATIC
    SearchService.h SearchService.cpp)
target_include_directories(studylab_search PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_search PUBLIC studylab_data)
studylab_set_warnings(studylab_search)
