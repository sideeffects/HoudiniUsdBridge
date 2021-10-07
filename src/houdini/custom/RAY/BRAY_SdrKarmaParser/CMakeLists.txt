set(PLUGIN_NAME BRAY_SdrKarmaParser)

include(${CMAKE_CURRENT_LIST_DIR}/CMakeSources.cmake)

add_library(${PLUGIN_NAME} SHARED
    ${sources})

set_target_properties(${PLUGIN_NAME}
    PROPERTIES
    PREFIX "")

target_link_libraries(${PLUGIN_NAME}
    ${PLATFORM_LINK_OPTIONS}
    ${KARMA_LINK_LIBS}
    ${HUSD_LINK_LIBS})

install(TARGETS ${PLUGIN_NAME}
    DESTINATION houdini/dso/usd)
