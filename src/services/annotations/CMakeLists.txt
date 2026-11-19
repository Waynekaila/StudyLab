add_library(studylab_annotations STATIC
    AnnotationService.h AnnotationService.cpp)
target_include_directories(studylab_annotations PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_annotations PUBLIC studylab_data)
studylab_set_warnings(studylab_annotations)
