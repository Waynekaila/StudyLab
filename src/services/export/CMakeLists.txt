add_library(studylab_export STATIC
    MarkdownExporter.h MarkdownExporter.cpp)
target_include_directories(studylab_export PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_export PUBLIC studylab_core)
studylab_set_warnings(studylab_export)
