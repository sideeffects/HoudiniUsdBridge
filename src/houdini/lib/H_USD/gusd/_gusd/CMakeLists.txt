# For help with CMake, see: $S/cmake/Help/H_Help.cmake
# Please copy this header in all new CMakeLists.txt files.

if (WIN32)
	set(PLUGIN_NAME gusd)
else ()
	set(PLUGIN_NAME _gusd)
endif()

set( sources
    ../module.cpp
    ../wrapStageCache.cpp
    ../wrapStageEdit.cpp
    ../wrapStageOpts.cpp
)

add_library(${PLUGIN_NAME} SHARED
    ${sources})

target_include_directories(${PLUGIN_NAME} BEFORE
    PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(${PLUGIN_NAME}
    ${PLATFORM_LINK_OPTIONS}
    ${HUSD_LINK_LIBS})

set_target_properties(${PLUGIN_NAME} PROPERTIES PREFIX "lib")

install(TARGETS ${PLUGIN_NAME}
    DESTINATION python/lib/python3.9/site-packages/pxr/Gusd)

