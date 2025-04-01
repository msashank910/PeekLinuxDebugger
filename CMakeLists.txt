cmake_minimum_required(VERSION 3.10)
project(PeekLinuxDebugger)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON) 
set(CMAKE_CXX_FLAGS "-Wall -g")

include(FetchContent)
find_package(PkgConfig REQUIRED)

FetchContent_Declare(
    linenoise
    GIT_REPOSITORY https://github.com/antirez/linenoise.git
    GIT_TAG master  
)

FetchContent_Declare(
    libelfin
    GIT_REPOSITORY https://github.com/TartanLlama/libelfin.git
    GIT_TAG fbreg
)

# Download the dependencies
FetchContent_MakeAvailable(linenoise libelfin)

# Calling libelfin Make
execute_process(
    COMMAND make
    WORKING_DIRECTORY ${libelfin_SOURCE_DIR}
)

# Adding Linenoise as Library
add_library(linenoise STATIC ${linenoise_SOURCE_DIR}/linenoise.c)
target_include_directories(linenoise PUBLIC ${linenoise_SOURCE_DIR})


# Set PKG_CONFIG_PATH so pkg-config can find libelfin
set(ENV{PKG_CONFIG_PATH} "${libelfin_SOURCE_DIR}/elf:${libelfin_SOURCE_DIR}/dwarf")

# Find libelfin packages
pkg_check_modules(LIBDWARF REQUIRED libdwarf++)
pkg_check_modules(LIBELF REQUIRED libelf++)

# Create the main executable
file(GLOB SRC_FILES src/*.cpp)
add_executable(pld ${SRC_FILES})

# Include linenoise and libelfin headers
target_include_directories(pld PRIVATE 
    ${linenoise_SOURCE_DIR} 
    ${libelfin_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src 
)

# Link libraries using pkg-config
# Link explicitly to shared libraries (.so)
target_link_libraries(pld PRIVATE linenoise
    ${libelfin_SOURCE_DIR}/dwarf/libdwarf++.so
    ${libelfin_SOURCE_DIR}/elf/libelf++.so)
