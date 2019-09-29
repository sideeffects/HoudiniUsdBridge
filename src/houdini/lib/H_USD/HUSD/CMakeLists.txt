if (WIN32)
	set(PLUGIN_NAME HUSD)
else()
	set(PLUGIN_NAME HoudiniUSD)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/CMakeSources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../gusd/CMakeSources.cmake)

list(TRANSFORM gusd_sources PREPEND ${CMAKE_CURRENT_LIST_DIR}/../gusd/)

add_library(${PLUGIN_NAME} SHARED
    ${husd_sources}
    ${gusd_sources})

target_include_directories(${PLUGIN_NAME} BEFORE
    PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(${PLUGIN_NAME}
    ${PLATFORM_LINK_OPTION}
    ${HUSD_LINK_LIBS})

set_target_properties(${PLUGIN_NAME} PROPERTIES PREFIX "lib")

install(TARGETS ${PLUGIN_NAME}
    DESTINATION dsolib)
