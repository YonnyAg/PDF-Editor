qt_add_library(pdf_editor_core STATIC
    core/PdfDocument.cpp
    core/PdfDocument.h
    core/OcrEngine.cpp
    core/OcrEngine.h
    session/SessionStore.cpp
    session/SessionStore.h
)
target_include_directories(pdf_editor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pdf_editor_core PUBLIC Qt6::Core Qt6::Gui PRIVATE Pdf4QtLibCore Qt6::Xml)
pdf_editor_enable_warnings(pdf_editor_core)
if(WIN32)
    target_link_libraries(pdf_editor_core PRIVATE windowsapp)
endif()

qt_add_library(pdf_editor_ui STATIC
    ui/CropDialog.cpp
    ui/CropDialog.h
    ui/InkDialog.cpp
    ui/InkDialog.h
    ui/OcrDialog.cpp
    ui/OcrDialog.h
    ui/RedactionDialog.cpp
    ui/RedactionDialog.h
    ui/ContentDialog.cpp
    ui/ContentDialog.h
    ui/AccessibleReaderDialog.cpp
    ui/AccessibleReaderDialog.h
    ui/DocumentPrint.cpp
    ui/DocumentPrint.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/WorkspaceWindow.cpp
    ui/WorkspaceWindow.h
    ui/PageView.cpp
    ui/PageView.h
    ui/SearchBar.cpp
    ui/SearchBar.h
    ui/ThumbnailView.cpp
    ui/ThumbnailView.h
)
target_include_directories(pdf_editor_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pdf_editor_ui PUBLIC Qt6::Widgets pdf_editor_core PRIVATE Qt6::Concurrent Qt6::PrintSupport)
pdf_editor_enable_warnings(pdf_editor_ui)

qt_add_executable(pdf_editor WIN32 app/main.cpp)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../packaging/release.json" PDF_EDITOR_RELEASE)
string(JSON PDF_EDITOR_VERSION GET "${PDF_EDITOR_RELEASE}" version)
string(JSON PDF_EDITOR_WIN_VERSION GET "${PDF_EDITOR_RELEASE}" windowsVersion)
string(REPLACE "." "," PDF_EDITOR_WIN_VERSION "${PDF_EDITOR_WIN_VERSION}")
target_compile_definitions(pdf_editor PRIVATE PDF_EDITOR_VERSION="${PDF_EDITOR_VERSION}")
set(PDF_EDITOR_BRANDING "${CMAKE_CURRENT_SOURCE_DIR}/../resources/branding")
set(PDF_EDITOR_ICON "${PDF_EDITOR_BRANDING}/pdf-editor.ico")
if(WIN32)
    configure_file(../packaging/windows/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(pdf_editor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()
set(PDF_EDITOR_ICON_FILES)
foreach(size 16 20 24 32 40 48 64 96 128 256)
    list(APPEND PDF_EDITOR_ICON_FILES "${PDF_EDITOR_BRANDING}/pdf-editor-${size}.png")
endforeach()
qt_add_resources(pdf_editor "branding" PREFIX "/branding" BASE "${PDF_EDITOR_BRANDING}" FILES ${PDF_EDITOR_ICON_FILES})
target_link_libraries(pdf_editor PRIVATE pdf_editor_ui)
pdf_editor_enable_warnings(pdf_editor)
