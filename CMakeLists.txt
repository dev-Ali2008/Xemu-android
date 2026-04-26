# Updated CMakeLists.txt

# Fix REPO_ROOT to navigate up three levels to the actual repository root
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# Remove duplicate QAPI_GEN_DIR assignments
set(QAPI_GEN_DIR ${CMAKE_BINARY_DIR}/qapi)

# Fix hardcoded GLIB_SOURCE_DIR path to use relative path based on REPO_ROOT
set(GLIB_SOURCE_DIR ${REPO_ROOT}/glib)

# Further CMake content here...