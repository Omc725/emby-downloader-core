cmake_minimum_required(VERSION 3.16)
project(emby_downloader_core VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

set(CORE_SOURCES
    src/platform.cpp
    src/config.cpp
    src/client.cpp
    src/downloader.cpp
    src/audio_extractor.cpp
    src/queue.cpp
    src/engine.cpp
    src/c_api.cpp
)

set(CORE_HEADERS
    include/emby/models.hpp
    include/emby/platform.hpp
    include/emby/config.hpp
    include/emby/client.hpp
    include/emby/downloader.hpp
    include/emby/audio_extractor.hpp
    include/emby/queue.hpp
    include/emby/engine.hpp
    include/emby/c_api.h
)

# Shared Library
add_library(emby_core SHARED ${CORE_SOURCES})
target_include_directories(emby_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(emby_core PUBLIC CURL::libcurl Threads::Threads)
target_compile_definitions(emby_core PRIVATE EMBY_CORE_EXPORTS)
set_target_properties(emby_core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "emby_core"
)

# Static Library
add_library(emby_core_static STATIC ${CORE_SOURCES})
target_include_directories(emby_core_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(emby_core_static PUBLIC CURL::libcurl Threads::Threads)
set_target_properties(emby_core_static PROPERTIES
    OUTPUT_NAME "emby_core"
)

# Demo Executable
add_executable(headless_demo examples/headless_demo.cpp)
target_link_libraries(headless_demo PRIVATE emby_core_static CURL::libcurl Threads::Threads)
target_include_directories(headless_demo PRIVATE include)

# Install
install(TARGETS emby_core emby_core_static
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/emby DESTINATION include)
