cmake_minimum_required(VERSION 3.16)

project(LR35902-emu
    VERSION 0.1.0
    DESCRIPTION "Sharp LR35902 (Game Boy) CPU emulator"
    LANGUAGES CXX
)

# Default to an optimised build; the emulator needs it to run at full speed.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Global compile options: warnings + best-effort ASAN/UBSAN in Debug builds
add_library(lr35902_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lr35902_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer -fsanitize=address,undefined>
    )
    target_link_options(lr35902_warnings INTERFACE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
elseif(MSVC)
    target_compile_options(lr35902_warnings INTERFACE /W4)
endif()

# Emulator core library (static for simple linking into future exe/tests)
add_library(lr35902 STATIC src/Memory.cpp src/LR35902.cpp src/Timer.cpp src/Cartridge.cpp src/PPU.cpp src/Serial.cpp src/APU.cpp src/SaveState.cpp src/CgbCompatibility.cpp)
target_compile_features(lr35902 PUBLIC cxx_std_20)
target_include_directories(lr35902 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lr35902 PRIVATE lr35902_warnings)

enable_testing()
function(add_emulator_test target source_file)
    add_executable(${target} ${source_file})
    target_link_libraries(${target} PRIVATE lr35902 lr35902_warnings)
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_emulator_test(cpu_tests tests/cpu_tests.cpp)
add_emulator_test(timer_tests tests/timer_tests.cpp)
add_emulator_test(cartridge_tests tests/cartridge_tests.cpp)
add_emulator_test(memory_tests tests/memory_tests.cpp)
add_emulator_test(joypad_tests tests/joypad_tests.cpp)
add_emulator_test(serial_tests tests/serial_tests.cpp)
add_emulator_test(ppu_tests tests/ppu_tests.cpp)
add_emulator_test(apu_tests tests/apu_tests.cpp)
add_emulator_test(save_state_tests tests/save_state_tests.cpp)

add_executable(rom_test_runner tests/rom_test_runner.cpp)
target_link_libraries(rom_test_runner PRIVATE lr35902 lr35902_warnings)

find_package(SDL2 REQUIRED)

# Key binding parsing uses SDL's key names.
add_emulator_test(controls_tests tests/controls_tests.cpp)
target_sources(controls_tests PRIVATE src/Controls.cpp)
target_link_libraries(controls_tests PRIVATE SDL2::SDL2)
# Keep our own main() rather than SDL's renaming it to SDL_main on Windows,
# which would need SDL2main.
target_compile_definitions(controls_tests PRIVATE SDL_MAIN_HANDLED)

# Placeholder executable target — add main.cpp under src/ to enable.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
    add_executable(lr35902-emu src/main.cpp src/Controls.cpp)
    target_include_directories(lr35902-emu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(lr35902-emu PRIVATE lr35902 lr35902_warnings SDL2::SDL2)
    target_compile_definitions(lr35902-emu PRIVATE SDL_MAIN_HANDLED)
endif()
