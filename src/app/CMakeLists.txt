qt_add_executable(studylab WIN32
    main.cpp
    Bootstrap.h Bootstrap.cpp)
target_link_libraries(studylab PRIVATE studylab_ui)
target_compile_definitions(studylab PRIVATE STUDYLAB_VERSION="${PROJECT_VERSION}")
studylab_set_warnings(studylab)
set_target_properties(studylab PROPERTIES OUTPUT_NAME "studylab")

include(GNUInstallDirs)
install(TARGETS studylab RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
