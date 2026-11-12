cmake_minimum_required(VERSION 4.0)

project(KernelMPC5566 C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIBRARIES_DIR	    ${CMAKE_SOURCE_DIR}/libs)

include(FetchContent)
include(cmake/Runtime.cmake)

FetchContent_Declare(
    EmbeddedIOServices
    GIT_REPOSITORY https://github.com/FL0WL0W/EmbeddedIOServices
    GIT_TAG        main
    GIT_PROGRESS   TRUE
    GIT_SHALLOW    TRUE
    SOURCE_DIR     ${LIBRARIES_DIR}/EmbeddedIOServices
)

FetchContent_MakeAvailable(EmbeddedIOServices)
target_link_libraries(EmbeddedIOServices PUBLIC e200_runtime)

set(EmbeddedIOServicesHardwareAbstraction_Path "${embeddedioservices_SOURCE_DIR}/HardwareAbstractions/MPC5xxx")
include_directories(${EmbeddedIOServicesHardwareAbstraction_Path}/include)
set(EmbeddedIOServiceHardwareAbstraction_SRCS
    ${EmbeddedIOServicesHardwareAbstraction_Path}/src/MPC5xxxSystemClockService.cpp
    ${EmbeddedIOServicesHardwareAbstraction_Path}/src/MPC55xxSystemClockService.cpp
    ${EmbeddedIOServicesHardwareAbstraction_Path}/src/MPC5xxxFlexCAN2Service.cpp
    ${EmbeddedIOServicesHardwareAbstraction_Path}/src/MPC5xxxSPIService.cpp
)

include_directories(${CMAKE_SOURCE_DIR}/include)
file(GLOB_RECURSE SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)

set(MPC5566_LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/MPC5566_RAM.ld")
add_executable(firmware.elf ${EmbeddedIOServiceHardwareAbstraction_SRCS} ${SRCS} startup_mpc5566.S)
set_property(TARGET firmware.elf PROPERTY LINK_DEPENDS ${MPC5566_LINKER_SCRIPT})
target_compile_definitions(firmware.elf PRIVATE MPC5566)
target_link_libraries(firmware.elf EmbeddedIOServices)

set(HEX_FILE "${CMAKE_CURRENT_BINARY_DIR}/Kernel-E78.hex")
set(BIN_FILE "${CMAKE_CURRENT_BINARY_DIR}/Kernel-E78.bin")
add_custom_command(TARGET firmware.elf POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O ihex $<TARGET_FILE:firmware.elf> "${HEX_FILE}"
    COMMAND "${CMAKE_OBJCOPY}" -O binary $<TARGET_FILE:firmware.elf> "${BIN_FILE}"
    COMMAND "${CMAKE_SIZE}" $<TARGET_FILE:firmware.elf>
    BYPRODUCTS "${HEX_FILE}" "${BIN_FILE}"
    VERBATIM
)
