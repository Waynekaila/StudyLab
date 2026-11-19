add_library(studylab_core STATIC
    Result.h
    Clock.h Clock.cpp
    Uuid.h Uuid.cpp
    Log.h Log.cpp
    AppPaths.h AppPaths.cpp)
target_include_directories(studylab_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(studylab_core PUBLIC Qt6::Core)
studylab_set_warnings(studylab_core)
