add_executable(drivelab_inventory_smoke linux_inventory_smoke.cpp)
target_link_libraries(drivelab_inventory_smoke PRIVATE DriveLab::LinuxInventory)
target_compile_features(drivelab_inventory_smoke PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(drivelab_inventory_smoke PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(drivelab_host_usage_smoke linux_host_usage_smoke.cpp)
target_link_libraries(drivelab_host_usage_smoke PRIVATE DriveLab::LinuxInventory)
target_compile_features(drivelab_host_usage_smoke PRIVATE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(drivelab_host_usage_smoke PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(drivelab_production_profile production_profile.cpp)
target_link_libraries(drivelab_production_profile PRIVATE DriveLab::NativeApplication)
target_compile_features(drivelab_production_profile PRIVATE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(drivelab_production_profile PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Development-only: no install rule and no CTest registration. Only an explicit
# invocation of this optional executable may enumerate the current host.
