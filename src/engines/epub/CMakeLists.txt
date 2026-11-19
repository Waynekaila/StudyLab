# QZipReader est une classe privée de Qt (stable depuis Qt 4, utilisée par Qt pour les .docx/.odt) :
# elle évite d'ajouter libzip pour l'itération 2. Écart documenté dans docs/design/02-architecture.md.
add_library(studylab_epub STATIC
    EpubPackage.h EpubPackage.cpp)
target_link_libraries(studylab_epub
    PUBLIC studylab_engines_document Qt6::Gui
    PRIVATE Qt6::CorePrivate Qt6::GuiPrivate)
studylab_set_warnings(studylab_epub)
