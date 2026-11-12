# 1. Update and build the e200 host compiler before CMake enables C/C++/ASM.
set(E200_LLVM_ROOT "${CMAKE_BINARY_DIR}/_deps" CACHE PATH "Shared LLVM source/build directory")
set(E200_LLVM_BUILD_JOBS 16 CACHE STRING "LLVM build jobs")
set(E200_LLVM_BOOTSTRAP ON CACHE BOOL "Build host tools; OFF in nested runtime builds")
get_filename_component(E200_LLVM_ROOT "${E200_LLVM_ROOT}" ABSOLUTE)

list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    E200_LLVM_ROOT E200_LLVM_BUILD_JOBS E200_LLVM_BOOTSTRAP)
get_property(llvm_ready GLOBAL PROPERTY E200_LLVM_READY)
get_property(in_compiler_check GLOBAL PROPERTY IN_TRY_COMPILE)
if(E200_LLVM_BOOTSTRAP AND NOT in_compiler_check AND NOT llvm_ready)
    file(MAKE_DIRECTORY "${E200_LLVM_ROOT}")
    file(LOCK "${E200_LLVM_ROOT}/llvm-bootstrap.lock" GUARD PROCESS TIMEOUT 600)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -S "${CMAKE_CURRENT_LIST_DIR}/cmake/llvm-bootstrap"
            -B "${E200_LLVM_ROOT}/llvm-bootstrap"
            -G "Unix Makefiles"
            -DCMAKE_TOOLCHAIN_FILE:FILEPATH=
            "-DFETCHCONTENT_BASE_DIR:PATH=${E200_LLVM_ROOT}"
            "-DE200_LLVM_ROOT:PATH=${E200_LLVM_ROOT}"
            "-DE200_LLVM_BUILD_JOBS:STRING=${E200_LLVM_BUILD_JOBS}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    set_property(GLOBAL PROPERTY E200_LLVM_READY TRUE)
endif()

# 2. Select only tools from that build.
set(E200_LLVM_SOURCE_DIR "${E200_LLVM_ROOT}/e200_llvm-src")
set(E200_LLVM_BIN_DIR "${E200_LLVM_ROOT}/e200_llvm-build/bin")
set(CMAKE_C_COMPILER "${E200_LLVM_BIN_DIR}/clang")
set(CMAKE_CXX_COMPILER "${E200_LLVM_BIN_DIR}/clang++")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
set(CMAKE_AR "${E200_LLVM_BIN_DIR}/llvm-ar")
set(CMAKE_RANLIB "${E200_LLVM_BIN_DIR}/llvm-ranlib")
set(CMAKE_LINKER "${E200_LLVM_BIN_DIR}/ld.lld")
set(CMAKE_OBJCOPY "${E200_LLVM_BIN_DIR}/llvm-objcopy")
set(CMAKE_SIZE "${E200_LLVM_BIN_DIR}/llvm-size")
foreach(language C CXX ASM)
    set(CMAKE_${language}_COMPILER_TARGET powerpc-none-eabi)
endforeach()
foreach(language C CXX)
    set(CMAKE_${language}_COMPILER_AR "${CMAKE_AR}")
    set(CMAKE_${language}_COMPILER_RANLIB "${CMAKE_RANLIB}")
endforeach()

# 3. Describe bare-metal e200 targets. CPU, optimization, and LTO live in the preset.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_FLAGS_INIT "-mbig-endian -mhard-float -ffreestanding -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fno-exceptions -fno-threadsafe-statics -fno-c++-static-destructors")
set(CMAKE_ASM_FLAGS_INIT "-mbig-endian -mhard-float")
set(CMAKE_EXE_LINKER_FLAGS_INIT "--ld-path=${CMAKE_LINKER} -nostdlib -static")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
