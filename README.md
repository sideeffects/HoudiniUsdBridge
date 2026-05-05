# HoudiniUsdBridge
Houdini libraries that must be recompiled to use Houdini with a custom build of
the USD library.

> [!IMPORTANT]
> **The master branch of this repository should never be used.**
> 
> Instead, switch the branch that corresponds to the Houdini release for which
> you wish to build the HoudiniUsdBridge. On that branch, search for the tag
> that corresponds to the exact Houdini build number you will be using. If
> there is no exact match, use the tag that is closest to, but lower than, your
> Houdini build number. This is the HoudiniUsdBridge baseline that will be
> compatible with your Houdini build.

## SideFX Changes to the USD Library

The USD library that ships with Houdini 22.0 is forked from the official USD
26.05 release. Some SideFX-specific changes have since been applied to fix
bugs, or deal with SideFX-specific build issues. Whenever possible we submit
pull requests to have our changes integrated into the next USD release, but
sometimes this isn't possible. And in any case, these changes are not part of
the USD 26.05 release. This section lists every deviation of the SideFX USD
library from Pixar's 26.05 branch, to help you decide which of these changes
you may want or need to incorporate into your own USD build.

Changes ordered from oldest to newest at initial release of Houdini 22.0:
- [646eee634](https://github.com/sideeffects/USD/commit/646eee634):
  - **Not required**: Change RPATH locations (Houdini-specific macOS/Linux RPATH overrides).
- [04d222000](https://github.com/sideeffects/USD/commit/04d222000):
  - **Not required**: Change invocation of Python when finding PySide and running (py)uic.
- [605a9f0b0](https://github.com/sideeffects/USD/commit/605a9f0b0):
  - **Not required**: Add relative directory to MacOS rpath so pure binary apps (`usdcat`, `usdtree`) can find `$HDSO` from `$HB`.
- [5cf00aede](https://github.com/sideeffects/USD/commit/5cf00aede):
  - **Not required**: Add another value to the MacOS rpath for binaries (`sdfdump`, `usdcat`) to find the Houdini Python library.
- [1897e5e20](https://github.com/sideeffects/USD/commit/1897e5e20):
  - **Not required**: Add `$HDSO/usd_plugins` to the rpath/runpath so plugin binaries (`usdAbc`, `sdrGlslfx`) link correctly and their Python modules import without exception.
- [27ee4de71](https://github.com/sideeffects/USD/commit/27ee4de71):
  - **Not required**: Pick up all the required Alembic components (add `C` component to HDF5 `find_package`).
- [81452eb3c](https://github.com/sideeffects/USD/commit/81452eb3c):
  - **Not required**: Houdini-specific update to ensure our TBB lib is used (replaces `PXR_FIND_TBB_IN_CONFIG` with a direct `find_package(TBB REQUIRED COMPONENTS tbb)`).
- [f453dbfb3](https://github.com/sideeffects/USD/commit/f453dbfb3):
  - **Not required**: Disable incremental linking on Windows.
- [f1d3badd3](https://github.com/sideeffects/USD/commit/f1d3badd3):
  - **Not required**: Skip the building of the `extras` subdirectory (issues with Windows).
- [aa74fe07b](https://github.com/sideeffects/USD/commit/aa74fe07b):
  - **Not required**: Provide a flag to turn on the `MATERIALX_BUILD_SHARED_LIBS` define in libraries that build against MaterialX (required for Windows shared-library builds).
- [71c93a280](https://github.com/sideeffects/USD/commit/71c93a280):
  - **Not required**: Support for debug builds (adds `DEBUG_POSTFIX "_d"` and debug `usd_plugins_d` paths).
- [938361326](https://github.com/sideeffects/USD/commit/938361326):
  - **Not required**: A few defines specific to how Houdini uses USD (`TF_NO_GNU_EXT`, `BUILD_COMPONENT_SRC_PREFIX`, unconditional `PXR_PYTHON_SUPPORT_ENABLED`, etc.).
- [1a579cb4b](https://github.com/sideeffects/USD/commit/1a579cb4b):
  - **Not required**: Patch to facilitate Windows builds and remove `PXR_USE_INTERNAL_BOOST_PYTHON`.
- [e45a54d9a](https://github.com/sideeffects/USD/commit/e45a54d9a):
  - **Not required**: Ensure default (public) symbol visibility on macOS.
- [0aff1ab27](https://github.com/sideeffects/USD/commit/0aff1ab27):
  - **Not required**: Provide a define (`PXR_FORCE_PYTHON_SUPPORT_DISABLED`) to allow disabling python support in render delegates.
- [f38aefca5](https://github.com/sideeffects/USD/commit/f38aefca5):
  - **Not required**: Comment out code that can manipulate the `_DEBUG` symbol (avoids breaking TBB headers in Houdini debug builds).
- [5497cd829](https://github.com/sideeffects/USD/commit/5497cd829):
  - **Not required**: Switch the default value for `USD_ABC_XFORM_PRIM_COLLAPSE` from true to false, since this seems to be what most Houdini users expect.
- [fa60374e2](https://github.com/sideeffects/USD/commit/fa60374e2):
  - **Not required**: Changing usage of OSL namespace to HOSL (sdrOsl only); needed for Houdini's OSL build.
- [6281f3df7](https://github.com/sideeffects/USD/commit/6281f3df7):
  - **Not required**: Pick up Houdini Qt plugins and fonts in `usdview.py`.
- [2e72dd1f3](https://github.com/sideeffects/USD/commit/2e72dd1f3):
  - **Not required**: Remove camera adapter registration so that Houdini's camera adapter will be used instead.
- [b02c8cf2d](https://github.com/sideeffects/USD/commit/b02c8cf2d):
  - **Not required**: Remove the DrawModeAdapter that ships with USD. Houdini provides its own version of this optimized for use with HGL. Having both installed leads to warnings on startup (and could lead to the use of the wrong one).
- [44bce1c5e](https://github.com/sideeffects/USD/commit/44bce1c5e):
  - **Not required**: Updated logic around the warning for missing build necessities, to prevent the warning when skipping codeGen.
- [a323c9c14](https://github.com/sideeffects/USD/commit/a323c9c14):
  - **Not required**: Fix MSVC 19.50 C++20 compiler error (avoid infinite recursion from spaceship-operator rewrite rules in `weakPtrFacade.h`).
- [0597c9d02](https://github.com/sideeffects/USD/commit/0597c9d02):
  - **Not required**: Replace usage of `std::is_pod`, which is deprecated in C++20.
- [081f38449](https://github.com/sideeffects/USD/commit/081f38449):
  - **Not required**: In `DidAddSpec` and `DidRemoveSpec`, don't raise a coding error when passed the absolute root path "/".
- [fc69fedf6](https://github.com/sideeffects/USD/commit/fc69fedf6):
  - **Not required**: Add test for an invalid iterator before removing it from a map; prevents a crash in `_ResyncInstancer` during a hydra update.
- [02418d906](https://github.com/sideeffects/USD/commit/02418d906):
  - **Not required**: UsdAbc — translate `arrayExtent` metadata into `elementSize` for round-tripping non-standard float tuples (issue #3362).
- [ae7f95ee1](https://github.com/sideeffects/USD/commit/ae7f95ee1):
  - **Not required**: Add `VT_API` before friend specification of a function that is later declared as `TF_API` (Windows symbol export consistency).
- [5e4ec3680](https://github.com/sideeffects/USD/commit/5e4ec3680):
  - **Not required**: Add an environment variable (`HOUDINI_DEFAULT_DOMELIGHT_TEXTURE`) that allows the overriding of the dome light texture file used for "simple light" dome lights.
- [e4976d442](https://github.com/sideeffects/USD/commit/e4976d442):
  - **Not required**: Add `houdiniFieldAsset` as a "legacy volume field prim type". Without this addition, Houdini fields are not tracked properly by Hydra for time-varying attributes.
- [586fd7ae7](https://github.com/sideeffects/USD/commit/586fd7ae7):
  - **Not required**: Export `UsdImagingDataSourceFieldAsset` constructors so that external libraries can call the static `New()` method on these classes (which is inlined).
- [b0e207579](https://github.com/sideeffects/USD/commit/b0e207579):
  - **Not required**: Export the protected `_RemovePrim` method on `cameraAdapter` so subclassing is possible without Windows linker errors.
- [4383cbeb7](https://github.com/sideeffects/USD/commit/4383cbeb7):
  - **Required**: Make `GlfSimpleLight` more controllable, but hide these extra controls behind a flag so that non-Houdini applications won't be affected by the addition of these new parameters.
- [6ca614b15](https://github.com/sideeffects/USD/commit/6ca614b15):
  - **Not required**: Improve performance of `HdMergingSceneIndex::InsertInputScenes()`. Fixes O(n²) behavior in `_AddStrictPrefixesOfSceneRoots` (50s → 42ms for 17,500 prototypes).
- [e4d3afbf2](https://github.com/sideeffects/USD/commit/e4d3afbf2):
  - **Not required**: Make the fetching of camera data more tolerant to a workflow where the query is actually on behalf of a coordSys (accept `GfVec2f` for `clippingRange`; allow `SamplePrimvar` to fall back on querying camera attributes for `coordSys` primType).
- [60288c3e7](https://github.com/sideeffects/USD/commit/60288c3e7):
  - **Not required**: Get correct invalidations to coordSys sprims when their target USD prim is updated.
- [8470eb559](https://github.com/sideeffects/USD/commit/8470eb559):
  - **Required**: Add data sharing id feature for instancer prototypes. Adds `HdSceneDelegate::GetDataSharingId()`, `HdDataSharingSchema`, and related machinery so multiple instancer prototypes that share a common USD prototype primitive can share Hydra data, even when Hydra creates separate `HdPrim`s for each prototype.
- [584fd02ca](https://github.com/sideeffects/USD/commit/584fd02ca):
  - **Required**: Add `MotionAPI` adapter for `motion:blurScale` and `nonlinearSampleCount`. The new `UsdImagingMotionAPIAdapter` translates these MotionAPI properties into Hydra primvars for any prim with MotionAPI applied (not just `UsdGeomGprim` descendants), tagged as constant interpolation. (USD GitHub issue #3970.)

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
* COPY_HOUDINI_USD_PLUGINS: Whether to copy the $HH/dso/usd_plugins directory
  from the Houdini install into the HoudiniUsdBridge install tree. This defaults  to ON.

Once the libraries are built, run `cmake --install`. This will copy the bridge
libraries into the installation directory structure (defaults to `/usr/local`,
but this location can be overridden in the CMake config).

## Running Houdini

To run Houdini using the HoudiniUsdBridge libraries, the following environment
variables must be set so that Houdini uses the bridge libraries and plugins
instead of the ones that ship with Houdini.

```
# The install location of your custom USD library.
export USD_ROOT=/path/to/USD
# The install location of the HoudiniUsdBridge libraries.
export BRIDGE_ROOT=/path/to/install/usr/local

# Ensure the bridge libHoudiniUSD.so library and the dummy libpxr_*
# libraries are used.
export LD_LIBRARY_PATH=$BRIDGE_ROOT/dsolib
# Give priority to the custom USD and bridge python libraries.
export PYTHONPATH=$USD_ROOT/lib/python:$BRIDGE_ROOT/houdini/python3.13libs
# Add the bridge houdini directory to the HOUDINI_PATH so that Houdini
# plugins will be loaded from here.
export HOUDINI_PATH=$BRIDGE_ROOT/houdini:\&
# Houdini will tell the USD library to load plugins explicitly from this
# directory. The default value of this variable loads USD plugins from the
# dso/usd_plugins subdirectory of every HOUDINI_PATH entry. But we
# explicitly don't want USD to try to load the plugins in
# $HFS/houdini/dso/usd_plugins.
export HOUDINI_USD_DSO_PATH=$BRIDGE_ROOT/houdini/dso/usd_plugins
# Tell Houdini to not load the USD related plugins that ship with Houdini.
export HOUDINI_DSO_EXCLUDE_PATTERN="{$HH/dso/USD_Ops.so} {$HH/dso/USD_SopVol.so}"
```

Some additional modifications to the environment may be necessary to use
the Shot Build package, which is also part of the HoudiniUsdBridge.
Alternatively, the Shot Builder package can simply be disabled.

Note that using these environment variables, the actual Houdini installation
does not need to be modified at all. And removing the environment variables
will therefore return Houdini to its native state, using the built-in USD
library.

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

The code in the `src/houdini/lib/H_USD/gusd` directory of this repository
began as a direct copy of the `third_party/houdini/lib/gusd` directory from
this USD repository, and so still contains the original Pixar copyright
notices.
