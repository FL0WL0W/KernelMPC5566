  set(CMAKE_SYSTEM_NAME Generic)
  set(CMAKE_SYSTEM_PROCESSOR powerpc)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

  find_program(POWERPC_GCC     NAMES powerpc-eabivle-gcc REQUIRED)
  find_program(POWERPC_GXX     NAMES powerpc-eabivle-g++ REQUIRED)
  find_program(POWERPC_OBJCOPY NAMES powerpc-eabivle-objcopy REQUIRED)

  set(CMAKE_C_COMPILER   "${POWERPC_GCC}")
  set(CMAKE_CXX_COMPILER "${POWERPC_GXX}")
  set(CMAKE_ASM_COMPILER "${POWERPC_GCC}")
  set(CMAKE_OBJCOPY      "${POWERPC_OBJCOPY}")

  # Compiler is at <toolchain-root>/bin/powerpc-eabivle-gcc.
  get_filename_component(POWERPC_BIN_DIR "${POWERPC_GCC}" DIRECTORY)
  get_filename_component(POWERPC_TOOLCHAIN_ROOT "${POWERPC_BIN_DIR}" DIRECTORY)

  set(CMAKE_SYSROOT
      "${POWERPC_TOOLCHAIN_ROOT}/powerpc-eabivle/newlib"
  )

  if(NOT EXISTS "${CMAKE_SYSROOT}/lib/nano.specs")
      message(FATAL_ERROR
          "Invalid PowerPC toolchain sysroot: ${CMAKE_SYSROOT}"
      )
  endif()
