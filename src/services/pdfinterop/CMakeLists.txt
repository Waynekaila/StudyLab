add_library(studylab_pdfinterop STATIC
    PdfInteropService.h PdfInteropService.cpp)
target_include_directories(studylab_pdfinterop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_pdfinterop PUBLIC studylab_annotations studylab_comments studylab_library studylab_pdf)
studylab_set_warnings(studylab_pdfinterop)
