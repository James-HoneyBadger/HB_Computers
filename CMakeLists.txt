# SPDX-License-Identifier: MIT
#
# The Kestrel: two home computers from a 1982 that never happened, and the one
# emulator that runs both. The machines' languages are machine code in ROMs
# assembled by the two assemblers in `tools/`; the host supplies a processor, a
# bus, a tube, a keyboard and a disk.
#
# Nothing here needs anything but a C++20 compiler, and the windows need Qt 6
# Widgets as well. There are no other dependencies.

cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" KESTREL_VERSION_RAW)
string(STRIP "${KESTREL_VERSION_RAW}" KESTREL_VERSION)

project(Kestrel VERSION ${KESTREL_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(KESTREL_WERROR "Treat compiler warnings as errors" ON)
option(KESTREL_BUILD_TESTS "Build the test suites" ON)
option(KESTREL_BUILD_GUI "Build the Qt machines' window" ON)
option(KESTREL_BUILD_TOOLS "Build the assemblers and the command line" ON)

# ---------------------------------------------------------------------------
# Warnings
# ---------------------------------------------------------------------------

function(kestrel_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        return()
    endif()
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-compare)
    if(KESTREL_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# The assemblers
# ---------------------------------------------------------------------------

add_executable(kestrel_asm tools/kestrel_asm.cpp)
kestrel_warnings(kestrel_asm)

add_executable(kestrel16_asm tools/kestrel16_asm.cpp)
kestrel_warnings(kestrel16_asm)


# ---------------------------------------------------------------------------
# The ROMs
# ---------------------------------------------------------------------------
#
# kestrel_rom(<name> <origin> <size>) assembles a 6502 image;
# kestrel16_rom(<name> <origin> <size>) a 65816 one, which may live in a bank of
# its own. Each becomes a header of bytes the machine can be given.

set(KESTREL_ROM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/roms/kestrel)
set(KESTREL_GENERATED ${CMAKE_BINARY_DIR}/generated/kestrel)

function(kestrel_rom name origin size)
    set(header ${KESTREL_GENERATED}/${name}_rom.hpp)
    # The image as data as well as as a header, so a program that is not built by
    # this CMake can load it -- the Rust port does.
    set(binary ${KESTREL_GENERATED}/${name}.bin)
    add_custom_command(
        OUTPUT ${header} ${binary}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${KESTREL_GENERATED}
        COMMAND kestrel_asm ${KESTREL_ROM_DIR}/${name}.s
                --name ${name} --origin ${origin} --size ${size} --fill 0xFF
                --header ${header} --bin ${binary}
                --symbols ${KESTREL_GENERATED}/${name}.sym
                --listing ${KESTREL_GENERATED}/${name}.lst
        WORKING_DIRECTORY ${KESTREL_ROM_DIR}
        DEPENDS kestrel_asm ${KESTREL_ROM_SOURCES}
        COMMENT "Assembling the Kestrel's ${name} ROM"
        VERBATIM)
    set(KESTREL_ROM_HEADERS ${KESTREL_ROM_HEADERS} ${header} PARENT_SCOPE)
endfunction()

file(GLOB KESTREL_ROM_SOURCES CONFIGURE_DEPENDS
    ${KESTREL_ROM_DIR}/*.s ${KESTREL_ROM_DIR}/*.inc)
kestrel_rom(basic 0xA000 16384)
kestrel_rom(kernal 0xE000 8192)
kestrel_rom(monitor 0xA000 16384)
add_custom_target(kestrel_roms DEPENDS ${KESTREL_ROM_HEADERS})

set(KESTREL16_ROM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/roms/kestrel16)
set(KESTREL16_GENERATED ${CMAKE_BINARY_DIR}/generated/kestrel16)

function(kestrel16_rom name origin size)
    set(header ${KESTREL16_GENERATED}/${name}_rom.hpp)
    set(binary ${KESTREL16_GENERATED}/${name}.bin)
    add_custom_command(
        OUTPUT ${header} ${binary}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${KESTREL16_GENERATED}
        COMMAND kestrel16_asm ${KESTREL16_ROM_DIR}/${name}.s
                --name ${name} --origin ${origin} --size ${size} --fill 0xFF
                --header ${header} --bin ${binary}
                --symbols ${KESTREL16_GENERATED}/${name}.sym
                --listing ${KESTREL16_GENERATED}/${name}.lst
        WORKING_DIRECTORY ${KESTREL16_ROM_DIR}
        DEPENDS kestrel16_asm ${KESTREL16_ROM_SOURCES}
        COMMENT "Assembling the Kestrel 16's ${name} ROM"
        VERBATIM)
    set(KESTREL16_ROM_HEADERS ${KESTREL16_ROM_HEADERS} ${header} PARENT_SCOPE)
endfunction()

file(GLOB KESTREL16_ROM_SOURCES CONFIGURE_DEPENDS
    ${KESTREL16_ROM_DIR}/*.s ${KESTREL16_ROM_DIR}/*.inc)
kestrel16_rom(kernal 57344 8192)
kestrel16_rom(basic 65536 65536)
add_custom_target(kestrel16_roms DEPENDS ${KESTREL16_ROM_HEADERS})

# ---------------------------------------------------------------------------
# The machine
# ---------------------------------------------------------------------------
#
# One class, two models. Both processors are real parts and both are used whole;
# the tube, the keyboard, the disk and the tape are shared code that takes its
# numbers from the model's profile.

add_library(kestrel_machine
    src/kestrel/devices.cpp
    src/kestrel/machine.cpp
    src/kestrel/model1982/cpu6502.cpp
    src/kestrel/model1982/roms.cpp
    src/kestrel/model1992/cpu65816.cpp
)
add_library(kestrel::machine ALIAS kestrel_machine)
target_include_directories(kestrel_machine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)
kestrel_warnings(kestrel_machine)
add_dependencies(kestrel_machine kestrel_roms kestrel16_roms)

if(KESTREL_BUILD_TOOLS)
    # One emulator, two models, on a shell.
    add_executable(kestrel tools/kestrel.cpp)
    target_link_libraries(kestrel PRIVATE kestrel::machine)
    kestrel_warnings(kestrel)
    install(TARGETS kestrel RUNTIME DESTINATION bin)

    # The interactive emulator: one program, either machine, chosen at the start
    # and switchable in the middle of a session.
    add_executable(kestrel_emu tools/kestrel_emu.cpp)
    target_link_libraries(kestrel_emu PRIVATE kestrel::machine)
    kestrel_warnings(kestrel_emu)
    install(TARGETS kestrel_emu RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# The window
# ---------------------------------------------------------------------------

if(KESTREL_BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)
    if(Qt6_FOUND)
        message(STATUS "Qt6 ${Qt6Widgets_VERSION} found: building the window")
        add_executable(kestrel_computers
            app/main.cpp
            src/kestrel/model1982/window.cpp
            include/kestrel/model1982/window.hpp)
        target_link_libraries(kestrel_computers PRIVATE
            kestrel::machine Qt6::Widgets)
        target_include_directories(kestrel_computers PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
        set_target_properties(kestrel_computers PROPERTIES AUTOMOC ON)
        kestrel_warnings(kestrel_computers)
        install(TARGETS kestrel_computers RUNTIME DESTINATION bin)
    else()
        message(STATUS "Qt6 Widgets not found: the window will not be built")
    endif()
endif()

# ---------------------------------------------------------------------------
# The test suites
# ---------------------------------------------------------------------------

if(KESTREL_BUILD_TESTS)
    enable_testing()

    function(kestrel_suite suite)
        add_executable(${suite} tests/${suite}.cpp)
        target_include_directories(${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_link_libraries(${suite} PRIVATE kestrel::machine)
        kestrel_warnings(${suite})
        add_test(NAME ${suite} COMMAND ${suite})
    endfunction()

    kestrel_suite(kestrel_asm_tests)
    kestrel_suite(kestrel_cpu_tests)
    kestrel_suite(kestrel_machine_tests)
    kestrel_suite(kestrel16_asm_tests)
    kestrel_suite(kestrel16_cpu_tests)
    kestrel_suite(kestrel16_machine_tests)
    kestrel_suite(kestrel_samples_tests)
    kestrel_suite(kestrel_emu_tests)

    # Both assemblers are run as the programs the build uses, so their tests
    # check the bytes that end up in the ROMs.
    target_compile_definitions(kestrel_asm_tests PRIVATE
        KESTREL_ASM_PATH="$<TARGET_FILE:kestrel_asm>")
    add_dependencies(kestrel_asm_tests kestrel_asm)
    target_compile_definitions(kestrel16_asm_tests PRIVATE
        KESTREL16_ASM_PATH="$<TARGET_FILE:kestrel16_asm>")
    # The samples are run as the files a person would type, read from the tree
    # rather than copied into the build.
    target_compile_definitions(kestrel_samples_tests PRIVATE
        KESTREL_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples")
    # The emulator's front end is run as the program a person runs, so what is
    # checked is that the model flag and the session commands do what they say.
    target_compile_definitions(kestrel_emu_tests PRIVATE
        KESTREL_EMU_PATH="$<TARGET_FILE:kestrel_emu>")
    add_dependencies(kestrel_emu_tests kestrel_emu)
    add_dependencies(kestrel16_asm_tests kestrel16_asm)
endif()
