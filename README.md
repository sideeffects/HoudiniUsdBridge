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

The USD library that ships with Houdini is forked from the official USD 21.08
release. Some SideFX-specific changes have since been applied to fix bugs, or
deal with SideFX-specific build issues. Whenever possible we submit pull
requests to have our changes integrated into the next USD release, but
sometimes this isn't possible. And in any case, these changes are not part
of the USD 21.08 release. This section lists every deviation of the SideFX
USD library from Pixar's 21.08 branch, to help you decide which of these
changes you may want or need to incorporate into your own USD build.

Changes ordered from oldest to newest:

- [34c57e87b38ee7371d8f6879ba9cd1318822a764](https://github.com/sideeffects/USD/commit/34c57e87b38ee7371d8f6879ba9cd1318822a764):
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
- [2474017bebe87c7433bc7d6287ab1502085d584a](https://github.com/sideeffects/USD/commit/2474017bebe87c7433bc7d6287ab1502085d584a):
  - **Not required**: Adds a couple of null pointer checks covering crashes
    we have seen when performing certain kinds of viewport updates. This
    change is completely harmless, but may not be necessary if your goal is
    maximum alignment with the official USD release.
- [3950b32c1ca824956740be705e5fd91ec8ff71a0](https://github.com/sideeffects/USD/commit/3950b32c1ca824956740be705e5fd91ec8ff71a0):
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
- [3a743695686f0c9c187fca5935337e2f52b5ba0d](https://github.com/sideeffects/USD/commit/3a743695686f0c9c187fca5935337e2f52b5ba0d):
  - **Required**: Continuation of the above change above.
  - This change is also part of [PR #1282](https://github.com/PixarAnimationStudios/USD/pull/1282)
    which is still unresolved.
- [5ba58d2d57a3fd2243ce2da90d6d6a30d84af9c5](https://github.com/sideeffects/USD/commit/5ba58d2d57a3fd2243ce2da90d6d6a30d84af9c5):
  - **Not required**: Adds a define to pxr.h that allows third party plugins
    built against Houdini's USD library to build without python support even
    though Houdini's USD library is always built with python support.
- [640dbca82402d8c05301dead9a555d28b39ab49a](https://github.com/sideeffects/USD/commit/640dbca82402d8c05301dead9a555d28b39ab49a):
  - **Not required**: Adds pragmas to control symbol visibility on MacOS.
    This change *might* be required if you are building the HoudiniUsdBridge
    for MacOS so that the symbol visibility of your USD library matches
    Houdini's expectation.
- [60e105ce8e1e79a59905e36703fe242f5ffc13b9](https://github.com/sideeffects/USD/commit/60e105ce8e1e79a59905e36703fe242f5ffc13b9):
  - **Not required**: CMake changes to allow Houdini's USD library to be
    built against a MaterialX library built as shared libraries.
- [9cd1e3b8c992817df16374debdb073a06dd8b1d2](https://github.com/sideeffects/USD/commit/9cd1e3b8c992817df16374debdb073a06dd8b1d2):
  - **Not required**: Changes how the USD library configures MaterialX for
    looking up material libraries.
- [ac2d48d673b480a5813dd1ef92bbd4009c74782b](https://github.com/sideeffects/USD/commit/ac2d48d673b480a5813dd1ef92bbd4009c74782b):
  - **Not required**: Changes how the USD library configures MaterialX for
    looking up material libraries.
- [995b2c8ab419f22df543b61448ed8e67ff73cdc9](https://github.com/sideeffects/USD/commit/995b2c8ab419f22df543b61448ed8e67ff73cdc9):
  - **Not required**: Change the default value for the
    USD_ABC_XFORM_PRIM_COLLAPSE environment variable, which controls how the
    Alembic plugin exposes Alembic xform prims to USD. In Houdini, a default
    value of false gives results that better match how Houdini imports
    Alembic into SOPs. So this change is not required, but it is recommended
    if you use Alembic files within both USD and SOPs in Houdini.
- [2d2259579e97dd485d9fd0243cdd8740852dfa56](https://github.com/sideeffects/USD/commit/2d2259579e97dd485d9fd0243cdd8740852dfa56):
  - **Not required**: Changes of OSL library namespace to HOSL in MaterialX
    plugins.
- [804f9735c1396d2cd9b9c0732766c6dc7839a9db](https://github.com/sideeffects/USD/commit/804f9735c1396d2cd9b9c0732766c6dc7839a9db):
  - **Not required**: Part of a commit to fix a crash that can occur when
    recomposing a scene after a change that alters multiple payload arcs.
  - This was merged into the USD mainline as part of [PR #1568](from https://github.com/PixarAnimationStudios/USD/pull/1568).
- [379bf524d71206e3d1c775e0ae613788e134bfff](https://github.com/sideeffects/USD/commit/379bf524d71206e3d1c775e0ae613788e134bfff):
  - **Not required**: This is also part of [PR #1568](from https://github.com/PixarAnimationStudios/USD/pull/1568).
- [15206bf16ddc60259be7628ad15f9fde9891bcec](https://github.com/sideeffects/USD/commit/379bf524d71206e3d1c775e0ae613788e134bfff):
  - **Not required**: Another null pointer check when updating instance
    prototype primitives in hydra. May resolve crashes after making
    certain kinds of USD edits.
- [f5da946bb4c716a04f1566c239f04f88bc6f2cc6](https://github.com/sideeffects/USD/commit/f5da946bb4c716a04f1566c239f04f88bc6f2cc6):
  - **Not required**: Fixes an issue with reading UDIMs from inside USDZ
    archives.
  - Merged into USD mainline as [PR #1560](https://github.com/PixarAnimationStudios/USD/pull/1560).
- [f99c29226c2f4f3708fff0dd95d4e86c6c8684b1](https://github.com/sideeffects/USD/commit/f99c29226c2f4f3708fff0dd95d4e86c6c8684b1):
  - **Not required**: A fix to allow running UsdUtilsExtractExternalReferences
    on a layer that is not editable. This API is not directly used by Houdini.
- [966cd4cc90bedc1807f8579bc034c758aad8555a](https://github.com/sideeffects/USD/commit/966cd4cc90bedc1807f8579bc034c758aad8555a):
  - **Not required**: This is also part of [PR #1568](from https://github.com/PixarAnimationStudios/USD/pull/1568).
- [17f204c27757016802f48d93bd09650c9bea76ed](https://github.com/sideeffects/USD/commit/17f204c27757016802f48d93bd09650c9bea76ed):
  - **Not required**: This is also part of [PR #1568](from https://github.com/PixarAnimationStudios/USD/pull/1568).
- [11d262593d4e10d87699588274bddd16e74ec2d6](https://github.com/sideeffects/USD/commit/11d262593d4e10d87699588274bddd16e74ec2d6):
  - **Not required**: Ensure files handles are closed by usdGenSchema.py and
    other USD utility scripts. Not closing these handles can cause warnings in
    Python 3 builds.
  - Merged into USD mainline as [PR #1557](https://github.com/PixarAnimationStudios/USD/pull/1557).
- [dc7c1dfaf6442d98af5ce1b13fdedc9481d5c32a](https://github.com/sideeffects/USD/commit/dc7c1dfaf6442d98af5ce1b13fdedc9481d5c32a):
  - **Not required**: Performance improvement when editing a large number of
    native instances.
  - Merged into USD mainline as [PR #1608](https://github.com/PixarAnimationStudios/USD/pull/1608).
- [770712468b123a2ca61cd6534f25af8f79d329ce](https://github.com/sideeffects/USD/commit/770712468b123a2ca61cd6534f25af8f79d329ce):
  - **Not required**: Addresses issues with saving USD files with certain
    umask settings.
  - Suggested as a fix for [Issue #1604](https://github.com/PixarAnimationStudios/USD/issues/1604),
    but so far still unresolved.
- [372a1c28ef8fdfe1d3e6ad3c2840ad31c25ad6b4](https://github.com/sideeffects/USD/commit/372a1c28ef8fdfe1d3e6ad3c2840ad31c25ad6b4):
  - **Not required**: Fixes a crash in hydra when processing multiple edits
    to native instances.
  - Merged into USD mainline as a fix for [Issue #1551](https://github.com/PixarAnimationStudios/USD/issues/1551).
- [d8d76de66dddf5e8199bb436ade60521b5ef724b](https://github.com/sideeffects/USD/commit/d8d76de66dddf5e8199bb436ade60521b5ef724b):
  - **Do not merge**: This change is reverted in a later commit.
- [aae23b08c65bc5047c899fd334f20fc8f85b87e0](https://github.com/sideeffects/USD/commit/aae23b08c65bc5047c899fd334f20fc8f85b87e0):
  - **Not required**: Works around file permission check failures on Windows
    network drives.
- [501f7064b1592852e450fa046100e5853828534e](https://github.com/sideeffects/USD/commit/501f7064b1592852e450fa046100e5853828534e):
  - **Do not merge**: This reverts [d8d76de66dddf5e8199bb436ade60521b5ef724b](https://github.com/sideeffects/USD/commit/d8d76de66dddf5e8199bb436ade60521b5ef724b)
    mentioned above.
- [ef120b26bc4cb39bd58901cafed13dfcb65b93e3](https://github.com/sideeffects/USD/commit/ef120b26bc4cb39bd58901cafed13dfcb65b93e3):
  - **Not required**: Fixes material resolution on instanceable primitives.
  - Merged into USD mainline to fix [Issue #1626](https://github.com/PixarAnimationStudios/USD/issues/1626).
- [60cd8cd7875c868b65491643ac64159c4198a8ef](https://github.com/sideeffects/USD/commit/60cd8cd7875c868b65491643ac64159c4198a8ef):
  - **Not required**: Links pxOsd against OSD CPU libraries only, not the GPU
    libraries. This whane may be necessary to prevent husk from loading any
    OpenGL dependencies, if that is a problem, as it is on some farm render
    nodes.
- [365456ca1e7358099c3673e3f72dfd07dc30306a](https://github.com/sideeffects/USD/commit/365456ca1e7358099c3673e3f72dfd07dc30306a):
  - **Not required**: Further elimination of Windows file permission checks
    which produce many false negatives on network drives. Only affects Windows.
- [55b3c549e9af7e3325dd0f0f185f93248616cbfd](https://github.com/sideeffects/USD/commit/55b3c549e9af7e3325dd0f0f185f93248616cbfd):
  - **Not required**: Prevents USD from trying to resolve reparse points for
    directories on NTFS network drives.
- [928c584e879966eac6f9b5dfcc7a5cc6014553dd](https://github.com/sideeffects/USD/commit/928c584e879966eac6f9b5dfcc7a5cc6014553dd):
  - **Not required**: Adding new hydra scene delegate API, GetDataSharingId,
    that can help a render delegate determine if it is safe to share an instance
    prototype between multiple instancers. This API is not used by Houdini or
    Karma, and so is only necessary if you wish to use this API in your own
    render delegate.
  - Currently unresolved as [PR #1745](https://github.com/PixarAnimationStudios/USD/pull/1745).
- [a3ed33f703aad9aa643ec1c499997f8e3fe34d37](https://github.com/sideeffects/USD/commit/a3ed33f703aad9aa643ec1c499997f8e3fe34d37):
  - **Required**: Adds new GetScenePrimPaths API that greatly improves the
    performance of the LOP viewport when using a render delegate other than
    Houdini GL.
  - If you have custom usdImaging prim adapters that create HdInstancers, you
    may need to implement this new API for your prim adapter, though you can do
    so as a simple loop calling GetScenePrimPath.
  - Currently unesolved as [PR #1744](https://github.com/PixarAnimationStudios/USD/pull/1744).
- [c370736f7302580c840a54751181c87f03790aea](https://github.com/sideeffects/USD/commit/c370736f7302580c840a54751181c87f03790aea):
  - **Not required**: Tracks and syncs hydra camera primitives in response to
    any animated attributes on the primitive. This fixes, for example, a bug
    where an animated background image in the viewport would not update.
  - Currently unresolved as [PR #1798](https://github.com/PixarAnimationStudios/USD/pull/1798).
- [c0de894cdb05f9300b1ebc4e39bc1d4fa458b7f1](https://github.com/sideeffects/USD/commit/c0de894cdb05f9300b1ebc4e39bc1d4fa458b7f1):
  - **Not required**: Fixes code for resolving UDIM texture paths when there
    is no 1001 tile image.
  - Merged into USD mainline as [PR #1787](https://github.com/PixarAnimationStudios/USD/pull/1787).
- [3b1f7258f68aeb8a23a30f0098f01cccc408ebf8](https://github.com/sideeffects/USD/commit/3b1f7258f68aeb8a23a30f0098f01cccc408ebf8) and
  [081584e5a9b549733fdfa3df2cd15dcfc87a859c](https://github.com/sideeffects/USD/commit/081584e5a9b549733fdfa3df2cd15dcfc87a859c):
  - **Not required**: In Houdini 19.0.699 added these commits from the dev
    branch of USD which fix issues with how USD memory mapped files. These
    changes allow an arbitrary number of arbitrarily large USD files to be
    opened by USD simultaneously.

## Building Pixar Operators for Houdini

Because the gusd library gets built into Houdini, you should _not_ build the
Houdini operators when doing your build of USD. Instead, you should build the
Pixar operators from the $HT/samples/USD directory that comes with
Houdini. You can of course update the source files in there from your USD
repository if you have newer code than what ships with Houdini.

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

