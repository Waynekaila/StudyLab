add_library(studylab_comments STATIC
    CommentService.h CommentService.cpp)
target_include_directories(studylab_comments PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(studylab_comments PUBLIC studylab_data)
studylab_set_warnings(studylab_comments)
