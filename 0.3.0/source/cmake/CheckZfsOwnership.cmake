include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

# Compile AND link the actual adapter: pkg-config alone cannot certify the
# exported headers, transitive C++ declarations, or required library symbols.
function(drivelab_check_zfs_ownership dependency_target result)
    cmake_push_check_state(RESET)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    # The explicit source root also supports the isolated detection fixture.
    set(CMAKE_REQUIRED_INCLUDES "${DRIVELAB_ZFS_SOURCE_ROOT}")
    set(CMAKE_REQUIRED_LIBRARIES "${dependency_target}")
    set(CMAKE_REQUIRED_DEFINITIONS -DDRIVELAB_HAVE_ZFS_OWNERSHIP=1)
    unset(DRIVELAB_ZFS_PUBLIC_INTERFACE_WORKS CACHE)
    check_cxx_source_compiles("
#include \"${DRIVELAB_ZFS_SOURCE_ROOT}/platform/linux/native_zfs_ownership.cpp\"
int main() {
    drivelab::NativeZfsOwnershipSource source;
    return source.read().complete ? 0 : 1;
}
" DRIVELAB_ZFS_PUBLIC_INTERFACE_WORKS)
    set(${result} "${DRIVELAB_ZFS_PUBLIC_INTERFACE_WORKS}" PARENT_SCOPE)
    cmake_pop_check_state()
endfunction()
