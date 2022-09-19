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

Changes ordered from oldest to newest:

- [5e18d7e99dc79968e55c52999f6bb2d63940a736](https://github.com/sideeffects/USD/commit/5e18d7e99dc79968e55c52999f6bb2d63940a736):
  - **Not required**: These changes are primarily to the CMake system, and 
    relate to the SideFX build system, Particularly around building debug
    versions of USD.
  - This commit also changes the
    way USD searches for plugins (the "install" directory is no searched).
    Reference to the OSL namespace are changed to HOSL (because Houdini
    uses HOSL as the namespace it OSL library). A number of defines are hard
    coded into pxr.h.
  - The standard draw mode adapter is removed from the associated plugInfo.json
    file. This is because Houdini ships with its own draw mode adapter. Using
    the standard adapter will still work in Houdini, but HoudiniGL will be
    slower to draw bounding boxes for unloaded payloads or when drawing prims
    with the Bounds draw mode.
  - The usdview script is altered to work with hython. Because this custom
    version of usdview ships in the $HFS/bin directory, there is no need to
    put these changes in your own usdview script.
  - Change the default value for the USD_ABC_XFORM_PRIM_COLLAPSE environment
    variable, which controls how the Alembic plugin exposes Alembic xform prims
    to USD. In Houdini, a default value of false gives results that better match
    how Houdini imports Alembic into SOPs. So this change is not required, but it
    is recommended if you use Alembic files within both USD and SOPs in Houdini.
- [d12551470fa596496fb6c35b95a33fd8a66ac6a0](https://github.com/sideeffects/USD/commit/d12551470fa596496fb6c35b95a33fd8a66ac6a0):
  - **Not required**: Changes to CMake for finding Houdini's MaterialX build.
  - Also update code that uses the OSL namespace to use HOSL instead.
- [474b1a62aca2de4b0193e225c1e0608e4880058b](https://github.com/sideeffects/USD/commit/474b1a62aca2de4b0193e225c1e0608e4880058b):
  - **Required**: This change alters the way that the USD library responds to
    certain kinds of edits to sublayer and reference lists executed through the
    UsdUtilsModifyAssetPaths method. With the standard implementation, changes
    that set a reference to an empty string, or that set the reference to a
    layer that is already in the list of sublayers is simply ignored. The
    requested change does nothing, or generates an error when the stage
    recomposes. This change makes it so that these "illegal" edits are
    meaningful. An empty reference is removed from the list, and duplicates
    are consolidated to a single entry. Since this change only affects the
    behavior of otherwise illegal edits, there should be no harm in
    incorporating this change into your USD library.
  - This change is part of [PR #1282](https://github.com/PixarAnimationStudios/USD/pull/1282)
    which is still unresolved.
- [170806f17c64a7eadb8c16a3e0a249742c5f242d](https://github.com/sideeffects/USD/commit/170806f17c64a7eadb8c16a3e0a249742c5f242d):
  - **Not required**: Fix a number of low level file handling methods, particularly
    on Windows. Skip all permissions checks prior to attempting to create or write
    files (such checks often fail on Windows network drives). Ignore reparese points
    on network drives (since they can't be resolved accurately).
  - Set umask properly when creating USD file
    (unmerged PR https://github.com/PixarAnimationStudios/USD/issues/1604).
- [797c46b483b8a761c25f53fcdeab383dda9447a2](https://github.com/sideeffects/USD/commit/797c46b483b8a761c25f53fcdeab383dda9447a2):
  - **Not required**: Fix crash in USD imaging caused by additional/removal of
    primitives within instance prototypes.
- [7eeeff91be1784d42b28c3fc5a8bdb4f8692a404](https://github.com/sideeffects/USD/commit/7eeeff91be1784d42b28c3fc5a8bdb4f8692a404):
  - **Not required**: Register velocities, acclerations, and local constant-interpolation
    primvars on point instancer primitives as hydra-accessible primvars. This change is
    submitted as a PR that has been partially merged (https://github.com/PixarAnimationStudios/USD/pull/1731).
- [110850c50ff06a0ab2212ac86ef0d1ab2b0431c5](https://github.com/sideeffects/USD/commit/110850c50ff06a0ab2212ac86ef0d1ab2b0431c5):
  - **Not required**: Continuation of previous commit for supporting primvars
    on point instancer prims.
- [d779210782817f2153e7bc4ef6850bb6d73281e9](https://github.com/sideeffects/USD/commit/d779210782817f2153e7bc4ef6850bb6d73281e9):
  - **Not required**: Provide new scene delegate API for render delegates to be able
    to query an id value that indicates the path of the implicit prototype for a prim.
  - Submitted as PR, but not yet merged (https://github.com/PixarAnimationStudios/USD/pull/1745).
- [336e4328fa18b05d854a1d3beb6b31d51d48b106](https://github.com/sideeffects/USD/commit/336e4328fa18b05d854a1d3beb6b31d51d48b106):
  - **Not required**: Continuation of above commit for data sharing ids, adding
    aupport for this API to the scene index emulation layer.
- [33c4e82cd89ff70bf522be2d705f9d0536b96216](https://github.com/sideeffects/USD/commit/33c4e82cd89ff70bf522be2d705f9d0536b96216):
  - **Not required**: Prevent double-creation of bprims when using the scene index
    emulation layer for the UsdImaging scene delegate.
  - Merged PR after 22.05 (https://github.com/PixarAnimationStudios/USD/pull/1737).
- [3d660f4facd5a9d3bd49ce7e34ac2857f270b785](https://github.com/sideeffects/USD/commit/3d660f4facd5a9d3bd49ce7e34ac2857f270b785):
  - **Not required**: Prevent massive memory consumption when using value clips with
    thousands of clip entries.
  - Merged PR after 22.05 (https://github.com/PixarAnimationStudios/USD/pull/1777).
- [88f41ffb28e16e0fd304db99833e5f690e67f3fc](https://github.com/sideeffects/USD/commit/88f41ffb28e16e0fd304db99833e5f690e67f3fc):
  - **Not required**: Allow building of usdBakeMtlx on Windows.
  - Equivalent change merged after 22.05 (https://github.com/PixarAnimationStudios/USD/pull/1848).
- [c141f60aac39d14ce085f515edda7f37030f7382](https://github.com/sideeffects/USD/commit/c141f60aac39d14ce085f515edda7f37030f7382):
  - **Not required**: Support animation of all attributes on camera prims, even
    attributes that are not part of the camera schema (such as renderer-specific
    attributes).
  - Merged PR after 22.05 (https://github.com/PixarAnimationStudios/USD/pull/1798)
- [9891f747d0eb7d099055ebd5d7a3241acb7d33d3](https://github.com/sideeffects/USD/commit/9891f747d0eb7d099055ebd5d7a3241acb7d33d3):
  - **Not Required**: Provide support for building USD against MaterialX 1.38.4.
  - Merged PR after 22.05 (https://github.com/PixarAnimationStudios/USD/pull/1792)
- [0eb418b9251ddf184e95f82cb08842666d6fdc81](https://github.com/sideeffects/USD/commit/0eb418b9251ddf184e95f82cb08842666d6fdc81):
  - **Not Required**: Fix issue specific to Houdini USD build process to allow USD
    executables (such as sdfdump) to run from within the Houdini binaries directory.
- [4a806a7bc4720eb5d1eb85013f0b3b8a13db03e9](https://github.com/sideeffects/USD/commit/4a806a7bc4720eb5d1eb85013f0b3b8a13db03e9):
  - **Not Required**: Fix race condition that could lead to crash during prim destruction.
  - Merged change after 22.05 (https://github.com/PixarAnimationStudios/USD/commit/c2847b059d3bc83fd52b1897ba6604bc9ea96b11)
- [fc7a207455220878d9be1814c91c7f70aabb630c](https://github.com/sideeffects/USD/commit/fc7a207455220878d9be1814c91c7f70aabb630c):
  - **Not Required**: Prevent python warning about not closing files after running usdview.
  - Merged PR after 22.05 (https://github.com/PixarAnimationStudios/USD/pull/1861)
- [09eec9937142d79b48719cf14415b161df6d37f4](https://github.com/sideeffects/USD/commit/09eec9937142d79b48719cf14415b161df6d37f4):
  - **Not Required**: Fix GLSL code generation error when binding primvars.
  - Merged PR after 22.05 (https://github.com/PixarAnimationStudios/USD/pull/1863)
- [77835ed40facd85633378c637900d46928bd242c](https://github.com/sideeffects/USD/commit/77835ed40facd85633378c637900d46928bd242c):
  - **Not Required**: Fix file existence check on Windows to prevent errors trying
    to write USD files to network drives.
  - Merged PR after 22.05 (https://github.com/PixarAnimationStudios/USD/pull/1871)
- [a7573246fa143543673be8925778cd223253797b](https://github.com/sideeffects/USD/commit/a7573246fa143543673be8925778cd223253797b):
  - **Not Required**: Invalidate CoordSys hydra representations when their target
    prim xform changes.
- [e4e794de841f0e687977c0e62238aefa11fb9045](https://github.com/sideeffects/USD/commit/e4e794de841f0e687977c0e62238aefa11fb9045):
  - **Not Required**: Fix tracking of changes to materials assigned to geometry subsets.
  - Merged PR after 22.05 (https://github.com/PixarAnimationStudios/USD/pull/1838)
- [d15e229a1c99cbe08f547139634bd166d0c57e14](https://github.com/sideeffects/USD/commit/d15e229a1c99cbe08f547139634bd166d0c57e14):
  - **Not Required**: Fix light linking updates by ensuring collections in hydra
    are dirtied when they are first created.
  - Merged PR after 22.05 (https://github.com/PixarAnimationStudios/USD/pull/1653)
- [9dd776e0519121646b30b202d2139800a285148f](https://github.com/sideeffects/USD/commit/9dd776e0519121646b30b202d2139800a285148f):
  - **Not Required**: Fix light linker update issues caused by missing dirty bit
    translation in the scene index emultation layer.
  - Merged PR after 22.05 (https://github.com/PixarAnimationStudios/USD/pull/1930)
- [1b6a59224caa7fda35df58020333383c4fa5d8e3](https://github.com/sideeffects/USD/commit/1b6a59224caa7fda35df58020333383c4fa5d8e3) and
  [688272331f82b086bc8d00e6667bceeac77e8a11](https://github.com/sideeffects/USD/commit/688272331f82b086bc8d00e6667bceeac77e8a11):
  - **Not required**: In Houdini 19.5.332 added these commits from the dev
    branch of USD which fix issues with how USD memory mapped files. These
    changes allow an arbitrary number of arbitrarily large USD files to be
    opened by USD simultaneously.
- [be04a42c3e621f4e0be85f5381dc76904b1e1330](https://github.com/sideeffects/USD/commit/be04a42c3e621f4e0be85f5381dc76904b1e1330):
  - **Not required**: In Houdini 19.5.379 added this commit to fix an issue with
    rendering scenes with value clips in husk
    (unmerged PR https://github.com/PixarAnimationStudios/USD/pull/2015).

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
export HOUDINI_DSO_EXCLUDE_PATTERN="{$HH/dso/USD_Ops.so}"
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
