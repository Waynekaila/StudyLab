add_library(studylab_library STATIC
    FileInspector.h FileInspector.cpp
    ImportWorker.h ImportWorker.cpp
    LibraryService.h LibraryService.cpp)
target_include_directories(studylab_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_library PUBLIC studylab_data studylab_pdf studylab_epub Qt6::Concurrent)
studylab_set_warnings(studylab_library)
