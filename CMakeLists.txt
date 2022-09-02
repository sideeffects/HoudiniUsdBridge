# Contributed by Nathan Rusch, Luma Pictures
cmake_minimum_required(VERSION 3.12)

project(HoudiniUsdBridge)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
set(HOUDINI_PATH ""
    CACHE STRING
    "The path to the Houdini installation. This is optional if the HFS environment variable is set.")
set(BOOST_NAMESPACE "boost"
    CACHE STRING
    "The namespace of the Boost build you are using with USD.")
set(USD_LIB_PREFIX "lib"
    CACHE STRING
    "The name prefix of the USD libraries to build/link against.")
option(COPY_HOUDINI_USD_PLUGINS "Copy $HH/dso/usd_plugins from Houdini to the project installation directory" ON)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

# Find Houdini
if (WIN32)
	find_path(HOUDINI_ROOT custom/houdini/dsolib/libFUSE.lib
		HINTS ${HOUDINI_PATH} ENV HFS
		NO_DEFAULT_PATH)
	set(PLATFORM_CXX_OPTIONS "")
	set(PLATFORM_LINK_OPTIONS "")
else()
    find_path(HOUDINI_ROOT dsolib/libHoudiniAPPS3${CMAKE_SHARED_LIBRARY_SUFFIX}
		HINTS ${HOUDINI_PATH} ENV HFS
		NO_DEFAULT_PATH)
	set(PLATFORM_CXX_OPTIONS
        -Wno-missing-field-initializers
        -Wno-unused-function)
	set(PLATFORM_LINK_OPTIONS "-Wl,--exclude-libs,ALL")
endif()

find_path(HOUDINI_INCLUDE_DIR HAPI/HAPI_Version.h
    HINTS ${HOUDINI_PATH} ENV HFS
    PATH_SUFFIXES toolkit/include
    NO_DEFAULT_PATH)

if (NOT (HOUDINI_ROOT AND HOUDINI_INCLUDE_DIR))
    message(FATAL_ERROR
        "Could not find Houdini dirs (HOUDINI_PATH: \"${HOUDINI_PATH}\", HFS: \"$ENV{HFS}\")")
endif()

message(STATUS "Found Houdini at ${HOUDINI_ROOT}")

# Find other dependencies
find_package(USD REQUIRED)

find_package(Boost
    COMPONENTS python
    REQUIRED)

find_package(PythonInterp 3.9 REQUIRED)
find_package(PythonLibs 3.9 REQUIRED)

add_subdirectory(src)
