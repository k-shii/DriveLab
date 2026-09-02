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

if(UNIX)
    add_drivelab_test(drivelab_test_posix_process_runner test_posix_process_runner.cpp)
endif()
