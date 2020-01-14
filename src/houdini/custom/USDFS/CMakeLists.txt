set(PLUGIN_NAME USD_FS)

add_library(${PLUGIN_NAME} SHARED
    FS_UsdzReaderHelper.C)

set_target_properties(${PLUGIN_NAME}
    PROPERTIES
    PREFIX "")

target_link_libraries(${PLUGIN_NAME}
    ${PLATFORM_LINK_OPTIONS}
    ${HUSD_LINK_LIBS})

install(TARGETS ${PLUGIN_NAME}
    DESTINATION houdini/dso)
