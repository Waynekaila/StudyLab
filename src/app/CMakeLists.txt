qt_add_executable(studylab WIN32
    main.cpp
    Bootstrap.h Bootstrap.cpp)
if(WIN32)
    set(STUDYLAB_ICO "${PROJECT_SOURCE_DIR}/packaging/windows/studylab.ico")
    configure_file(${PROJECT_SOURCE_DIR}/packaging/windows/studylab.rc.in ${CMAKE_CURRENT_BINARY_DIR}/studylab.rc @ONLY)
    target_sources(studylab PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/studylab.rc)
endif()
# Icône de la fenêtre (barre des tâches, Alt+Tab)
qt_add_resources(studylab "app_icon"
    PREFIX "/app"
    BASE ${PROJECT_SOURCE_DIR}/packaging/icons
    FILES ${PROJECT_SOURCE_DIR}/packaging/icons/studylab.svg)
target_link_libraries(studylab PRIVATE studylab_ui)
target_compile_definitions(studylab PRIVATE STUDYLAB_VERSION="${PROJECT_VERSION}")
studylab_set_warnings(studylab)
set_target_properties(studylab PROPERTIES OUTPUT_NAME "studylab")

include(GNUInstallDirs)
if(WIN32)
    install(TARGETS studylab RUNTIME DESTINATION ".")
else()
    install(TARGETS studylab RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
