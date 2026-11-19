# Installation et paquets : .deb (Debian 13) par CPack, dossier autonome pour l'installateur Windows (Inno Setup).
include(GNUInstallDirs)

set(CPACK_PACKAGE_NAME "studylab")
set(CPACK_PACKAGE_VENDOR "StudyLab")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lire, annoter et commenter des PDF et EPUB, exporter vers Obsidian")
string(CONCAT CPACK_PACKAGE_DESCRIPTION
    "StudyLab est une application de bureau hors ligne pour étudier des PDF et des EPUB : bibliothèque, lecteurs, "
    "annotations en couleur, commentaires Markdown avec formules LaTeX, recherche universelle, sauvegardes locales "
    "et export Markdown compatible Obsidian.")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)

if(WIN32)
    # Dossier autonome : studylab.exe, DLL Qt (windeployqt, en CI) et bibliothèques d'exécution MSVC à côté.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
    set(CMAKE_INSTALL_UCRT_LIBRARIES ON)
    include(InstallRequiredSystemLibraries)
    install(FILES LICENSE THIRD_PARTY_LICENSES.md DESTINATION ".")
    set(CPACK_GENERATOR "ZIP")
elseif(UNIX AND NOT APPLE)
    install(FILES packaging/linux/studylab.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/studylab.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
            RENAME io.github.waynekaila.StudyLab.metainfo.xml)
    install(FILES packaging/icons/studylab.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES packaging/icons/studylab-256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME studylab.png)
    install(FILES packaging/icons/studylab-48.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps
            RENAME studylab.png)
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/studylab RENAME copyright)
    install(FILES THIRD_PARTY_LICENSES.md README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/studylab)

    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Wayne <100537693+Waynekaila@users.noreply.github.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "education")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)  # bibliothèques Qt, SQLite embarqué
    # Greffons chargés à l'exécution (non vus par dpkg-shlibdeps) : plateformes, SVG, processus WebEngine.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins, qt6-svg-plugins | libqt6svg6, libqt6webenginecore6-bin")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland, xdg-utils")
endif()

include(CPack)
