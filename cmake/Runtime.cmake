include(FetchContent)
include(ExternalProject)

# Picolibc's CMake port generates its headers during configuration. Keep its
# project variables/options scoped away from the firmware configuration.
function(configure_picolibc)
    set(TESTS OFF)
    set(__SINGLE_THREAD ON)
    set(__THREAD_LOCAL_STORAGE OFF)
    set(__GLOBAL_ERRNO ON)
    set(__NANO_MALLOC ON)
    set(PICOLIBC_EXTRA_COMPILE_OPTIONS
        "-I${CMAKE_CURRENT_FUNCTION_LIST_DIR}/runtime/include")
    FetchContent_Declare(picolibc
        EXCLUDE_FROM_ALL
        URL https://codeload.github.com/picolibc/picolibc/tar.gz/refs/tags/1.8.10
        URL_HASH SHA256=d1fc2a20f217472f8d9da084a519a25678e8d91852a41c7f3f1840d3508f7163
    )
    FetchContent_MakeAvailable(picolibc)
    # Clang accepts the PowerPC return instruction without its prediction suffix.
    set(setjmp "${picolibc_SOURCE_DIR}/newlib/libc/machine/powerpc/setjmp.S")
    file(READ "${setjmp}" original)
    string(REPLACE "bclr+" "bclr" patched "${original}")
    if(NOT original STREQUAL patched)
        file(WRITE "${setjmp}" "${patched}")
    endif()
    set(picolibc_headers "${picolibc_BINARY_DIR}/picolibc" PARENT_SCOPE)
endfunction()
configure_picolibc()

# Use the last -mcpu option, just as Clang does. Configuration flags follow
# the common C flags on the compiler command line.
string(TOUPPER "${CMAKE_BUILD_TYPE}" runtime_config)
separate_arguments(runtime_c_flags NATIVE_COMMAND
    "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${runtime_config}}")
foreach(flag IN LISTS runtime_c_flags)
    if(flag MATCHES "^-mcpu=(.+)$")
        set(runtime_cpu "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(NOT runtime_cpu)
    message(FATAL_ERROR "Set -mcpu=<e200 core> in the C compiler flags to select the target runtimes.")
endif()

# Always build the target runtimes from the same sources as the compiler.
set(runtime_args
    "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_CURRENT_LIST_DIR}/../toolchain.cmake"
    -DE200_LLVM_BOOTSTRAP:BOOL=OFF
    "-DE200_LLVM_ROOT:PATH=${E200_LLVM_ROOT}"
    "-DCMAKE_SYSROOT:PATH=${picolibc_headers}"
    -DCMAKE_BUILD_TYPE:STRING=MinSizeRel
)
# Forward the effective CPU; runtimes keep their own configuration and LTO policy.
foreach(language C CXX ASM)
    list(APPEND runtime_args
        "-DCMAKE_${language}_FLAGS:STRING=${CMAKE_${language}_FLAGS} -mcpu=${runtime_cpu}")
endforeach()
add_library(e200_builtins STATIC IMPORTED GLOBAL)
# Keep runtime archives and generated headers separate for each target CPU.
set(builtins_binary "${CMAKE_BINARY_DIR}/_deps/compiler-rt-${runtime_cpu}-build")
set(compiler_rt_archive "${builtins_binary}/lib/baremetal/libclang_rt.builtins-powerpc.a")
ExternalProject_Add(e200_compiler_rt
    PREFIX "${CMAKE_BINARY_DIR}/_deps/compiler-rt-${runtime_cpu}"
    SOURCE_DIR "${E200_LLVM_SOURCE_DIR}/compiler-rt/lib/builtins"
    BINARY_DIR "${builtins_binary}"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
    CMAKE_ARGS ${runtime_args}
        -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON
        -DCOMPILER_RT_BAREMETAL_BUILD=ON
        -DCOMPILER_RT_OS_DIR=baremetal
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS "${compiler_rt_archive}"
    EXCLUDE_FROM_ALL TRUE
)
set_target_properties(e200_builtins PROPERTIES IMPORTED_LOCATION "${compiler_rt_archive}")

set(cxx_binary "${CMAKE_BINARY_DIR}/_deps/libcxx-${runtime_cpu}-build")
# Imported include directories must exist when the parent is generated. The
# external build fills them before any firmware/dependency source is compiled.
file(MAKE_DIRECTORY "${cxx_binary}/include/c++/v1")
ExternalProject_Add(e200_libcxx
    DEPENDS e200_compiler_rt
    PREFIX "${CMAKE_BINARY_DIR}/_deps/libcxx-${runtime_cpu}"
    LIST_SEPARATOR "|"
    SOURCE_DIR "${E200_LLVM_SOURCE_DIR}/runtimes"
    BINARY_DIR "${cxx_binary}"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
    CMAKE_ARGS ${runtime_args}
        "-DLLVM_ENABLE_RUNTIMES=libcxx|libcxxabi"
        -DLLVM_ENABLE_LTO=Full
        -DLLVM_INCLUDE_TESTS=OFF
        -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF
        -DLIBCXX_ENABLE_SHARED=OFF
        -DLIBCXX_ENABLE_STATIC=ON
        -DLIBCXX_ABI_VERSION=2
        -DLIBCXX_ENABLE_EXCEPTIONS=OFF
        -DLIBCXX_ENABLE_RTTI=OFF
        -DLIBCXX_ENABLE_THREADS=OFF
        -DLIBCXX_ENABLE_MONOTONIC_CLOCK=OFF
        -DLIBCXX_ENABLE_FILESYSTEM=OFF
        -DLIBCXX_ENABLE_LOCALIZATION=OFF
        -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF
        -DLIBCXX_ENABLE_RANDOM_DEVICE=OFF
        -DLIBCXX_ENABLE_UNICODE=OFF
        -DLIBCXX_ENABLE_TIME_ZONE_DATABASE=OFF
        -DLIBCXX_INCLUDE_TESTS=OFF
        -DLIBCXX_INCLUDE_BENCHMARKS=OFF
        -DLIBCXXABI_ENABLE_SHARED=OFF
        -DLIBCXXABI_ENABLE_STATIC=ON
        -DLIBCXXABI_ENABLE_EXCEPTIONS=OFF
        -DLIBCXXABI_ENABLE_THREADS=OFF
        -DLIBCXX_USE_COMPILER_RT=ON
        -DLIBCXXABI_USE_COMPILER_RT=ON
        -DLIBCXXABI_USE_LLVM_UNWINDER=OFF
        -DLIBCXXABI_BAREMETAL=ON
        -DLIBCXXABI_INCLUDE_TESTS=OFF
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS "${cxx_binary}/lib/libc++.a" "${cxx_binary}/lib/libc++abi.a"
    EXCLUDE_FROM_ALL TRUE
)
foreach(name c++ c++abi)
    add_library(e200_${name} STATIC IMPORTED GLOBAL)
    set_target_properties(e200_${name} PROPERTIES
        IMPORTED_LOCATION "${cxx_binary}/lib/lib${name}.a")
endforeach()

add_library(e200_runtime INTERFACE)
target_compile_options(e200_runtime INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:-nostdinc++;-fno-rtti>")
target_include_directories(e200_runtime SYSTEM INTERFACE "${cxx_binary}/include/c++/v1")
target_link_libraries(e200_runtime INTERFACE e200_c++ e200_c++abi c e200_builtins)
add_dependencies(e200_runtime e200_compiler_rt e200_libcxx)
