add_library(studylab_ui STATIC
    theme/Tokens.h theme/Tokens.cpp
    theme/Icons.h theme/Icons.cpp
    theme/ThemeManager.h theme/ThemeManager.cpp
    components/ToggleSwitch.h components/ToggleSwitch.cpp
    components/SegmentedControl.h components/SegmentedControl.cpp
    components/FadeStackedWidget.h components/FadeStackedWidget.cpp
    components/Widgets.h components/Widgets.cpp
    shell/AppContext.h
    shell/Sidebar.h shell/Sidebar.cpp
    shell/MainWindow.h shell/MainWindow.cpp
    pages/PageScaffold.h pages/PageScaffold.cpp
    pages/HomePage.h pages/HomePage.cpp
    pages/SettingsPage.h pages/SettingsPage.cpp)
target_link_libraries(studylab_ui PUBLIC studylab_settings studylab_data studylab_core Qt6::Widgets Qt6::Svg)
studylab_set_warnings(studylab_ui)

file(GLOB STUDYLAB_ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS resources/icons/*.svg)
qt_add_resources(studylab_ui "icons"
    PREFIX "/icons"
    BASE resources/icons
    FILES ${STUDYLAB_ICONS})
qt_add_resources(studylab_ui "fonts"
    PREFIX "/fonts"
    BASE resources/fonts
    FILES
        resources/fonts/Inter-Regular.otf
        resources/fonts/Inter-Medium.otf
        resources/fonts/Inter-SemiBold.otf)
qt_add_resources(studylab_ui "theme"
    PREFIX "/theme"
    BASE resources/theme
    FILES
        resources/theme/chevron-down-light.svg
        resources/theme/chevron-down-dark.svg)
