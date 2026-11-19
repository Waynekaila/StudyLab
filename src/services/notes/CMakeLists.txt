add_library(studylab_notes STATIC
    NoteService.h NoteService.cpp)
target_include_directories(studylab_notes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_notes PUBLIC studylab_data)
studylab_set_warnings(studylab_notes)
