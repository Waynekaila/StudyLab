add_library(studylab_ui STATIC
    theme/Tokens.h theme/Tokens.cpp
    theme/Icons.h theme/Icons.cpp
    theme/ThemeManager.h theme/ThemeManager.cpp
    components/ToggleSwitch.h components/ToggleSwitch.cpp
    components/SegmentedControl.h components/SegmentedControl.cpp
    components/FadeStackedWidget.h components/FadeStackedWidget.cpp
    components/Widgets.h components/Widgets.cpp
    components/FlowLayout.h components/FlowLayout.cpp
    components/MarkdownHighlighter.h components/MarkdownHighlighter.cpp
    shell/AppContext.h
    shell/Sidebar.h shell/Sidebar.cpp
    shell/MainWindow.h shell/MainWindow.cpp
    shell/SearchPalette.h shell/SearchPalette.cpp
    shell/InstanceChannel.h shell/InstanceChannel.cpp
    pages/PageScaffold.h pages/PageScaffold.cpp
    pages/HomePage.h pages/HomePage.cpp
    pages/SettingsPage.h pages/SettingsPage.cpp
    library/CoverArt.h library/CoverArt.cpp
    library/LibraryModel.h library/LibraryModel.cpp
    library/LibraryDelegate.h library/LibraryDelegate.cpp
    library/EditDocumentDialog.h library/EditDocumentDialog.cpp
    library/DocumentDetailPanel.h library/DocumentDetailPanel.cpp
    library/LibraryPage.h library/LibraryPage.cpp
    reader/PdfView.h reader/PdfView.cpp
    reader/ThumbnailModel.h reader/ThumbnailModel.cpp
    reader/PdfReaderTab.h reader/PdfReaderTab.cpp
    reader/ReaderPage.h reader/ReaderPage.cpp
    reader/AnnotationStyle.h reader/AnnotationStyle.cpp
    reader/AnnotationPopup.h reader/AnnotationPopup.cpp
    reader/AnnotationsPane.h reader/AnnotationsPane.cpp
    reader/ReaderTab.h reader/ReaderTab.cpp
    reader/EpubWeb.h reader/EpubWeb.cpp
    reader/EpubView.h reader/EpubView.cpp
    reader/EpubReaderTab.h reader/EpubReaderTab.cpp
    reader/MarkdownView.h reader/MarkdownView.cpp
    reader/CommentsPane.h reader/CommentsPane.cpp
    notes/NoteEditor.h notes/NoteEditor.cpp
    notes/NotesPage.h notes/NotesPage.cpp)
target_link_libraries(studylab_ui PUBLIC studylab_library studylab_annotations studylab_comments studylab_notes studylab_search studylab_backup studylab_export studylab_settings studylab_epub studylab_data studylab_core Qt6::Widgets Qt6::Svg Qt6::WebEngineWidgets Qt6::WebChannel Qt6::Network)
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

# Lecteur EPUB : foliate-js (MIT, version figée dans foliate/VERSION) et notre adaptateur ;
# rendu Markdown des commentaires : markdown-it et KaTeX (MIT, versions dans markdown/vendor/VERSION).
file(GLOB_RECURSE STUDYLAB_EPUB_READER RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    resources/epub-reader/*.js resources/epub-reader/*.html resources/epub-reader/*.css
    resources/epub-reader/*.woff2)
qt_add_resources(studylab_ui "epub_reader"
    PREFIX "/epub-reader"
    BASE resources/epub-reader
    FILES ${STUDYLAB_EPUB_READER})
