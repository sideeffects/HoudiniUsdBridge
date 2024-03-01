# HoudiniUsdBridge
Houdini libraries that must be recompiled to use Houdini with a custom build of
the USD library.

## Building USD on Linux

In order for this to work, there are a few requirements for how the USD library
is built:

1. You must define "_GLIBCXX_USE_CXX11_ABI=0"
2. You must set the compile flag "-std=c++14"

These conditions can be met by editing
USD/cmake/defaults/gccclangshareddefaults.cmake to set these options.
Specifically, the following line:

```
set(_PXR_GCC_CLANG_SHARED_CXX_FLAGS "${_PXR_GCC_CLANG_SHARED_CXX_FLAGS} -std=c++11")
```

Should be replaced by:

```
set(_PXR_GCC_CLANG_SHARED_CXX_FLAGS "${_PXR_GCC_CLANG_SHARED_CXX_FLAGS} -std=c++14")
set(_PXR_GCC_CLANG_SHARED_CXX_FLAGS "${_PXR_GCC_CLANG_SHARED_CXX_FLAGS} -D_GLIBCXX_USE_CXX11_ABI=0")
```

In addition, the boost build used by USD must also set these options, which can
be done by editing the USD/build_scripts/build_usd.py file. Specifically, the
following lines must be added to the b2_settings variable:

```python
'cflags="-fPIC -std=c++14 -D_GLIBCXX_USE_CXX11_ABI=0"'
'cxxflags="-fPIC -std=c++14 -D_GLIBCXX_USE_CXX11_ABI=0"'
```

The OpenSubdiv build must also set these options in the root CMakeLists.txt
file:

```
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_CXX11_ABI=0")
```

This can be a challenge when using the USD build script, as the code is
downloaded and built in a single step. You must either interrupt this build
step to make the change, or make the change after the build, and then force
OpenSubdiv (and then USD) to rebuild with these changes.

## SideFX Changes to the USD Library

The USD library that ships with Houdini is forked from the official USD 22.05
release. Some SideFX-specific changes have since been applied to fix bugs, or
deal with SideFX-specific build issues. Whenever possible we submit pull
requests to have our changes integrated into the next USD release, but
sometimes this isn't possible. And in any case, these changes are not part
of the USD 22.05 release. This section lists every deviation of the SideFX
USD library from Pixar's 22.05 branch, to help you decide which of these
changes you may want or need to incorporate into your own USD build.

