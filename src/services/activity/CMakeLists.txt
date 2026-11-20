add_library(studylab_activity STATIC
    ActivityService.h ActivityService.cpp)
target_include_directories(studylab_activity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_activity PUBLIC studylab_data)
studylab_set_warnings(studylab_activity)
