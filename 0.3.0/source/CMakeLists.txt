cmake_minimum_required(VERSION 3.16)

project(DriveLab VERSION 0.3.0 LANGUAGES CXX)

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

set(DRIVELAB_LINUX_INVENTORY_DEFAULT OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DRIVELAB_LINUX_INVENTORY_DEFAULT ON)
endif()

option(DRIVELAB_BUILD_LINUX_INVENTORY
    "Build the read-only Linux libudev/sysfs inventory provider"
    ${DRIVELAB_LINUX_INVENTORY_DEFAULT})

if(DRIVELAB_BUILD_LINUX_INVENTORY AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "DRIVELAB_BUILD_LINUX_INVENTORY is supported only when targeting Linux")
endif()

option(DRIVELAB_BUILD_INVENTORY_SMOKE
    "Build the manual development-only Linux inventory smoke executables" OFF)

if(DRIVELAB_BUILD_INVENTORY_SMOKE AND NOT DRIVELAB_BUILD_LINUX_INVENTORY)
    message(FATAL_ERROR
        "DRIVELAB_BUILD_INVENTORY_SMOKE requires DRIVELAB_BUILD_LINUX_INVENTORY=ON")
endif()

add_subdirectory(src)

if(DRIVELAB_BUILD_INVENTORY_SMOKE)
    add_subdirectory(tools)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
