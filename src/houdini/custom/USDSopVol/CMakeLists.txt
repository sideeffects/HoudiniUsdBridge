set(PLUGIN_NAME USD_SopVol)

set( sources
    USD_SopVolumeExtract.C
)

add_library(${PLUGIN_NAME} SHARED
    ${sources})

set_target_properties(${PLUGIN_NAME}
    PROPERTIES
    PREFIX "")

target_link_libraries(${PLUGIN_NAME}
    ${PLATFORM_LINK_OPTIONS}
    ${HUSD_LINK_LIBS})

install(TARGETS ${PLUGIN_NAME}
    DESTINATION houdini/dso)
