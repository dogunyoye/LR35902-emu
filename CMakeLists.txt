cmake_minimum_required(VERSION 3.16)

project(LR35902-emu
    VERSION 0.1.0
    DESCRIPTION "Sharp LR35902 (Game Boy) CPU emulator"
    LANGUAGES CXX
)

# Global compile options: warnings + best-effort ASAN/UBSAN in Debug builds
add_library(lr35902_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lr35902_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
    )
    target_link_options(lr35902_warnings INTERFACE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
elseif(MSVC)
    target_compile_options(lr35902_warnings INTERFACE /W4)
endif()

# Emulator core library (static for simple linking into future exe/tests)
add_library(lr35902 STATIC src/Memory.cpp src/LR35902.cpp src/Timer.cpp src/Cartridge.cpp src/PPU.cpp src/Serial.cpp)
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

add_executable(rom_test_runner tests/rom_test_runner.cpp)
target_link_libraries(rom_test_runner PRIVATE lr35902 lr35902_warnings)

find_package(SDL2 REQUIRED)

# Placeholder executable target — add main.cpp under src/ to enable.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
    add_executable(lr35902-emu src/main.cpp)
    target_link_libraries(lr35902-emu PRIVATE lr35902 SDL2::SDL2)
endif()
