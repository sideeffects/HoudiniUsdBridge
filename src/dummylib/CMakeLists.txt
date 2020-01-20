# Many of Houdini's executables link directly against USD libraries, even if
# they don't actually use them. Thus, in order to prevent these executables from
# pulling Houdini's distributed USD libraries into the process space at runtime,
# we create empty shim libraries with the same names and make the executables
# load those instead. These shim libraries in turn link against the *actual* USD
# libraries we're building against.

foreach(libname ${usd_LINK_LIB_NAMES})
    # Prepend the "pxr_" name prefix used for Houdini's shipped USD libraries.
    set(dummy_libname pxr_${libname})

    add_library(${dummy_libname} SHARED
        dummy.C)

    # Link against the matching USD component library being built against, so
    # e.g. the shim `libpxr_usd.so` links against the `libusd.so` from the
    # custom USD build, accounting for a custom USD library name prefix if one
    # was passed.
    target_link_libraries(${dummy_libname}
        ${PLATFORM_LINK_OPTIONS}
        "${usd_LIB_LINK_PREFIX}${libname}")

    install(TARGETS ${dummy_libname}
        DESTINATION dsolib)
endforeach()
