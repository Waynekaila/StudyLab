# Tesseract est chargé à l'exécution (QLibrary) : aucune dépendance à la compilation, l'OCR reste facultatif.
add_library(studylab_ocr STATIC
    OcrPage.h OcrPage.cpp
    TesseractOcr.h TesseractOcr.cpp)
target_link_libraries(studylab_ocr PUBLIC studylab_core Qt6::Gui)
studylab_set_warnings(studylab_ocr)
