add_library(studylab_export STATIC
    MarkdownExporter.h MarkdownExporter.cpp
    ChapterIndex.h ChapterIndex.cpp
    LibraryExporter.h LibraryExporter.cpp)
target_include_directories(studylab_export PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_export
    PUBLIC studylab_core
    PRIVATE studylab_epub studylab_library studylab_annotations studylab_comments Qt6::Pdf)
studylab_set_warnings(studylab_export)
