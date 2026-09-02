cmake_minimum_required(VERSION 3.16)

project(DriveLab VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(GNUInstallDirs)

set(DRIVELAB_TUI_DEFAULT OFF)
if(UNIX)
    set(DRIVELAB_TUI_DEFAULT ON)
endif()

option(DRIVELAB_BUILD_TUI "Build the bundled ncursesw TUI" ${DRIVELAB_TUI_DEFAULT})

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
