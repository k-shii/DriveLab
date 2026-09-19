add_library(drivelab_core
    app/core_application.cpp
    app/production_application.cpp
    app/core_cli.cpp
    app/demo_session.cpp
    app/launch_options.cpp
    config/app_config.cpp
    core/capability.cpp
    core/demo_model.cpp
    core/drive.cpp
    core/resolved_inventory.cpp
    core/ownership_graph.cpp
    core/operation.cpp
    jobs/job_manager.cpp
    logging/logger.cpp
    process/disabled_process_runner.cpp
    providers/mock_providers.cpp
    report/report_writer.cpp
    safety/safety_policy.cpp
)

add_library(DriveLab::Core ALIAS drivelab_core)

target_include_directories(drivelab_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(drivelab_core PUBLIC cxx_std_20)
target_compile_definitions(drivelab_core PUBLIC DRIVELAB_VERSION="${PROJECT_VERSION}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(drivelab_core PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(drivelab_core PRIVATE /W4 /permissive-)
endif()

if(UNIX)
    find_package(Threads REQUIRED)
    target_sources(drivelab_core PRIVATE process/posix_process_runner.cpp)
    target_link_libraries(drivelab_core PRIVATE Threads::Threads)
endif()

option(DRIVELAB_ENABLE_ZFS_OWNERSHIP
    "Enable detection of the public OpenZFS interface for live ownership observations" ON)
option(DRIVELAB_REQUIRE_ZFS_OWNERSHIP
    "Require the public OpenZFS interface for live ownership observations" OFF)
if(DRIVELAB_REQUIRE_ZFS_OWNERSHIP AND NOT DRIVELAB_ENABLE_ZFS_OWNERSHIP)
    message(FATAL_ERROR
        "DRIVELAB_REQUIRE_ZFS_OWNERSHIP=ON contradicts DRIVELAB_ENABLE_ZFS_OWNERSHIP=OFF.")
endif()
if(DRIVELAB_REQUIRE_ZFS_OWNERSHIP AND NOT DRIVELAB_BUILD_LINUX_INVENTORY)
    message(FATAL_ERROR "Required ZFS ownership needs DRIVELAB_BUILD_LINUX_INVENTORY=ON.")
endif()

if(DRIVELAB_BUILD_LINUX_INVENTORY)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(DRIVELAB_LIBUDEV QUIET IMPORTED_TARGET libudev)
    endif()

    if(NOT TARGET PkgConfig::DRIVELAB_LIBUDEV)
        find_path(DRIVELAB_LIBUDEV_INCLUDE_DIR NAMES libudev.h)
        find_library(DRIVELAB_LIBUDEV_LIBRARY NAMES udev)
        mark_as_advanced(DRIVELAB_LIBUDEV_INCLUDE_DIR DRIVELAB_LIBUDEV_LIBRARY)
    endif()

    if(NOT TARGET PkgConfig::DRIVELAB_LIBUDEV AND
       (NOT DRIVELAB_LIBUDEV_INCLUDE_DIR OR NOT DRIVELAB_LIBUDEV_LIBRARY))
        message(FATAL_ERROR
            "DriveLab Linux inventory requires libudev development files. "
            "Install libudev-dev on Debian, Ubuntu, or Proxmox, or configure "
            "with -DDRIVELAB_BUILD_LINUX_INVENTORY=OFF for a Core-only build.")
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DRIVELAB_BLKID REQUIRED IMPORTED_TARGET blkid>=2.38)

    add_library(drivelab_linux_inventory
        platform/linux/linux_production_source.cpp
        platform/linux/linux_inventory_provider.cpp
        platform/linux/udev_sysfs_inventory_source.cpp
        platform/linux/linux_host_usage.cpp
        platform/linux/linux_host_parsers.cpp
        platform/linux/proc_host_files.cpp
        platform/linux/udev_signature_source.cpp
        platform/linux/linux_ownership_source.cpp
        platform/linux/proxmox_ownership.cpp
        platform/linux/native_ownership_io.cpp
        platform/linux/native_fresh_signatures.cpp
        platform/linux/native_zfs_ownership.cpp
    )

    set(DRIVELAB_ZFS_USABLE FALSE)
    if(DRIVELAB_ENABLE_ZFS_OWNERSHIP)
        # Certify the real public adapter build, not just package discovery.
        if(PkgConfig_FOUND)
            pkg_check_modules(DRIVELAB_ZFS QUIET IMPORTED_TARGET libzfs)
        endif()
        if(TARGET PkgConfig::DRIVELAB_ZFS)
            include("${PROJECT_SOURCE_DIR}/cmake/CheckZfsOwnership.cmake")
            set(DRIVELAB_ZFS_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
            drivelab_check_zfs_ownership(PkgConfig::DRIVELAB_ZFS DRIVELAB_ZFS_USABLE)
        endif()
    endif()
    if(DRIVELAB_ZFS_USABLE)
        target_compile_definitions(drivelab_linux_inventory PRIVATE DRIVELAB_HAVE_ZFS_OWNERSHIP=1)
        target_link_libraries(drivelab_linux_inventory PRIVATE PkgConfig::DRIVELAB_ZFS)
    elseif(DRIVELAB_REQUIRE_ZFS_OWNERSHIP)
        message(FATAL_ERROR
            "Required ZFS ownership could not compile/link the public libzfs/nvpair adapter. "
            "Check pkg-config libzfs, the exported development headers/libraries, "
            "and CMake configure test diagnostics. No private ZFS header is required.")
    elseif(NOT DRIVELAB_ENABLE_ZFS_OWNERSHIP)
        message(STATUS "Native ZFS ownership explicitly disabled; ZFS evidence will remain unavailable.")
    else()
        message(STATUS "Public OpenZFS ownership interface unavailable; ZFS evidence will remain unavailable.")
    endif()

    add_library(DriveLab::LinuxInventory ALIAS drivelab_linux_inventory)

    target_include_directories(drivelab_linux_inventory
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_features(drivelab_linux_inventory PUBLIC cxx_std_20)
    target_link_libraries(drivelab_linux_inventory PUBLIC DriveLab::Core PRIVATE PkgConfig::DRIVELAB_BLKID)

    if(TARGET PkgConfig::DRIVELAB_LIBUDEV)
        target_link_libraries(drivelab_linux_inventory PRIVATE PkgConfig::DRIVELAB_LIBUDEV)
    else()
        target_include_directories(drivelab_linux_inventory PRIVATE
            ${DRIVELAB_LIBUDEV_INCLUDE_DIR})
        target_link_libraries(drivelab_linux_inventory PRIVATE
            ${DRIVELAB_LIBUDEV_LIBRARY})
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(drivelab_linux_inventory PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(drivelab_linux_inventory PRIVATE /W4 /permissive-)
    endif()
endif()

add_library(drivelab_demo_read_model
    ui/demo_read_model.cpp
)

add_library(DriveLab::DemoReadModel ALIAS drivelab_demo_read_model)

target_include_directories(drivelab_demo_read_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(drivelab_demo_read_model PUBLIC cxx_std_20)
target_link_libraries(drivelab_demo_read_model PUBLIC DriveLab::Core)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(drivelab_demo_read_model PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(drivelab_demo_read_model PRIVATE /W4 /permissive-)
endif()

add_library(drivelab_production_read_model ui/production_read_model.cpp)
add_library(DriveLab::ProductionReadModel ALIAS drivelab_production_read_model)
target_link_libraries(drivelab_production_read_model PUBLIC DriveLab::Core)

add_library(drivelab_native_application app/native_application.cpp)
add_library(DriveLab::NativeApplication ALIAS drivelab_native_application)
target_link_libraries(drivelab_native_application PUBLIC DriveLab::Core)
if(TARGET drivelab_linux_inventory)
    target_compile_definitions(drivelab_native_application PRIVATE DRIVELAB_NATIVE_LINUX=1)
    target_link_libraries(drivelab_native_application PRIVATE DriveLab::LinuxInventory)
endif()
foreach(target drivelab_production_read_model drivelab_native_application)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()
endforeach()

if(DRIVELAB_BUILD_TUI)
    set(CURSES_NEED_WIDE TRUE)
    find_package(Curses REQUIRED)

    add_executable(drivelab
        ${PROJECT_SOURCE_DIR}/prototype/drivelab.cpp
        ${PROJECT_SOURCE_DIR}/prototype/demo_ui.cpp
        ui/production_ui.cpp
    )

    target_include_directories(drivelab PRIVATE ${CURSES_INCLUDE_DIR} ${CURSES_INCLUDE_DIRS})
    target_link_libraries(drivelab PRIVATE DriveLab::DemoReadModel
        DriveLab::ProductionReadModel DriveLab::NativeApplication ${CURSES_LIBRARIES})
    target_compile_features(drivelab PRIVATE cxx_std_20)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(drivelab PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(drivelab PRIVATE /W4 /permissive-)
    endif()

    install(TARGETS drivelab RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
endif()
