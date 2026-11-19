cmake_minimum_required(VERSION 3.25)

project(StudyLab
    VERSION 0.1.0
    DESCRIPTION "Lire, annoter, comprendre, relier, apprendre"
    HOMEPAGE_URL "https://github.com/Waynekaila/StudyLab"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(STUDYLAB_BUILD_TESTS "Compiler les tests automatisés" ON)

# Qt 6.8 LTS est la cible (Debian 13, Windows). 6.4 est le minimum accepté pour compiler.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Svg Concurrent Network Pdf WebEngineWidgets WebChannel)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
    find_package(Qt6 REQUIRED COMPONENTS CorePrivate GuiPrivate)  # QZipReader (lecture EPUB)
endif()
qt_standard_project_setup()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(StudyLabCompilerOptions)

add_subdirectory(third_party/sqlite)
add_subdirectory(src)
include(StudyLabPackaging)

if(STUDYLAB_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
