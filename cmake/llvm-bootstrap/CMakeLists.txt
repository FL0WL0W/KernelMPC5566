cmake_minimum_required(VERSION 3.30)
project(E200LLVMHost LANGUAGES NONE)
include(FetchContent)

# Share the fork's main source tree between host tools and target runtimes.
find_package(Git REQUIRED)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/../fetch-llvm.cmake" fetch_recipe)
FetchContent_Declare(e200_llvm
    SOURCE_SUBDIR source-only
    DOWNLOAD_COMMAND "${CMAKE_COMMAND}" "-DGIT=${GIT_EXECUTABLE}"
        "-DSOURCE_DIR=<SOURCE_DIR>"
        "-DFETCH_RECIPE_HASH=${fetch_recipe}"
        -P "${CMAKE_CURRENT_LIST_DIR}/../fetch-llvm.cmake"
)
FetchContent_MakeAvailable(e200_llvm)
# FetchContent caches its download step; explicitly refresh existing checkouts.
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DGIT=${GIT_EXECUTABLE}"
        "-DSOURCE_DIR=${e200_llvm_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_LIST_DIR}/../fetch-llvm.cmake"
    COMMAND_ERROR_IS_FATAL ANY
)

# Native host build, separate from the consuming project's cross toolchain.
find_program(E200_HOST_C_COMPILER NAMES clang-18 clang gcc cc REQUIRED)
find_program(E200_HOST_CXX_COMPILER NAMES clang++-18 clang++ g++ c++ REQUIRED)
message(STATUS "LLVM configure/build logs: ${E200_LLVM_ROOT}/llvm-host-*.log")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${e200_llvm_SOURCE_DIR}/llvm"
        -B "${e200_llvm_BINARY_DIR}" -G "Unix Makefiles"
        "-DCMAKE_C_COMPILER:FILEPATH=${E200_HOST_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER:FILEPATH=${E200_HOST_CXX_COMPILER}"
        -DCMAKE_TOOLCHAIN_FILE:FILEPATH=
        -DCMAKE_BUILD_TYPE=Release
        "-DLLVM_ENABLE_PROJECTS=clang;lld"
        -DLLVM_TARGETS_TO_BUILD=PowerPC
        -DLLVM_DEFAULT_TARGET_TRIPLE=powerpc-none-eabi
        -DLLVM_INCLUDE_TESTS=OFF
        -DLLVM_INCLUDE_BENCHMARKS=OFF
        -DLLVM_INCLUDE_EXAMPLES=OFF
        -DCLANG_ENABLE_CIR=OFF
        -DCLANG_ENABLE_STATIC_ANALYZER=OFF
    OUTPUT_FILE "${E200_LLVM_ROOT}/llvm-host-configure.log"
    ERROR_FILE "${E200_LLVM_ROOT}/llvm-host-configure.log"
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${e200_llvm_BINARY_DIR}"
        --parallel "${E200_LLVM_BUILD_JOBS}"
        --target clang lld llvm-ar llvm-ranlib llvm-objcopy llvm-size
    OUTPUT_FILE "${E200_LLVM_ROOT}/llvm-host-build.log"
    ERROR_FILE "${E200_LLVM_ROOT}/llvm-host-build.log"
    COMMAND_ERROR_IS_FATAL ANY
)
