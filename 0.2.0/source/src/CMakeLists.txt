add_library(drivelab_core
    app/core_application.cpp
    app/core_cli.cpp
    app/demo_session.cpp
    app/launch_options.cpp
    config/app_config.cpp
    core/capability.cpp
    core/demo_model.cpp
    core/drive.cpp
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

if(DRIVELAB_BUILD_TUI)
    set(CURSES_NEED_WIDE TRUE)
    find_package(Curses REQUIRED)

    add_executable(drivelab
        ${PROJECT_SOURCE_DIR}/prototype/drivelab.cpp
        ${PROJECT_SOURCE_DIR}/prototype/demo_ui.cpp
    )

    target_include_directories(drivelab PRIVATE ${CURSES_INCLUDE_DIR} ${CURSES_INCLUDE_DIRS})
    target_link_libraries(drivelab PRIVATE DriveLab::DemoReadModel ${CURSES_LIBRARIES})
    target_compile_features(drivelab PRIVATE cxx_std_20)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(drivelab PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(drivelab PRIVATE /W4 /permissive-)
    endif()

    install(TARGETS drivelab RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
endif()
