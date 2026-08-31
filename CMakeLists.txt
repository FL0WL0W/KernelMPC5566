 cmake_minimum_required(VERSION 4.0)

project ( KernelMPC5566 C CXX ASM )

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIBRARIES_DIR	    ${CMAKE_SOURCE_DIR}/libs)
set(EmbeddedIOServicesHardwareAbstraction_Path ${LIBRARIES_DIR}/EmbeddedIOServices/HardwareAbstractions/MPC5xxx)

include(FetchContent)

FetchContent_Declare(
    EmbeddedIOServices
    GIT_REPOSITORY https://github.com/FL0WL0W/EmbeddedIOServices
    GIT_TAG        main
    GIT_PROGRESS   TRUE
    GIT_SHALLOW    TRUE
    SOURCE_DIR  ${LIBRARIES_DIR}/EmbeddedIOServices
)

FetchContent_MakeAvailable(EmbeddedIOServices)

include_directories(include)
include_directories(${EmbeddedIOServicesHardwareAbstraction_Path}/include)

file(GLOB_RECURSE USER_CSOURCES src/*.c)
file(GLOB_RECURSE USER_CSOURCES_Templates src/*_template.c)
IF(USER_CSOURCES_Templates)
        list(REMOVE_ITEM USER_CSOURCES ${USER_CSOURCES_Templates})
ENDIF()
file(GLOB_RECURSE USER_CXXSOURCES src/*.cpp)
file(GLOB_RECURSE USER_CXXSOURCES_Templates src/*_template.cpp)
IF(USER_CXXSOURCES_Templates)
        list(REMOVE_ITEM USER_CXXSOURCES ${USER_CXXSOURCES_Templates})
ENDIF()
file(GLOB_RECURSE EmbeddedIOService_SOURCES ${EmbeddedIOServicesHardwareAbstraction_Path}/src/*.cpp)
file(GLOB_RECURSE EmbeddedIOService_SOURCES_Templates ${EmbeddedIOServicesHardwareAbstraction_Path}/src/*_template.cpp)
IF(EmbeddedIOService_SOURCES_Templates)
        list(REMOVE_ITEM EmbeddedIOService_SOURCES ${EmbeddedIOService_SOURCES_Templates})
ENDIF()

set(MPC5566_STARTUP_SOURCE
    "${CMAKE_SOURCE_DIR}/startup_mpc5566.s"
    CACHE FILEPATH "Startup procedure used by the firmware image")
set(MPC5566_LINKER_SCRIPT
    "${CMAKE_SOURCE_DIR}/MPC5566_RAM.ld"
    CACHE FILEPATH "Linker script used by the firmware image")
set(MPC5566_IMAGE_BASENAME
    "Kernel-E78"
    CACHE STRING "Base filename for generated firmware images")
set(MPC5566_GENERATE_FULL_FLASH_IMAGE
    OFF
    CACHE BOOL "Combine the application with a resident bootloader and pad to full flash size")
set(MPC5566_BOOTLOADER_IMAGE
    "${CMAKE_SOURCE_DIR}/bootloader.bin"
    CACHE FILEPATH "Bootloader used at the beginning of the full flash image")
set(MPC5566_BINARY_GAP_FILL
    ""
    CACHE STRING "Optional objcopy fill byte for gaps in the raw binary")

set(SRC_FILES ${USER_CSOURCES} ${USER_CXXSOURCES} ${EmbeddedIOService_SOURCES}
    ${MPC5566_STARTUP_SOURCE})
add_executable(firmware.elf ${SRC_FILES})
set_property(TARGET firmware.elf PROPERTY LINK_DEPENDS ${MPC5566_LINKER_SCRIPT})

target_compile_definitions(firmware.elf PRIVATE MPC5566)
target_link_libraries(firmware.elf EmbeddedIOServices)

set(HEX_FILE ${PROJECT_SOURCE_DIR}/build/${MPC5566_IMAGE_BASENAME}.hex)
set(BIN_FILE ${PROJECT_SOURCE_DIR}/build/${MPC5566_IMAGE_BASENAME}.bin)
set(BINARY_OBJCOPY_ARGS -O binary)
if(NOT MPC5566_BINARY_GAP_FILL STREQUAL "")
    list(APPEND BINARY_OBJCOPY_ARGS --gap-fill ${MPC5566_BINARY_GAP_FILL})
endif()
add_custom_command(TARGET firmware.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} ARGS -O ihex $<TARGET_FILE:firmware.elf> ${HEX_FILE}
        COMMAND ${CMAKE_OBJCOPY} ARGS ${BINARY_OBJCOPY_ARGS} $<TARGET_FILE:firmware.elf> ${BIN_FILE}
        COMMENT "Building ${HEX_FILE} \nBuilding ${BIN_FILE}")

if(MPC5566_GENERATE_FULL_FLASH_IMAGE)
    find_program(BASH_EXECUTABLE bash REQUIRED)
    set(FULL_FLASH_IMAGE_SCRIPT
        ${CMAKE_SOURCE_DIR}/tools/build_full_flash_image.sh)
    set_property(TARGET firmware.elf APPEND PROPERTY LINK_DEPENDS
        ${MPC5566_BOOTLOADER_IMAGE}
        ${FULL_FLASH_IMAGE_SCRIPT})
    set(FULL_FLASH_BIN_FILE
        ${PROJECT_SOURCE_DIR}/build/${MPC5566_IMAGE_BASENAME}-Full.bin)
    add_custom_command(TARGET firmware.elf POST_BUILD
        COMMAND ${BASH_EXECUTABLE}
            ${FULL_FLASH_IMAGE_SCRIPT}
            ${MPC5566_BOOTLOADER_IMAGE}
            ${BIN_FILE}
            ${FULL_FLASH_BIN_FILE}
            0x80000
            0x300000
        COMMENT "Building complete 3 MiB flash image ${FULL_FLASH_BIN_FILE}")
endif()
