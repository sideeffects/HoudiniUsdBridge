set(PLUGIN_NAME pxr_all)

add_library(${PLUGIN_NAME} SHARED
    dummy.C)

target_link_libraries(${PLUGIN_NAME}
    ${PLATFORM_LINK_OPTIONS}
    ${HUSD_LINK_LIBS})

install(TARGETS ${PLUGIN_NAME}
    DESTINATION dsolib)
