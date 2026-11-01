# SPDX-License-Identifier: MIT
#
# Kestrel Computers: the Kestrel and the Kestrel 16, standing on their own.
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ctest --test-dir build --output-on-failure
#
# The machines are built from the same Qt-free engines the Time Warp Studio
# editors use -- the eleven language executors, the expression evaluator, the
# Scheme machine, the canvas queue -- plus one Qt library for the windows. A
# build host with no Qt still gets every engine and every engine test.

cmake_minimum_required(VERSION 3.20)

project(kestrel_computers
    VERSION 1.0.0
    DESCRIPTION "The Kestrel and Kestrel 16 home computers"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# A compilation database: clangd and the C/C++ extension resolve the Qt include
# paths from it instead of reporting every QWidget include as missing.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HB_WERROR "Treat compiler warnings as errors" ON)
option(HB_BUILD_TESTS "Build the test suites" ON)
option(HB_BUILD_GUI "Build the Qt machine windows" ON)
option(HB_BUILD_TOOLS "Build the headless tools" ON)

include(cmake/warnings.cmake)

# Two of the flags below are GCC's own and Clang rejects them as unknown, which
# `-Werror` turns into a failed build.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(HB_GNU_WARNING_FLAGS -Wlogical-op -Wuseless-cast)
else()
    set(HB_GNU_WARNING_FLAGS "")
endif()

# `-Wnull-dereference` is dropped for GCC before 14: GCC 13 reports a false
# positive inside libstdc++'s own `__shared_ptr` copy constructor. Nothing is
# left unwatched, because the sanitizer jobs treat a null dereference as a crash.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    set(HB_NULL_DEREFERENCE "")
else()
    set(HB_NULL_DEREFERENCE "-Wnull-dereference")
endif()

# The product version, read from the repository's VERSION file. The window title
# and the packages must not be able to disagree with the file a release bumps.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" HB_VERSION_RAW)
string(STRIP "${HB_VERSION_RAW}" HB_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# The Kestrel: a 6502, its ROMs, and the machine that runs them
# ---------------------------------------------------------------------------
#
# The ROMs are 6502 machine code, assembled from roms/kestrel/*.s at build time.
# `kestrel_asm` is a build tool, not a dependency: the tree still builds with nothing
# but a C++20 compiler, and the images are generated where the compiler can see
# them.

add_executable(kestrel_asm tools/kestrel_asm.cpp)
hb_enable_warnings(kestrel_asm)

add_executable(kestrel16_asm tools/kestrel16_asm.cpp)
hb_enable_warnings(kestrel16_asm)

set(KESTREL_ROM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/roms/kestrel)
set(HB1_GENERATED ${CMAKE_BINARY_DIR}/generated/kestrel)
file(GLOB KESTREL_ROM_SOURCES CONFIGURE_DEPENDS
    ${KESTREL_ROM_DIR}/*.s
    ${KESTREL_ROM_DIR}/*.inc
)

# kestrel_rom(<name> <origin> <size>)
function(kestrel_rom name origin size)
    set(header ${HB1_GENERATED}/${name}_rom.hpp)
    add_custom_command(
        OUTPUT ${header}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${HB1_GENERATED}
        COMMAND kestrel_asm ${KESTREL_ROM_DIR}/${name}.s
                --name ${name}
                --origin ${origin}
                --size ${size}
                --fill 0xFF
                --header ${header}
                --symbols ${HB1_GENERATED}/${name}.sym
                --listing ${HB1_GENERATED}/${name}.lst
        WORKING_DIRECTORY ${KESTREL_ROM_DIR}
        DEPENDS kestrel_asm ${KESTREL_ROM_SOURCES}
        COMMENT "Assembling the ${name} ROM"
        VERBATIM
    )
    set(KESTREL_ROM_HEADERS ${KESTREL_ROM_HEADERS} ${header} PARENT_SCOPE)
endfunction()

kestrel_rom(kernal 0xE000 8192)
kestrel_rom(basic 0xA000 16384)
kestrel_rom(monitor 0xA000 16384)

add_custom_target(kestrel_roms DEPENDS ${KESTREL_ROM_HEADERS})
message(STATUS "Kestrel ROMs: ${KESTREL_ROM_HEADERS}")

# ---------------------------------------------------------------------------
# The Kestrel 16's ROMs
# ---------------------------------------------------------------------------
#
# The KERNAL sits at the top of bank zero, where the processor's reset vector
# points; the language has a bank of its own, which is what a decade bought.

set(KESTREL16_ROM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/roms/kestrel16)
set(HB2_GENERATED ${CMAKE_BINARY_DIR}/generated/kestrel16)
file(GLOB KESTREL16_ROM_SOURCES CONFIGURE_DEPENDS
    ${KESTREL16_ROM_DIR}/*.s
    ${KESTREL16_ROM_DIR}/*.inc
)

# kestrel16_rom(<name> <origin> <size>)
function(kestrel16_rom name origin size)
    set(header ${HB2_GENERATED}/${name}_rom.hpp)
    add_custom_command(
        OUTPUT ${header}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${HB2_GENERATED}
        COMMAND kestrel16_asm ${KESTREL16_ROM_DIR}/${name}.s
                --name ${name}
                --origin ${origin}
                --size ${size}
                --fill 0xFF
                --header ${header}
                --symbols ${HB2_GENERATED}/${name}.sym
                --listing ${HB2_GENERATED}/${name}.lst
        WORKING_DIRECTORY ${KESTREL16_ROM_DIR}
        DEPENDS kestrel16_asm ${KESTREL16_ROM_SOURCES}
        COMMENT "Assembling the Kestrel 16 ${name} ROM"
        VERBATIM
    )
    set(KESTREL16_ROM_HEADERS ${KESTREL16_ROM_HEADERS} ${header} PARENT_SCOPE)
endfunction()

kestrel16_rom(kernal 57344 8192)
kestrel16_rom(basic 65536 65536)

add_custom_target(kestrel16_roms DEPENDS ${KESTREL16_ROM_HEADERS})

# One machine, two models: the processors are both real parts and both are used
# whole; everything around them -- the tube, the keyboard, the disk, the tape and
# the bus -- is shared code that takes its numbers from the model's profile.
add_library(kestrel_machine
    src/devices.cpp
    src/machine.cpp
    src/kestrel/cpu6502.cpp
    src/kestrel/roms.cpp
    src/kestrel16/cpu65816.cpp
)
add_library(kestrel::machine ALIAS kestrel_machine)
target_include_directories(kestrel_machine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)
target_compile_features(kestrel_machine PUBLIC cxx_std_20)
hb_enable_warnings(kestrel_machine)
add_dependencies(kestrel_machine kestrel_roms kestrel16_roms)

# ---------------------------------------------------------------------------
# The Qt-free engines
# ---------------------------------------------------------------------------

add_library(hb_expr
    src/token.cpp
    src/lexer.cpp
    src/evaluator.cpp
)
add_library(hb::expr ALIAS hb_expr)
target_include_directories(hb_expr PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(hb_expr PUBLIC cxx_std_20)
hb_enable_warnings(hb_expr)

add_library(hb_scheme
    src/scheme/value.cpp
    src/scheme/reader.cpp
    src/scheme/printer.cpp
    src/scheme/machine.cpp
    src/scheme/machine_apply.cpp
    src/scheme/machine_forms.cpp
    src/scheme/builtins.cpp
)
add_library(hb::scheme ALIAS hb_scheme)
target_include_directories(hb_scheme PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(hb_scheme PUBLIC cxx_std_20)
hb_enable_warnings(hb_scheme)

add_library(hb_audio
    src/audio/music.cpp
    src/audio/mixer.cpp
)
add_library(hb::audio ALIAS hb_audio)
target_include_directories(hb_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(hb_audio PUBLIC cxx_std_20)
hb_enable_warnings(hb_audio)
# No fused multiply-add: the drawings are compared with a reference that rounds
# every multiplication and addition separately.
target_compile_options(hb_audio PRIVATE ${HB_GNU_WARNING_FLAGS}
    ${HB_NULL_DEREFERENCE} -ffp-contract=off)

add_library(hb_io
    src/io/ports.cpp
    src/io/gamepad.cpp
)
add_library(hb::io ALIAS hb_io)
target_include_directories(hb_io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(hb_io PUBLIC cxx_std_20)
hb_enable_warnings(hb_io)
target_compile_options(hb_io PRIVATE ${HB_GNU_WARNING_FLAGS}
    ${HB_NULL_DEREFERENCE})

# The interpreter loop, the value type, dispatch and one executor per language.
# The Kestrel's ROM socket is this list: BASIC, Logo, PILOT, FOCAL, COMAL, REXX,
# APL, Smalltalk, Forth and CHIP-8, with Scheme answered by the machine above.
file(GLOB HB_LANG_SOURCES CONFIGURE_DEPENDS src/lang/*.cpp)
add_library(hb_lang ${HB_LANG_SOURCES})
add_library(hb::lang ALIAS hb_lang)
target_include_directories(hb_lang PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(hb_lang PUBLIC cxx_std_20)
target_link_libraries(hb_lang PUBLIC hb::expr hb::audio hb::io)
hb_enable_warnings(hb_lang)
target_compile_options(hb_lang PRIVATE ${HB_GNU_WARNING_FLAGS}
    ${HB_NULL_DEREFERENCE} -ffp-contract=off)

# The bridge between an interpreter thread and a GUI thread, Qt-free so it can be
# unit tested and so a machine without Qt still builds the queue.
add_library(hb_canvas
    src/canvas/command.cpp
    src/canvas/command_queue.cpp
)
add_library(hb::canvas ALIAS hb_canvas)
target_include_directories(hb_canvas PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(hb_canvas PUBLIC cxx_std_20)
target_link_libraries(hb_canvas PUBLIC Threads::Threads)
hb_enable_warnings(hb_canvas)


# ---------------------------------------------------------------------------
# Kestrel 16, the machine
# ---------------------------------------------------------------------------
#
# A 65816 with sixteen megabytes of address space, and the ROM socket that holds
# its KERNAL and its BASIC. The languages are machine code in `roms/kestrel16/`, the
# same way the Kestrel's are.

# ---------------------------------------------------------------------------
# Headless tools
# ---------------------------------------------------------------------------

if(HB_BUILD_TOOLS)
    # One emulator, two models: the same program powers on either machine.
    add_executable(kestrel tools/kestrel.cpp)
    target_link_libraries(kestrel PRIVATE kestrel::machine)
    hb_enable_warnings(kestrel)
    install(TARGETS kestrel RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# The Qt windows
# ---------------------------------------------------------------------------

if(HB_BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Core Gui Widgets)
    set(HB_QT Qt6)
    set(HB_QT_FOUND "${${HB_QT}_FOUND}")
endif()

if(HB_BUILD_GUI AND HB_QT_FOUND)
    message(STATUS "${HB_QT} ${${HB_QT}Widgets_VERSION} found: building the machine windows")

    # Two components are wanted but not required. `Svg` is QSvgGenerator, for the
    # canvas's vector export; a build without it still runs and the format is
    # absent rather than broken.
    find_package(${HB_QT} QUIET COMPONENTS Svg)

    add_library(hb_canvas_qt
        src/canvas/qt_bridge.cpp
        include/timewarp/canvas/qt_bridge.hpp
    )
    add_library(hb::canvas_qt ALIAS hb_canvas_qt)
    target_link_libraries(hb_canvas_qt PUBLIC hb::canvas ${HB_QT}::Core)
    target_include_directories(hb_canvas_qt PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    set_target_properties(hb_canvas_qt PROPERTIES AUTOMOC ON)
    hb_enable_warnings(hb_canvas_qt)

    # Everything the two machines are made of: the CRT and its glass, Kestrel DOS, the
    # 64K and the tube, the Kestrel 16's window, the editor on the Kestrel's PROGRAM page,
    # the runner and the REPL behind its prompt, and the turtle canvas beside it.
    #
    # Headers are listed as sources so AUTOMOC scans them: the Q_OBJECT classes
    # live in `include/`, outside this target's source directory.
    set(HB_UI_HEADERS
        include/timewarp/ui/app_paths.hpp
        include/timewarp/ui/code_editor.hpp
        include/timewarp/ui/hb_bbs.hpp
        include/timewarp/ui/hb_clock.hpp
        include/timewarp/ui/hb_dos.hpp
        include/timewarp/ui/hb_memory.hpp
        include/timewarp/ui/hb_tube.hpp
        include/timewarp/ui/language.hpp
        include/timewarp/ui/lint.hpp
        include/timewarp/ui/machine_image.hpp
        include/timewarp/ui/machine_voice.hpp
        include/timewarp/ui/native_runner.hpp
        include/timewarp/ui/crt_overlay.hpp
        include/timewarp/ui/repl_runner.hpp
        include/timewarp/ui/retro_computer.hpp
        include/timewarp/ui/retro_settings.hpp
        include/timewarp/ui/sound_player.hpp
        include/timewarp/ui/syntax_highlighter.hpp
        include/timewarp/ui/turtle_canvas.hpp
    )
    add_library(hb_ui
        src/ui/app_paths.cpp
        src/ui/code_editor.cpp
        src/ui/hb_bbs.cpp
        src/ui/hb_clock.cpp
        src/ui/hb_dos.cpp
        src/ui/hb_memory.cpp
        src/ui/hb_tube.cpp
        src/ui/language.cpp
        src/ui/lint.cpp
        src/ui/machine_image.cpp
        src/ui/machine_voice.cpp
        src/ui/native_runner.cpp
        src/ui/crt_overlay.cpp
        src/ui/repl_runner.cpp
        src/ui/retro_computer_basic.cpp
        src/ui/retro_computer_bbs.cpp
        src/ui/retro_computer_boot.cpp
        src/ui/retro_computer_dos.cpp
        src/ui/retro_computer_machine.cpp
        src/ui/retro_computer_settings.cpp
        src/ui/retro_computer_support.cpp
        src/ui/retro_computer_ui.cpp
        src/ui/retro_settings.cpp
        src/ui/sound_player.cpp
        src/ui/syntax_highlighter.cpp
        src/ui/turtle_canvas_commands.cpp
        src/ui/turtle_canvas_painting.cpp
        src/ui/turtle_canvas_raster.cpp
        src/ui/turtle_canvas_view.cpp
        src/ui/retro_computer_internal.hpp
        src/ui/turtle_canvas_internal.hpp
        ${HB_UI_HEADERS}
    )
    add_library(hb::ui ALIAS hb_ui)
    target_include_directories(hb_ui PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    target_compile_features(hb_ui PUBLIC cxx_std_20)
    target_link_libraries(hb_ui PUBLIC ${HB_QT}::Widgets)
    target_link_libraries(hb_ui PRIVATE hb::lang hb::scheme hb::canvas_qt)
    if(${HB_QT}Svg_FOUND)
        target_link_libraries(hb_ui PUBLIC ${HB_QT}::Svg)
        target_compile_definitions(hb_ui PUBLIC TIME_WARP_HAVE_SVG=1)
    endif()
    set_target_properties(hb_ui PROPERTIES AUTOMOC ON)
    hb_enable_warnings(hb_ui)

    # The launcher: one window naming both machines, with the two actions the
    # tests drive. A library of its own so the tests do not have to link an
    # `int main`.
    add_library(hb_launcher
        app/machine_launcher.cpp
        app/machine_launcher.hpp
    )
    add_library(hb::launcher ALIAS hb_launcher)
    target_include_directories(hb_launcher PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/app>)
    target_link_libraries(hb_launcher PUBLIC hb::ui)
    set_target_properties(hb_launcher PROPERTIES AUTOMOC ON)
    hb_enable_warnings(hb_launcher)

    # The Kestrel on its own: the 6502 machine and the tube it draws on. It is a
    # separate program from the launcher above because it is a separate machine
    # -- there is no interpreter behind this window.
    add_executable(kestrel_window
        app/kestrel_window_main.cpp
        src/kestrel/window.cpp
        include/timewarp/kestrel/window.hpp
    )
    target_link_libraries(kestrel_window PRIVATE kestrel::machine ${HB_QT}::Widgets)
    target_include_directories(kestrel_window PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    set_target_properties(kestrel_window PROPERTIES AUTOMOC ON)
    hb_enable_warnings(kestrel_window)

    add_executable(kestrel_computers app/main.cpp)
    target_link_libraries(kestrel_computers PRIVATE hb::launcher)
    target_compile_definitions(kestrel_computers PRIVATE HB_VERSION="${HB_VERSION}")
    hb_enable_warnings(kestrel_computers)

    install(TARGETS kestrel_computers RUNTIME DESTINATION bin)
    # The machines boot from the programs beside them: Kestrel DOS's volume and
    # HB-OS's two processes are found through `examplesDir()`.
    install(DIRECTORY Examples/ DESTINATION share/hb-computers/examples)
else()
    message(STATUS "Qt 6 Widgets not found: building the engines only")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(HB_BUILD_TESTS)
    enable_testing()

    hb_add_suite(lexer_tests hb::expr)
    hb_add_suite(evaluator_tests hb::expr)
    hb_add_suite(scheme_tests hb::scheme)
    hb_add_suite(canvas_queue_tests hb::canvas)

    # The language suite is one binary over every executor, and the shipped
    # examples are a gate as well as a sample: a program that runs and draws
    # nothing is the failure this project treats as the bug, so the test reads
    # the file from the checkout wherever the build tree is.
    add_executable(lang_tests
        tests/lang_tests.cpp
        tests/lang_apl_rexx_smalltalk_tests.cpp
        tests/lang_basic_tests.cpp
        tests/lang_chip8_assembler_tests.cpp
        tests/lang_chip8_machine_tests.cpp
        tests/lang_focal_pilot_comal_tests.cpp
        tests/lang_forth_tests.cpp
        tests/lang_logo_tests.cpp
        tests/lang_repl_tests.cpp
    )
    target_link_libraries(lang_tests PRIVATE hb::lang)
    target_include_directories(lang_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(lang_tests PRIVATE
        TIME_WARP_TEST_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/Examples")
    hb_enable_warnings(lang_tests)
    add_test(NAME lang_tests COMMAND lang_tests)

    hb_add_suite(audio_tests hb::lang)

    # The Kestrel: the processor and the machine it is wired into. The ROMs are
    # assembled first, so these suites run the same bytes the window does.
    # The 65816 assembler is run as the program the build uses, so the bytes in
    # its test are the bytes in the ROM.
    add_executable(kestrel16_asm_tests tests/kestrel16_asm_tests.cpp)
    target_include_directories(kestrel16_asm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(kestrel16_asm_tests PRIVATE
        HB2ASM_PATH="$<TARGET_FILE:kestrel16_asm>")
    add_dependencies(kestrel16_asm_tests kestrel16_asm)
    hb_enable_warnings(kestrel16_asm_tests)
    add_test(NAME kestrel16_asm_tests COMMAND kestrel16_asm_tests)

    hb_add_suite(kestrel_cpu_tests kestrel::machine)
    hb_add_suite(kestrel16_cpu_tests kestrel::machine)
    hb_add_suite(kestrel16_machine_tests kestrel::machine)
    hb_add_suite(kestrel_asm_tests kestrel::machine)
    hb_add_suite(kestrel_machine_tests kestrel::machine)

    if(TARGET hb::ui)
        hb_add_suite(canvas_bridge_tests hb::canvas_qt)
        hb_add_suite(syntax_highlighter_tests hb::ui)

        # The Kestrel is a whole little machine: ROM socket, screen modes, the
        # prompt's direct mode and the native engines behind it. It gets its own
        # binary because a failure should name the machine, not the launcher
        # around it.
        add_executable(retro_computer_tests
            tests/retro_computer_tests.cpp
            tests/retro_basic_tests.cpp
            tests/retro/example_tests.cpp
            tests/retro/paper_tests.cpp
            tests/retro/bbs_tests.cpp
            tests/retro_boot_tests.cpp
            tests/retro/launcher_tests.cpp
            tests/retro_config_tests.cpp
            tests/retro_dos_tests.cpp
            tests/retro_hardware_tests.cpp
            tests/retro_image_tests.cpp
            tests/retro_memory_tests.cpp
        )
        target_link_libraries(retro_computer_tests PRIVATE hb::launcher)
        target_include_directories(retro_computer_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        # One case opens the demo volume shipped in `Examples/hb1`, so the test
        # needs the checkout it is running from.
        target_compile_definitions(retro_computer_tests PRIVATE
            TIME_WARP_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        hb_enable_warnings(retro_computer_tests)
        add_test(NAME retro_computer_tests COMMAND retro_computer_tests)
    endif()
endif()
