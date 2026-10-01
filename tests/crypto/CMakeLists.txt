cmake_minimum_required(VERSION 3.16)
project(matrix_crypto_regression LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core Sql)
find_path(OLM_INCLUDE_DIR olm/olm.h HINTS ${OLM_ROOT} PATH_SUFFIXES include REQUIRED)
find_library(OLM_LIBRARY NAMES olm HINTS ${OLM_ROOT} PATH_SUFFIXES lib REQUIRED)
add_executable(matrix_crypto_regression
    matrix_crypto_regression.cpp
    ../../src/plugins/matrix/matrixolm.cpp
    ../../src/plugins/matrix/matrixdatabase.cpp)
target_include_directories(matrix_crypto_regression PRIVATE
    ../../src/plugins/matrix ${OLM_INCLUDE_DIR})
target_compile_definitions(matrix_crypto_regression PRIVATE HAVE_OLM=1)
target_link_libraries(matrix_crypto_regression PRIVATE Qt6::Core Qt6::Sql ${OLM_LIBRARY})
enable_testing()
add_test(NAME matrix_crypto_regression COMMAND matrix_crypto_regression)
set_tests_properties(matrix_crypto_regression PROPERTIES TIMEOUT 15)
