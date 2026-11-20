add_library(studylab_references STATIC
    ReferenceService.h ReferenceService.cpp)
target_include_directories(studylab_references PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_references PUBLIC studylab_data studylab_notes)
studylab_set_warnings(studylab_references)