Changes ordered from oldest to newest at initial release of Houdini 20.0.506:
- [e65e9f57ea09c4715c0654ac6b50d59b7c58d143](https://github.com/sideeffects/USD/commit/e65e9f57ea09c4715c0654ac6b50d59b7c58d143):
  - **Not required**: These changes are primarily to the CMake system, and 
    relate to the SideFX build system, Particularly around building debug
    versions of USD.
  - This commit also changes the
    way USD searches for plugins (the "install" directory is no searched).
    Reference to the OSL namespace are changed to HOSL (because Houdini
    uses HOSL as the namespace it OSL library). A number of defines are hard
    coded into pxr.h.
  - The usdview script is altered to work with hython. Because this custom
    version of usdview ships in the $HFS/bin directory, there is no need to
    put these changes in your own usdview script.
  - Change the default value for the USD_ABC_XFORM_PRIM_COLLAPSE environment
    variable, which controls how the Alembic plugin exposes Alembic xform prims
    to USD. In Houdini, a default value of false gives results that better match
    how Houdini imports Alembic into SOPs. So this change is not required, but it
    is recommended if you use Alembic files within both USD and SOPs in Houdini.
- [a98d3a0e00ab1046113ff0c23e8d2eafa1b83849](https://github.com/sideeffects/USD/commit/a98d3a0e00ab1046113ff0c23e8d2eafa1b83849):
  - **Not required**: Register velocities, acclerations, and local constant-interpolation
    primvars on point instancer primitives as hydra-accessible primvars. This change is
    submitted as a PR that has been partially merged (https://github.com/PixarAnimationStudios/USD/pull/1731).
- [da8f7ea391bf1396536868beefefe72796534712](https://github.com/sideeffects/USD/commit/da8f7ea391bf1396536868beefefe72796534712):
  - **Not required**: Continuation of above commit for data sharing ids, adding
    support for this API to the scene index emulation layer.
- [319f6613180af71a179d823d34773761e2e1cd13](https://github.com/sideeffects/USD/commit/319f6613180af71a179d823d34773761e2e1cd13):
  - **Not required**: Fix a number of low level file handling methods, particularly
    on Windows. Skip all permissions checks prior to attempting to create or write
    files (such checks often fail on Windows network drives). Ignore reparese points
    on network drives (since they can't be resolved accurately).
  - Set umask properly when creating USD file
    (unmerged PR https://github.com/PixarAnimationStudios/USD/issues/2933).
- [9a803b47572f8fae4ab9d864945e761e41d555c4](https://github.com/sideeffects/USD/commit/9a803b47572f8fae4ab9d864945e761e41d555c4):
  - **Not Required**: Invalidate CoordSys hydra representations when their target
    prim xform changes.
- [018be3615bfdc5596998b7a3ffc79864dea06a74](https://github.com/sideeffects/USD/commit/018be3615bfdc5596998b7a3ffc79864dea06a74):
  - **Not Required**: Added a Houdini-specific environment
    variable to override the default texture file used for viewport-only dome lights.
- [9b00a950c00648064ae88a97e77d5bd9f76f8310](https://github.com/sideeffects/USD/commit/9b00a950c00648064ae88a97e77d5bd9f76f8310):
  - **Not Required**: Allow instancing of bounding boxes drawn on prims with unloaded payloads
    (PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2408 merged after release).
- [2e79bf1c7d3276935a0ebd783be9403feaa01453](https://github.com/sideeffects/USD/commit/2e79bf1c7d3276935a0ebd783be9403feaa01453):
  - **Not Required**: Prevent exceptions loading USD python modules when the PATH variable
    contains a relative path (PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2409 merged after release).
- [3db5c57e119d94dc22096d948ed407feef1f7f33](https://github.com/sideeffects/USD/commit/3db5c57e119d94dc22096d948ed407feef1f7f33):
  - **Not Required**: Update to handling of assetPath array data when UsdUtilsModifyAssetPaths
    leaves an array empty (should be superceded by
    https://github.com/PixarAnimationStudios/OpenUSD/commit/5b9a59df49187f71b23ac2cae5a2c0c6baf1b6e2).
- [ae0aff98eef15e2697aeffa7a36b8dac857061f7](https://github.com/sideeffects/USD/commit/ae0aff98eef15e2697aeffa7a36b8dac857061f7):
  - **Not required**: Fix VtArray declaraions of extern template instantiations so that the code
    in array.cpp will actually instantiate and export the templates as expected.
    Currently the templates are marked as extern even while compiling array.cpp
    so the compiler isn't actually instantiating anything
    (PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2415 merged after release).
- [a0af4c6696f332abeff87d3872902b744738f86f](https://github.com/sideeffects/USD/commit/a0af4c6696f332abeff87d3872902b744738f86f):
  - **Not required**: Support asset type primvars on native instances
    (PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2521 merged after release).
- [7a97c11d690f04c437cb5de001ac6d007afd720d](https://github.com/sideeffects/USD/commit/7a97c11d690f04c437cb5de001ac6d007afd720d):
  - **Not required**: Remove the DrawModeAdapter that ships with USD. Houdini provides its
    own version of this optimized for use with HGL. Having both installed
    leads to warnings on startup (and could lead to the use of the wrong one).
- [0769436fd20d5b6ad8678b16f24f28767b7bbe8a](https://github.com/sideeffects/USD/commit/0769436fd20d5b6ad8678b16f24f28767b7bbe8a):
  - **Required**: Make GlfSimpleLight more controllable, but hide these extra controls behind
    a flag so that non-Houdini applications won't be affected by the addition
    of these new parameters.

The following changes were incorporated into Houdini 20.0.609:
- [aa00e7d45fd86a712658bdc2982f77d2a15c5b20](https://github.com/sideeffects/USD/commit/aa00e7d45fd86a712658bdc2982f77d2a15c5b20):
  - **Not required**: Fix a potential crash when updating skinned primitives
    (PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2931 not yet merged).

The following changes were incorporated into Houdini 20.0.620:
- [a4c3d1035a83be6d2a81392c347ef0028962947c](https://github.com/sideeffects/USD/commit/a4c3d1035a83be6d2a81392c347ef0028962947c):
  - **Not required**: Improve performance of stage recomposition when adding or removing sublayers
    (PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2937 not yet merged).
- [a9ca92696b8c18520a1fd03b43b35477ae8c75e6](https://github.com/sideeffects/USD/commit/a9ca92696b8c18520a1fd03b43b35477ae8c75e6):
  - **Not required**: Build change specific to Houdini's USD build requirements.
- [bec511a8219a47845c2a2c4d60f3d977d646ca49](https://github.com/sideeffects/USD/commit/bec511a8219a47845c2a2c4d60f3d977d646ca49):
  - **Not required**: Fix incorrectly transformed points for GPU blendshapes with no skinning
    (PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2696 not yet merged).
- [9afc40205cda31a7ae11a6355932297398c53d6b](https://github.com/sideeffects/USD/commit/9afc40205cda31a7ae11a6355932297398c53d6b):
  - **Not required**: Fix crashes when using pxr.Pcp.NodeRef objects pointing to "null" C++ PcpNodeRef objects
    (PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2943 not yet merged).
- [662bfcd8f207c5a03f04d707a26e9402b74e9e68](https://github.com/sideeffects/USD/commit/662bfcd8f207c5a03f04d707a26e9402b74e9e68):
  - **Not required**: Make the Alembic file format plugin respect "dccfps" metadata authored to the Alembic file
    (PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2944 not yet merged).
- [8a04ce970cc9a01eeac596697947e6557a50c0c4](https://github.com/sideeffects/USD/commit/8a04ce970cc9a01eeac596697947e6557a50c0c4):
  - **Not required**: Interpolate VtValue's containing GfMatrix*f types in HdResampleNeighbors
    (part of PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2956 not yet merged).
- [b08c877b755a8db4c0588105b1abb553cf88c6be](https://github.com/sideeffects/USD/commit/b08c877b755a8db4c0588105b1abb553cf88c6be):
  - **Not required**: Fix an issue where an unexpected time sample could be reported for skinning computations
    (part of PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2956 not yet merged).
- [967195bdae3f23fccf6d20e2a505fdfc99658803](https://github.com/sideeffects/USD/commit/967195bdae3f23fccf6d20e2a505fdfc99658803):
  - **Not required**: Eliminate DecprecationWarning caused by an invalid escape sequence in a regex pattern in usdview
    (PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2955 not yet merged).

The following changes were incorporated into Houdini 20.0.625:
- [aa001af6a76279594bbe0bcd3cac2e1bb316873f](https://github.com/sideeffects/USD/commit/aa001af6a76279594bbe0bcd3cac2e1bb316873f):
  - **Not required**: Fix a crash that could occur when selecting or highlighting native instances
    (PR https://github.com/PixarAnimationStudios/OpenUSD/pull/2960 not yet merged).

## Building Houdini libraries

Once you have built USD, it is time to build the replacement libHoudiniUSD.so
(libHUSD.dll and libgusd.dll on Windows), USD_Plugins.so, and other libraries.
You must use CMake to build these libraries. CMake version 3.12 is required
(due to the use of add_compile_definitions).

The following variables are used to configure the CMake build:

* HOUDINI_PATH: The path to the root of the Houdini install. If this is
  omitted, the $HFS environment variable will be checked as well.
* USD_ROOT: The path to the USD install.
* USD_LIB_PREFIX: The naming prefix of the USD libraries to build/link against.
  This should match the value of the `PXR_LIB_PREFIX` CMake variable used to
  build USD, and defaults to "lib" (which matches the USD build default).
* BOOST_NAMESPACE: The namespace of the Boost build used by the external USD
  build. This defaults to "boost".
* COPY_HOUDINI_USD_PLUGINS: Whether to copy the $HH/dso/usd_plugins directory
  from the Houdini install into the HoudiniUsdBridge install tree. This defaults  to ON.

Once the libraries are built, run `make install`. This will copy the bridge libraries
into the installation directory structure (defaults to `/usr/local`, but this
location can be overridden by running `make DESTDIR=/path/to/install install`).

## Running Houdini

To run Houdini using the HoudiniUsdBridge libraries, the following environment variables
must be set so that Houdini uses the bridge libraries and plugins instead of the ones that
ship with Houdini.

```
# The install location of your custom USD library.
export USD_ROOT=/path/to/USD
# The install location of the HoudiniUsdBridge libraries.
export BRIDGE_ROOT=/path/to/install/usr/local

# Ensure the bridge libHoudiniUSD.so library and the dummy libpxr_* libraries are used.
export LD_LIBRARY_PATH=$BRIDGE_ROOT/dsolib
# Give priority to the custom USD and bridge python libraries.
export PYTHONPATH=$USD_ROOT/lib/python:$BRIDGE_ROOT/houdini/python2.7libs
# Add the bridge houdini directory to the HOUDINI_PATH so that Houdini plugins will be
# loaded from here.
export HOUDINI_PATH=$BRIDGE_ROOT/houdini:\&
# Houdini will tell the USD library to load plugins explicitly from this directory.
# The default value of this variable loads USD plugins from the dso/usd_plugins subdirectory
# of every HOUDINI_PATH entry. But we explicitly don't want USD to try to load the plugins
# in $HFS/houdini/dso/usd_plugins.
export HOUDINI_USD_DSO_PATH=$BRIDGE_ROOT/houdini/dso/usd_plugins
# Tell Houdini to not load the USD related plugins that ship with Houdini.
export HOUDINI_DSO_EXCLUDE_PATTERN="{$HH/dso/USD_Ops.so} {$HH/dso/USD_SopVol.so}"
```

Note that using these environment variables, the actual Houdini installation does not need to
be modified at all. And removing the environment variables will therefore return Houdini to its
native state, using the built-in USD library.

## Other Considerations

The Houdini USD build has a number of directories automatically added to the
USD plugin path ($HDSO/usd_plugins and $HH/dso/usd_plugins). You may need to
add these explicitly to your USD plugin path environment variable to ensure
that the Houdini USD plugins are loaded by your USD build.

Always build the HoudiniUsdBridge in Release mode. Even RelWithDebInfo has been
reported to cause crashes and other bad behavior.

## Acknowledgements

The USD library on which these libraries are built is created by Pixar:
https://github.com/PixarAnimationStudios/USD
SideFX also maintains a fork of this repository:
https://github.com/sideeffects/USD

The code in the 'src/houdini/lib/H_USD/gusd' directory of this repository
began as a direct copy of the 'third_party/houdini/lib/gusd' directory from
this USD repository, and so still contains the original Pixar copyright
notices.
