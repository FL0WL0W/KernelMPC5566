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

set(SRC_FILES ${USER_CSOURCES} ${USER_CXXSOURCES} ${EmbeddedIOService_SOURCES}
    startup_mpc5566.s MPC5566_RAM.ld)
add_executable(firmware.elf ${SRC_FILES})

target_compile_definitions(firmware.elf PRIVATE MPC5566)
target_link_libraries(firmware.elf EmbeddedIOServices)

set(HEX_FILE ${PROJECT_SOURCE_DIR}/build/Kernel-E78.hex)
set(BIN_FILE ${PROJECT_SOURCE_DIR}/build/Kernel-E78.bin)
add_custom_command(TARGET firmware.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} ARGS -O ihex $<TARGET_FILE:firmware.elf> ${HEX_FILE}
        COMMAND ${CMAKE_OBJCOPY} ARGS -O binary $<TARGET_FILE:firmware.elf> ${BIN_FILE}
        COMMENT "Building ${HEX_FILE} \nBuilding ${BIN_FILE}")
