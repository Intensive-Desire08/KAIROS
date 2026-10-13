cmake_minimum_required(VERSION 3.16)
project(PseudoQuantumEntropy VERSION 1.0 LANGUAGES CXX)

# ============================================================================
# Compiler and Standard Settings
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Compiler Warnings
# ============================================================================

if(MSVC)
    add_compile_options(
        /W4
        /WX-
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
    add_compile_options(
        /wd4251
        /wd4275
        /wd4996
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wno-unused-parameter
    )
endif()

# ============================================================================
# Build Type Configuration
# ============================================================================

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING 
        "Choose the type of build: Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# Dependencies - vcpkg will handle these in manifest mode
# ============================================================================

find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

# ============================================================================
# Include Directories
# ============================================================================

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# ============================================================================
# Source Files
# ============================================================================

set(SOURCES
    src/main.cpp
    src/Logger.cpp
    src/Config.cpp
    src/EntropyCollector.cpp
    src/EntropyPool.cpp
    src/WebServer.cpp
    src/entropy/SerialEntropySource.cpp
    src/entropy/OpenSSLEntropySource.cpp
    src/crypto/KeyGenerator.cpp
    src/crypto/Encryptor.cpp
    src/crypto/Hasher.cpp
    src/utilities/Helpers.cpp
)

# ============================================================================
# Header Files (for IDE visibility, not compiled)
# ============================================================================

set(HEADERS
    src/Logger.h
    src/Config.h
    src/EntropyCollector.h
    src/EntropyPool.h
    src/WebServer.h
    src/entropy/IEntropySource.h
    src/entropy/SerialEntropySource.h
    src/entropy/OpenSSLEntropySource.h
    src/crypto/KeyGenerator.h
    src/crypto/Encryptor.h
    src/crypto/Hasher.h
    src/utilities/Helpers.h
)

# ============================================================================
# Executable
# ============================================================================

add_executable(PseudoQuantumEntropy ${SOURCES} ${HEADERS})

# ============================================================================
# Link Libraries
# ============================================================================

target_link_libraries(PseudoQuantumEntropy
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    nlohmann_json::nlohmann_json
)

# Windows specific libraries
if(WIN32)
    target_link_libraries(PseudoQuantumEntropy
        ws2_32
        crypt32
        advapi32
        user32
        shell32
    )
endif()

# Linux specific libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(PseudoQuantumEntropy
        pthread
        dl
    )
endif()

# macOS specific libraries
if(APPLE)
    target_link_libraries(PseudoQuantumEntropy
        pthread
    )
endif()

# ============================================================================
# Platform-specific Definitions
# ============================================================================

if(WIN32)
    target_compile_definitions(PseudoQuantumEntropy PRIVATE
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0A00
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Enable OpenSSL support for cpp-httplib
target_compile_definitions(PseudoQuantumEntropy PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
)

if(UNIX)
    target_compile_definitions(PseudoQuantumEntropy PRIVATE
        _GNU_SOURCE
    )
endif()

# ============================================================================
# Properties
# ============================================================================

set_target_properties(PseudoQuantumEntropy PROPERTIES
    OUTPUT_NAME "PseudoQuantumEntropy"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Enable debugging symbols in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        target_compile_options(PseudoQuantumEntropy PRIVATE /Zi /Od)
        target_link_options(PseudoQuantumEntropy PRIVATE /DEBUG)
    else()
        target_compile_options(PseudoQuantumEntropy PRIVATE -g -O0)
    endif()
endif()

# Enable optimization in Release mode
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(PseudoQuantumEntropy PRIVATE /O2 /MT)
    else()
        target_compile_options(PseudoQuantumEntropy PRIVATE -O3 -DNDEBUG)
    endif()
endif()

# ============================================================================
# Tests (Optional - Add later)
# ============================================================================

enable_testing()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()

# ============================================================================
# Build Information
# ============================================================================

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/build_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/build_config.h
    @ONLY
)

# ============================================================================
# Print Build Configuration Summary
# ============================================================================

message(STATUS "")
message(STATUS "========================================")
message(STATUS "PseudoQuantumEntropy Build Configuration")
message(STATUS "========================================")
message(STATUS "CMake version:     ${CMAKE_VERSION}")
message(STATUS "Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:          ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Platform:          ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  Boost:           ${Boost_INCLUDE_DIRS}")
message(STATUS "  OpenSSL:         ${OPENSSL_INCLUDE_DIR}")
message(STATUS "  nlohmann_json:   ${nlohmann_json_INCLUDE_DIR}")
message(STATUS "  cpp-httplib:     Header-only (include/httplib.h)")
message(STATUS "")
message(STATUS "Output directory:  ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "========================================")
message(STATUS "")
