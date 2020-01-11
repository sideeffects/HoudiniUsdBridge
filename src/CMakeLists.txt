# Add the base project definitions

add_compile_options(
    -DBUILDING_HOUDINIUSD
    -DBOOST_NS=${BOOST_NAMESPACE}
    -DHUSD_EXPORTS
    -DGUSD_EXPORTS
    -DUSDDUMMY_EXPORTS
    -DCXX11_ENABLED=1
    -DMAKING_DSO
    ${PLATFORM_CXX_OPTIONS})

# Add the compiler flags and definitions from the Houdini plugin toolkit
include(${HOUDINI_ROOT}/toolkit/cmake/HoudiniConfig.cmake)

add_compile_options(${_houdini_compile_options})
add_compile_definitions(${_houdini_defines})

# Libs we will link with from the Houdini install
if (WIN32)
    set(houdini_LINK_LIBS
	libHOMUI
	libFUSE
	libMT
	libMDS
	libJEDI
	libOP3D
	libDM
	libVISF
	libGUI
	libGR
	libSHOP
	libVOP
	libVCC
	libPI
	libOP
	libPRM
	libCMD
	libCH
	libDEP
	libUT
	libVM
	libSYS
	libtools
	hboost_system-mt
	tbb
	tbbmalloc
	libEXPR
	libFS
	libcurlwrap
	libHOM
	libPY
	libPXL
	libCL
	libARR
	libLM
	libTAKE
	libIMG
	libTBF
	libDD
	libCVEX
	libPBR
	libVEX
	libTIL
	libhptex
	libIMG3D
	libGVEX
	libGT
	libGU
	libGSTY
	libSTY
	libGOP
	libGEO
	libGP
	libGD
	libGA
	libCE
	libTS
	libBV
	libKIN
	libGQ
	libUI
	libAU
	libRE
	libFONT
	libHARD
	libHAPIL
	libPYP
	libBM
	libFUI
	libSTORUI
	libIPR
	libOPUI
	libPSI2
	libSI
	libBR
	libSS
	libCHOP
	libOH
	libMOT
	libMGR
	libDOPZ
	libSIMZ
	libOBJ
	libSOP
	libSOPTG
	libDOP
	libWIRE
	libCLO
	libSIM
	libGAS
	libRBD
	libGDT
	libPDG
	libPDGT
	libSOPZ
	libFBX
	libDAE
	libCOP2
	libRU
	libTOP
	libROP
	libSOHO
	libLOP
	libHUSD
	libgusd
	libPDGD
	libCOPZ
	libCHOPZ
	libCHOPNET
	libCOPNET
	libVOPNET
	libIMGUI
	libGLTF
	libSHLFUI
	libSHLF
	libGABC
	libVIS
	libCV
	libSTOR
	libMH
	libMCS
	libMWS
	libMPI
	libMSS
	libMLS
	libIMS
	libIMP
	libIMH
	libIM
	libMATUI
	libJIVE
	libCHUI
	libTHOR
	libMIDI
	libDTUI
	libHOMF
	libRAY
	libVPRM
	libVGEO
	libBRAY)

else()
    set(houdini_LINK_LIBS
	HoudiniGEO
	HoudiniOP1
	HoudiniOP2
	HoudiniOP3
	HoudiniOPZ
	HoudiniPRM
	HoudiniSIM
	HoudiniUI
	HoudiniUT
	openvdb_sesi
	tbb)
endif()


# Libs we will link with from the external USD install
set(usd_LINK_LIB_NAMES
    ar
    arch
    garch
    gf
    glf
    hd
    hdx
    hf
    kind
    ndr
    pcp
    plug
    pxOsd
    sdf
    sdr
    tf
    trace
    usd
    usdGeom
    usdHydra
    usdImaging
    usdImagingGL
    usdLux
    usdRi
    usdShade
    usdSkel
    usdSkelImaging
    usdUtils
    usdVol
    vt
    work)

# The complete set of libs we need to link against
set(HUSD_LINK_LIBS ${houdini_LINK_LIBS})

# Strip any platform-specific library prefix from USD_LIB_PREFIX to get the
# library name prefix to use in linker commands.
set(usd_LIB_LINK_PREFIX ${USD_LIB_PREFIX})
if (CMAKE_SHARED_LIBRARY_PREFIX AND USD_LIB_PREFIX MATCHES "^${CMAKE_SHARED_LIBRARY_PREFIX}.*")
    string(LENGTH ${CMAKE_SHARED_LIBRARY_PREFIX} _lib_prefix_length)
    string(SUBSTRING ${USD_LIB_PREFIX} ${_lib_prefix_length} -1 usd_LIB_LINK_PREFIX)
endif()

# Convert base USD library names to their actual names based on the prefix
# computed above, and append the qualified names to HUSD_LINK_LIBS.
foreach (libname ${usd_LINK_LIB_NAMES})
    list(APPEND HUSD_LINK_LIBS "${usd_LIB_LINK_PREFIX}${libname}")
endforeach()

include_directories(
    ${USD_INCLUDE_DIR}
    houdini/lib/H_USD
    ${Boost_INCLUDE_DIR}
    ${PYTHON_INCLUDE_DIRS}
    ${HOUDINI_INCLUDE_DIR})

if (WIN32)
	link_directories(${USD_LIBRARY_DIR} ${HOUDINI_ROOT}/custom/houdini/dsolib ${Boost_LIBRARY_DIRS})
else()
	link_directories(${USD_LIBRARY_DIR} ${HOUDINI_ROOT}/dsolib ${Boost_LIBRARY_DIRS})
endif()

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_subdirectory(houdini/custom/USD)
add_subdirectory(houdini/custom/USDFS)
add_subdirectory(houdini/custom/USDOP)
add_subdirectory(houdini/lib/H_USD/HUSD)
add_subdirectory(houdini/lib/H_USD/HUSD/UsdHoudini)
add_subdirectory(dummylib)

if (COPY_HOUDINI_USD_PLUGINS)
    install(DIRECTORY ${HOUDINI_ROOT}/houdini/dso/usd_plugins
        DESTINATION houdini/dso)
endif()
