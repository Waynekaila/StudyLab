add_library(studylab_search STATIC
    SearchService.h SearchService.cpp
    TextIndexer.h TextIndexer.cpp
    OcrIndexer.h OcrIndexer.cpp)
target_include_directories(studylab_search PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_search PUBLIC studylab_data studylab_pdf studylab_epub studylab_ocr Qt6::Concurrent)
studylab_set_warnings(studylab_search)
