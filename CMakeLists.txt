cmake_minimum_required(VERSION 3.25)

project(PdfEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(cmake/CompilerWarnings.cmake)

find_package(Qt6 6.9 REQUIRED COMPONENTS Widgets Concurrent PrintSupport Xml)
if(BUILD_TESTING)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
endif()
qt_standard_project_setup(REQUIRES 6.9)

include(cmake/Pdf4Qt.cmake)

add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
