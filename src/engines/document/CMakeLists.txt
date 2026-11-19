add_library(studylab_engines_document STATIC
    DocumentMetadata.h DocumentMetadata.cpp)
target_link_libraries(studylab_engines_document PUBLIC studylab_core Qt6::Gui)
studylab_set_warnings(studylab_engines_document)
