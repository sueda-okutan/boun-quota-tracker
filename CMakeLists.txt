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

# URL of the CORS proxy used by the WebAssembly build (see proxy/). The browser
# blocks a direct POST to BOUN, so the WASM build POSTs here instead. Native
# builds ignore this and talk to BOUN directly.
#
# Defaults to the deployed Cloudflare Worker so the WASM/Pages build works out of
# the box. Override at configure time with
#   -DBOUN_QUOTA_PROXY_URL="https://<your-worker>.workers.dev"
# (the deploy-wasm.yml workflow passes the BOUN_QUOTA_PROXY_URL repo variable,
# which overrides this default when set).
set(BOUN_QUOTA_PROXY_URL "https://boun-quota-proxy.suedanisaokutan.workers.dev"
    CACHE STRING "CORS proxy URL for the WASM build")

if(EMSCRIPTEN)
    target_compile_definitions(boun_quota_tracker PRIVATE BOUN_QUOTA_WASM=1)
    target_compile_definitions(boun_quota_core PRIVATE BOUN_QUOTA_WASM=1)
    set_target_properties(boun_quota_tracker PROPERTIES OUTPUT_NAME "boun_quota_tracker")
    target_link_options(boun_quota_tracker PRIVATE "SHELL:-sALLOW_MEMORY_GROWTH=1")
    if(BOUN_QUOTA_PROXY_URL)
        # Define as a quoted C string literal for QStringLiteral(...) in network.cpp.
        target_compile_definitions(boun_quota_core
            PRIVATE BOUN_QUOTA_PROXY_URL="${BOUN_QUOTA_PROXY_URL}")
    else()
        message(WARNING
            "BOUN_QUOTA_PROXY_URL is not set: the WASM build will POST directly "
            "to BOUN and fail with a CORS error in the browser. Set it to your "
            "deployed proxy URL (see proxy/README.md).")
    endif()
else()
    target_compile_definitions(boun_quota_tracker PRIVATE BOUN_QUOTA_WASM=0)
    target_compile_definitions(boun_quota_core PRIVATE BOUN_QUOTA_WASM=0)
endif()

# Tests are native-only; skip them for the WebAssembly build.
if(NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(tests)
endif()
