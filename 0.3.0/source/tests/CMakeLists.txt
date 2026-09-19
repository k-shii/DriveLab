function(add_drivelab_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE DriveLab::Core ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_drivelab_test(drivelab_test_native_ownership_io test_native_ownership_io.cpp)
target_sources(drivelab_test_native_ownership_io PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/linux/native_ownership_io.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_parsers.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_ownership_source.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/proxmox_ownership.cpp)
# Select a project-named fake API only for this test; never shadow POSIX headers.
target_compile_definitions(drivelab_test_native_ownership_io PRIVATE
    DRIVELAB_TEST_OWNERSHIP_IO=1)
target_include_directories(drivelab_test_native_ownership_io PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_drivelab_test(drivelab_test_native_zfs_ownership test_native_zfs_ownership.cpp)
target_sources(drivelab_test_native_zfs_ownership PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/linux/native_zfs_ownership.cpp)
target_include_directories(drivelab_test_native_zfs_ownership BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes/zfs)
target_compile_definitions(drivelab_test_native_zfs_ownership PRIVATE DRIVELAB_HAVE_ZFS_OWNERSHIP=1)

add_drivelab_test(drivelab_test_native_fresh_signatures test_native_fresh_signatures.cpp)
target_sources(drivelab_test_native_fresh_signatures PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/linux/native_fresh_signatures.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_ownership_source.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/proxmox_ownership.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_parsers.cpp)
target_compile_definitions(drivelab_test_native_fresh_signatures PRIVATE DRIVELAB_TEST_FRESH_PROBE=1)
target_include_directories(drivelab_test_native_fresh_signatures PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The real development smoke formatter runs only against injected sources.
add_drivelab_test(drivelab_test_host_usage_smoke test_host_usage_smoke.cpp)
target_sources(drivelab_test_host_usage_smoke PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_inventory_provider.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_usage.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_parsers.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_ownership_source.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/proxmox_ownership.cpp)

add_drivelab_test(drivelab_test_production_application test_production_application.cpp
    DriveLab::ProductionReadModel)
target_sources(drivelab_test_production_application PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_production_source.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_inventory_provider.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_usage.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_parsers.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_ownership_source.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/proxmox_ownership.cpp)

# Real production bootstrap/renderer with a private terminal and platform double.
add_drivelab_test(drivelab_test_production_ui test_production_ui.cpp
    DriveLab::ProductionReadModel DriveLab::DemoReadModel)
target_include_directories(drivelab_test_production_ui BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes/tui)
target_compile_definitions(drivelab_test_production_ui PRIVATE
    DRIVELAB_NATIVE_LINUX=1 DRIVELAB_TEST_TERMINAL=1)
target_sources(drivelab_test_production_ui PRIVATE
    ${PROJECT_SOURCE_DIR}/prototype/demo_ui.cpp
    ${PROJECT_SOURCE_DIR}/src/app/native_application.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/production_ui.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_production_source.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_inventory_provider.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_usage.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_parsers.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_ownership_source.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/proxmox_ownership.cpp)

# C graph/source fixtures inject all host observations.
foreach(name ownership_graph linux_ownership_source proxmox_ownership)
    add_drivelab_test(drivelab_test_${name} test_${name}.cpp)
    target_sources(drivelab_test_${name} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_parsers.cpp
        ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_ownership_source.cpp
        ${PROJECT_SOURCE_DIR}/src/platform/linux/proxmox_ownership.cpp)
endforeach()

# Native proc/stat adapter with a private syscall ABI: never accesses the host.
add_drivelab_test(drivelab_test_proc_host_files test_proc_host_files.cpp)
target_sources(drivelab_test_proc_host_files PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/linux/proc_host_files.cpp)
target_include_directories(drivelab_test_proc_host_files BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes/proc)

# Portable identity and injected Linux host-state tests run without host access.
add_drivelab_test(drivelab_test_identity_resolution test_identity_resolution.cpp)
add_drivelab_test(drivelab_test_linux_host_usage test_linux_host_usage.cpp)
target_sources(drivelab_test_linux_host_usage PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_usage.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_parsers.cpp)
add_drivelab_test(drivelab_test_udev_signature_source test_udev_signature_source.cpp)
target_sources(drivelab_test_udev_signature_source PRIVATE
    ${PROJECT_SOURCE_DIR}/src/platform/linux/udev_signature_source.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_host_parsers.cpp)
target_include_directories(drivelab_test_udev_signature_source BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes)

add_drivelab_test(drivelab_test_drive_and_providers test_drive_and_providers.cpp)
add_drivelab_test(drivelab_test_demo_session test_demo_session.cpp)
add_drivelab_test(drivelab_test_demo_read_model
    test_demo_read_model.cpp
    DriveLab::DemoReadModel)
add_drivelab_test(drivelab_test_job_manager test_job_manager.cpp)
add_drivelab_test(drivelab_test_modes_and_interfaces test_modes_and_interfaces.cpp)
add_drivelab_test(drivelab_test_frontend_boundary test_frontend_boundary.cpp)
target_compile_definitions(drivelab_test_frontend_boundary PRIVATE
    DRIVELAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

if(TARGET drivelab_linux_inventory)
    add_drivelab_test(
        drivelab_test_linux_inventory_provider
        test_linux_inventory_provider.cpp
        DriveLab::LinuxInventory)

else()
    add_drivelab_test(drivelab_test_linux_inventory_provider test_linux_inventory_provider.cpp)
    target_sources(drivelab_test_linux_inventory_provider PRIVATE
        ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_inventory_provider.cpp)
endif()

# The production target uses system libudev; this private test uses a fake.
    add_drivelab_test(drivelab_test_udev_sysfs_inventory_source
        test_udev_sysfs_inventory_source.cpp)
    target_sources(drivelab_test_udev_sysfs_inventory_source PRIVATE
        ${PROJECT_SOURCE_DIR}/src/platform/linux/linux_inventory_provider.cpp
        ${PROJECT_SOURCE_DIR}/src/platform/linux/udev_sysfs_inventory_source.cpp)
    target_include_directories(drivelab_test_udev_sysfs_inventory_source BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/fakes)

if(UNIX)
    add_drivelab_test(drivelab_test_posix_process_runner test_posix_process_runner.cpp)
endif()
