set(PDFEDITOR_PDF4QT_SOURCE_DIR "${PROJECT_SOURCE_DIR}/.tools/PDF4QT" CACHE PATH "Pinned PDF4QT checkout")
if(NOT EXISTS "${PDFEDITOR_PDF4QT_SOURCE_DIR}/Pdf4QtLibCore/CMakeLists.txt")
    message(FATAL_ERROR "PDF4QT is missing. Run scripts/setup-dependencies.ps1 first.")
endif()

file(READ "${PDFEDITOR_PDF4QT_SOURCE_DIR}/Pdf4QtLibCore/sources/pdfnametreeloader.h" pdfeditor_name_tree_header)
if(NOT pdfeditor_name_tree_header MATCHES "PDF Editor: bound name-tree traversal before catalog initialization")
    message(FATAL_ERROR "PDF4QT name-tree protection is missing. Run scripts/setup-dependencies.ps1 first.")
endif()

# Use upstream's core library, not its editor or installers.
set(PDF4QT_BUILD_ONLY_CORE_LIBRARY ON CACHE BOOL "" FORCE)
set(PDF4QT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PDF4QT_INSTALL_INCLUDE OFF CACHE BOOL "" FORCE)
set(PDF4QT_INSTALL_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(PDF4QT_INSTALL_QT_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(PDF4QT_INSTALL_TO_USR OFF CACHE BOOL "" FORCE)
add_subdirectory("${PDFEDITOR_PDF4QT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third_party/pdf4qt" EXCLUDE_FROM_ALL)
# Upstream's core searches the top-level binary directory for generated headers.
configure_file("${CMAKE_BINARY_DIR}/third_party/pdf4qt/config.h" "${CMAKE_BINARY_DIR}/config.h" COPYONLY)
set_target_properties(Pdf4QtLibCore PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(MSVC)
    target_compile_options(Pdf4QtLibCore PRIVATE /utf-8 /MP2)
endif()
