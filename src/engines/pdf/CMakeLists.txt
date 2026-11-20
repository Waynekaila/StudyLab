add_library(studylab_pdf STATIC
    PdfMetadataReader.h PdfMetadataReader.cpp
    PdfObjects.h PdfObjects.cpp
    PdfAnnotationIO.h PdfAnnotationIO.cpp
    ImagePages.h ImagePages.cpp
    PdfRenderQueue.h PdfRenderQueue.cpp
    PdfTextExtractor.h PdfTextExtractor.cpp)
target_link_libraries(studylab_pdf PUBLIC studylab_engines_document Qt6::Gui Qt6::Pdf)
studylab_set_warnings(studylab_pdf)
