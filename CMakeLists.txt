cmake_minimum_required(VERSION 3.20)

project(boun_quota_tracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

qt_standard_project_setup()

# Parsing + POST-body construction live in a small static library so the test
# target can link them without pulling in the GUI entry point.
qt_add_library(boun_quota_core STATIC
    src/models.h
    src/parser.h
    src/parser.cpp
    src/network.h
    src/network.cpp
    src/logic.h
    src/logic.cpp
)

target_link_libraries(boun_quota_core
    PUBLIC
        Qt6::Core
        Qt6::Network
)

target_include_directories(boun_quota_core PUBLIC src)

qt_add_executable(boun_quota_tracker
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
)

target_link_libraries(boun_quota_tracker
    PRIVATE
        boun_quota_core
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
)

if(EMSCRIPTEN)
    target_compile_definitions(boun_quota_tracker PRIVATE BOUN_QUOTA_WASM=1)
    target_compile_definitions(boun_quota_core PRIVATE BOUN_QUOTA_WASM=1)
    set_target_properties(boun_quota_tracker PROPERTIES OUTPUT_NAME "boun_quota_tracker")
    target_link_options(boun_quota_tracker PRIVATE "SHELL:-sALLOW_MEMORY_GROWTH=1")
else()
    target_compile_definitions(boun_quota_tracker PRIVATE BOUN_QUOTA_WASM=0)
    target_compile_definitions(boun_quota_core PRIVATE BOUN_QUOTA_WASM=0)
endif()

# Tests are native-only; skip them for the WebAssembly build.
if(NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(tests)
endif()
