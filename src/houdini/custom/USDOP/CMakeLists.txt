set(PLUGIN_NAME USD_Ops)

include(${CMAKE_CURRENT_LIST_DIR}/CMakeSources.cmake)

add_library(${PLUGIN_NAME} SHARED
    ${sources})

# Code generation for embedded DS files.
houdini_generate_proto_headers( FILES ${protoc_sources} )

# Include ${CMAKE_CURRENT_BINARY_DIR} for the generated header.
target_include_directories( ${PLUGIN_NAME} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

set_target_properties(${PLUGIN_NAME}
    PROPERTIES
    PREFIX "")

target_link_libraries(${PLUGIN_NAME}
    ${PLATFORM_LINK_OPTIONS}
    ${HUSD_LINK_LIBS})

install(TARGETS ${PLUGIN_NAME}
    DESTINATION houdini/dso)
