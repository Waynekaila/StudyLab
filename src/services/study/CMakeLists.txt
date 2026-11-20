add_library(studylab_study STATIC
    Scheduler.h Scheduler.cpp
    StudyService.h StudyService.cpp)
target_include_directories(studylab_study PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_study PUBLIC studylab_data)
studylab_set_warnings(studylab_study)
