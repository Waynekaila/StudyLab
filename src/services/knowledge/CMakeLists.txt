add_library(studylab_knowledge STATIC
    KnowledgeService.h KnowledgeService.cpp
    ForceLayout.h ForceLayout.cpp)
target_include_directories(studylab_knowledge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_knowledge PUBLIC studylab_data)
studylab_set_warnings(studylab_knowledge)
