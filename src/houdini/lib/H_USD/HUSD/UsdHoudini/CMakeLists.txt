# For help with CMake, see: $S/cmake/Help/H_Help.cmake
# Please copy this header in all new CMakeLists.txt files.

set(PLUGIN_NAME _usdHoudini)

include(${CMAKE_CURRENT_LIST_DIR}/CMakeSources.cmake)

add_library(${PLUGIN_NAME} SHARED
    ${sources})

target_include_directories(${PLUGIN_NAME} BEFORE
    PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(${PLUGIN_NAME}
    ${PLATFORM_LINK_OPTIONS}
    ${HUSD_LINK_LIBS})

set_target_properties(${PLUGIN_NAME} PROPERTIES PREFIX "")

install(TARGETS ${PLUGIN_NAME}
    DESTINATION houdini/python3.9libs)

