# HoudiniUsdBridge
Houdini libraries that must be recompiled to use Houdini with a custom build of
the USD library.

## Building USD on Linux

In order for the HoudiniUsdBridge to work, you must build USD with the boost
python library replacement that ships in the USD code base.

## SideFX Changes to the USD Library

The USD library that ships with Houdini 21.0 is forked from the official USD
25.05 release. Some SideFX-specific changes have since been applied to fix
bugs, or deal with SideFX-specific build issues. Whenever possible we submit
pull requests to have our changes integrated into the next USD release, but
sometimes this isn't possible. And in any case, these changes are not part of
the USD 25.05 release. This section lists every deviation of the SideFX USD
library from Pixar's 25.05 branch, to help you decide which of these changes
you may want or need to incorporate into your own USD build.

Changes ordered from oldest to newest at initial release of Houdini 21.0:
- [f090fe00a9fa26d2fdf59d759c6f83414480bb71](https://github.com/sideeffects/USD/commit/f090fe00a9fa26d2fdf59d759c6f83414480bb71):
  - **Not required**: Provide a flag to turn on the MATERIALX_BUILD_SHARED_LIBS define in libraries that build against MaterialX. This is required to enable defines within MaterialX code that allow importing symbols from shared libraries on Windows.
- [8450b1a082e6a90a6a32ef055eb2357e853b0582](https://github.com/sideeffects/USD/commit/8450b1a082e6a90a6a32ef055eb2357e853b0582):
  - **Not required**: Switch the default value for USD_ABC_XFORM_PRIM_COLLAPSE from true to false, since this seems to be what most Houdini users expect.
- [33291076290d08f23b5355c58d7d7fa3da89b16b](https://github.com/sideeffects/USD/commit/33291076290d08f23b5355c58d7d7fa3da89b16b):
  - **Not required**: Skip the building of the `extras` subdirectory (issues with Windows)
- [d09fbfe8da53eec222d7bc4edcf4d39c1cc3de77](https://github.com/sideeffects/USD/commit/d09fbfe8da53eec222d7bc4edcf4d39c1cc3de77):
  - **Not required**: Pick up all the required Alembic components
- [0416f953ff7c0e250aaaefad03215aa310cf11d3](https://github.com/sideeffects/USD/commit/0416f953ff7c0e250aaaefad03215aa310cf11d3):
  - **Not required**: Change RPATH locations
- [13e313c2a3eba1fb6ce3d4859a960cbd9fd2bb12](https://github.com/sideeffects/USD/commit/13e313c2a3eba1fb6ce3d4859a960cbd9fd2bb12):
  - **Not required**: Pick up Houdini Qt plugins and fonts
- [160c5e7b2a3a2ee5d84aaa08d86183f88718862b](https://github.com/sideeffects/USD/commit/160c5e7b2a3a2ee5d84aaa08d86183f88718862b):
  - **Not required**: Disable incremental linking on Windows
- [dee66026e51ad7eef48456b38f761527073e080c](https://github.com/sideeffects/USD/commit/dee66026e51ad7eef48456b38f761527073e080c):
  - **Not required**: Support for debug builds
- [dc52a33b41864fc677ab87ca3e88916768c4747a](https://github.com/sideeffects/USD/commit/dc52a33b41864fc677ab87ca3e88916768c4747a):
  - **Not required**: A few defines specific to how Houdini uses USD
- [7bf0164011f70fca279fcccf5b18717b4e28b594](https://github.com/sideeffects/USD/commit/7bf0164011f70fca279fcccf5b18717b4e28b594):
  - **Not required**: Change invocation of Python when finding PySide and running (py)uic
- [53b167b2d91230c858ce7bec22e892a95c9d334a](https://github.com/sideeffects/USD/commit/53b167b2d91230c858ce7bec22e892a95c9d334a):
  - **Required**: Add new HdSceneDelegate and HdPrim methods to get a "data sharing id" for an HdPrim. The purpose of this id (an SdfPath) is to provide an identifier that can be used to share data between multiple instancer prototypes when these prototypes in USD all share a common prototype primitive, but where hydra has created multiple HdPrims (since hydra creates a separate HdPrim for every prototype of every point instancer, even if multiple point instancers use identical instanceable prims as their prototypes).
- [1ff64a2f62fb1e5d5d446c6118e9d2676c91b5ca](https://github.com/sideeffects/USD/commit/1ff64a2f62fb1e5d5d446c6118e9d2676c91b5ca):
  - **Not required**: Get correct invalidations to coordSys sprims when their target USD prim     is updated.
- [4956b03c3b60c853b523b383038b78125b8de609](https://github.com/sideeffects/USD/commit/4956b03c3b60c853b523b383038b78125b8de609):
  - **Not required**: Remove the DrawModeAdapter that ships with USD. Houdini provides its own version of this optimized for use with HGL. Having both installed leads to warnings on startup (and could lead to the use of the wrong one).
- [d161c4032ff3b38ca74ec174d8c4c40bf0293e17](https://github.com/sideeffects/USD/commit/d161c4032ff3b38ca74ec174d8c4c40bf0293e17):
  - **Required**: Make GlfSimpleLight more controllable, but hide these extra controls behind a flag so that non-Houdini applications won't be affected by the addition of these new parameters.
- [8781ed0ee53cdfe6067283a8983a1304bcff5698](https://github.com/sideeffects/USD/commit/8781ed0ee53cdfe6067283a8983a1304bcff5698):
  - **Not required**: Fix a potential crash when updating skinned primitives.
- [84f1e351a74764ac908f1f603d9aad02c883e529](https://github.com/sideeffects/USD/commit/84f1e351a74764ac908f1f603d9aad02c883e529):
  - **Not required**: Adding $HDSO/usd_plugins to the rpath/runpath of MacOS and Linux USD builds. This allows libraries to be linked against the "plugin" binaries that live under usd_plugins, such as usdAbc and sdrGlslfx. This allows (perhaps among other things) for the UsdAbc and SdrGlslfx python modules to be imported without raising exceptions.
- [0de5a31f183c233cac151878e289be8abfd92d7b](https://github.com/sideeffects/USD/commit/0de5a31f183c233cac151878e289be8abfd92d7b):
  - **Not required**: Interpolate VtValue's containing GfMatrix\*f types in HdResampleNeighbors().
- [795d4a83f844f33f4275b73d116aae354be85fb7](https://github.com/sideeffects/USD/commit/795d4a83f844f33f4275b73d116aae354be85fb7):
  - **Not required**: Fix an issue where an unexpected time sample could be reported for skinning computations.
- [69afabaf07f52f249a711c66d6e24261bdebfabc](https://github.com/sideeffects/USD/commit/69afabaf07f52f249a711c66d6e24261bdebfabc):
  - **Not required**: Remove camera adapter registration so that Houdini's camera adapter will be used instead.
- [3eb94b3f1c3bd8a174eae30a5b11b3685d603063](https://github.com/sideeffects/USD/commit/3eb94b3f1c3bd8a174eae30a5b11b3685d603063):
  - **Not required**: Export the protected \_RemovePrim method so that we can subclass the UsdImagingCameraAdapter and call this method (though on Windows even if we don't call the method, linking fails when trying to create a subclass because of this inaccessible method).
- [15387dc7b6d8aa6c7d7cf8bf14e8f9c8e1fe5edf](https://github.com/sideeffects/USD/commit/15387dc7b6d8aa6c7d7cf8bf14e8f9c8e1fe5edf):
  - **Not required**: Add an environment variable that allows the overriding of the dome light texture file used for "simple light" dome lights.
- [64a7ae9be3fe9c2ce9795935e646aa409cc87e10](https://github.com/sideeffects/USD/commit/64a7ae9be3fe9c2ce9795935e646aa409cc87e10):
  - **Not required**: Add houdiniFieldAsset as a "legacy volume field prim type". Without this addition, Houdini fields are not tracked properly by Hydra for time varying attributes.
- [2bb765196f0b9a85d5058342b67d797398f6afbf](https://github.com/sideeffects/USD/commit/2bb765196f0b9a85d5058342b67d797398f6afbf):
  - **Not required**: Export UsdImagingDataSourceFieldAsset constructors so that external libraries can call the static New() method on these classes (which is inlined).
- [f4325bb11edba7e93c963cd5d7c73f28ec2d6c8c](https://github.com/sideeffects/USD/commit/f4325bb11edba7e93c963cd5d7c73f28ec2d6c8c):
  - **Not required**: Add USDIMAGING_API to GetDataSharingId method in case anyone wants to create a subclass of UsdImagingDelegate.
- [67cbe29c4b857eef2206ff4e5d04c28ab91e9862](https://github.com/sideeffects/USD/commit/67cbe29c4b857eef2206ff4e5d04c28ab91e9862):
  - **Not required**: Update to "Fix a potential crash when updating skinned primitives." change.
- [5c6244872e65f49cfb51648c9aaf151c5547abdd](https://github.com/sideeffects/USD/commit/5c6244872e65f49cfb51648c9aaf151c5547abdd):
  - **Not required**: Add relative directory to MacOS rpath to find $HDSO from $HB so pure binary apps on MacOS (usdcat and usdtree) can run.
- [4cb774a3699284b995609c36897f6ff8226a722b](https://github.com/sideeffects/USD/commit/4cb774a3699284b995609c36897f6ff8226a722b):
  - **Not required**: Moving logic for iterating over render delegate's material contexts from the scene delegate to the material schema. This ensures that the fallback context is only used if *all* the delegate's contexts fail.
- [97f54b0306fd9af4731dbc85dae6143540f2eceb](https://github.com/sideeffects/USD/commit/97f54b0306fd9af4731dbc85dae6143540f2eceb):
  - **Not required**: Moving logic for iterating over render delegate's material contexts from the scene delegate to the material schema. This ensures that the fallback context is only used if *all* the delegate's contexts fail.
- [79a52ebf5f2d98ccc45b0f97887c9ef36f5ff9cf](https://github.com/sideeffects/USD/commit/79a52ebf5f2d98ccc45b0f97887c9ef36f5ff9cf):
  - **Not required**: Add another value to the MacOS rpath to let binaries (sdfdump, usdcat) find the Python library in the Houdini installation.
- [8ec98c8e22ebf829ca84c2196e2575174948945c](https://github.com/sideeffects/USD/commit/8ec98c8e22ebf829ca84c2196e2575174948945c):
  - **Not required**: Making the fetching of camera data more tolerant to a workflow where the query is actually on behalf of a coordSys. This means both: * accepting a GfVec2f for the clippingRange as generated by a UsdImagingPrimAdapter (vs a GfRange1f as generated by a UsdImagingCameraAdapter) * allowing SamplePrimvar to fall back on querying camera attributes not only when the primType is camera but also coordSys
- [63ea68268588a9a080e404f4ac36cd33e983fd76](https://github.com/sideeffects/USD/commit/63ea68268588a9a080e404f4ac36cd33e983fd76):
  - **Not required**: Add test for an invalid iterator before removing it from a map. Prevents a crash in a test case during a hydra update.
- [c4fdd0d0f460dd88dea9fde7bd56f9f7b076a29b](https://github.com/sideeffects/USD/commit/c4fdd0d0f460dd88dea9fde7bd56f9f7b076a29b):
  - **Not required**: Changing usage of OSL namespace to HOSL
- [cc5ff35190dc1e7bc11034b1e0806716f142d4d0](https://github.com/sideeffects/USD/commit/cc5ff35190dc1e7bc11034b1e0806716f142d4d0):
  - **Not required**: Provide a define that will allow render delegate developers to disable python support while building their render delegates against Houdini's USD library(which is always built with python support enabled).
- [9e4c2f9900ab9b86eed1f914f82eeb402d58f925](https://github.com/sideeffects/USD/commit/9e4c2f9900ab9b86eed1f914f82eeb402d58f925):
  - **Not required**: This change  addresses a crash that resulted when adding / removing a sublayer results in a cycle.  In order to prevent this, a cache of visited layer/sublayer path pairs is used to terminate recursive processing of sublayer paths.
- [b3fde894c4bc128d3f05257f39cd2fae7193c88e](https://github.com/sideeffects/USD/commit/b3fde894c4bc128d3f05257f39cd2fae7193c88e):
  - **Not required**: Patch to facilitate Windows builds
- [47921e87ffd24c1b9f7366b2abca5d07acecfc05](https://github.com/sideeffects/USD/commit/47921e87ffd24c1b9f7366b2abca5d07acecfc05):
  - **Not required**: Comment out code that can manipulate the \_DEBUG symbol. In Houdini builds of USD, we manage this setting without the need for an additional BOOST_DEBUG_PYTHON symbol, and this code can actually _change_ the \_DEBUG symbol (set it from =1 to simply defined) which can lead to problems when including TBB headers.
- [4efd51f1be82349edd7a1b1f5124077317e85e87](https://github.com/sideeffects/USD/commit/4efd51f1be82349edd7a1b1f5124077317e85e87):
  - **Not required**: Implement a specialization of UsdImagingDataSourceAttribute<SdfAssetPath> that properly resolves UDIM relative paths.
- [b508c6502acc9c0fd7dd414e0cce903e405285e4](https://github.com/sideeffects/USD/commit/b508c6502acc9c0fd7dd414e0cce903e405285e4):
  - **Not required**: Ensure default (public) symbol visibility on macOS
- [8d2eb893c1d9e9968d2b360c33fa2c6c31c70fc5](https://github.com/sideeffects/USD/commit/8d2eb893c1d9e9968d2b360c33fa2c6c31c70fc5):
  - **Not required**: Revert "Merging scene index: conservatively always removing prim and re-adding prims when receiving a prims removed notice."
- [014475b2cd6d5842aff4d72f03f8e385922e6004](https://github.com/sideeffects/USD/commit/014475b2cd6d5842aff4d72f03f8e385922e6004):
  - **Not required**: Attempt to fix an issue with the MergingSceneIndex.
- [a112029ba2944bb051b9140df1898ac8f380c9e7](https://github.com/sideeffects/USD/commit/a112029ba2944bb051b9140df1898ac8f380c9e7):
  - **Not required**: This change fixes a coding error that is triggered when performing a sublayer operation on a layer whose file format is a package.  The current implementation which generates fine grained change lists makes use of an anonymous layer to compute a diff against.  It is an error to create such a layer with a package file format.  To workaround this limitation with the current implementation, we send packages down the "big bang" invalidation path.
- [a0aada2ca30693f8263d4e7fc09a3df2728baab7](https://github.com/sideeffects/USD/commit/a0aada2ca30693f8263d4e7fc09a3df2728baab7):
  - **Not required**: Revert "Make pxrTargets.cmake relocatable (when built with TBB and OpenSubdiv)"
- [71fb15d73afddeb6fc894c8bad0a9528cedd6eea](https://github.com/sideeffects/USD/commit/71fb15d73afddeb6fc894c8bad0a9528cedd6eea):
  - **Not required**: Minor fixes and reverts (scope, arg data type etc.)
- [17ba5d8ec8be63cb5661fb1e9e894ca99a26ca16](https://github.com/sideeffects/USD/commit/17ba5d8ec8be63cb5661fb1e9e894ca99a26ca16):
  - **Not required**: [usdImaging] Fix for light filter edits
- [f1f3567228612659e764021654d6360751741370](https://github.com/sideeffects/USD/commit/f1f3567228612659e764021654d6360751741370):
  - **Not required**: Making `UsdShadeConnectionSourceInfo` hashable, and keeping a cache of seen connections in the recursive `_IsConnectionDirty` to minimise rechecking branches.
- [b3f25b3a8ed1d8d02193831d3c5c08d885399ce6](https://github.com/sideeffects/USD/commit/b3f25b3a8ed1d8d02193831d3c5c08d885399ce6):
  - **Not required**: UsdSkelImaging Hydra 2.0: block normals to match Hydra 1.0.
- [53e8b675d9c89548feb3eecfc296de12bca848ed](https://github.com/sideeffects/USD/commit/53e8b675d9c89548feb3eecfc296de12bca848ed):
  - **Not required**: Fix SampleExtComputationInput() for non-varying computation inputs.
- [ec7a5df3c948bf8ba8ed1536dfc15848925234de](https://github.com/sideeffects/USD/commit/ec7a5df3c948bf8ba8ed1536dfc15848925234de):
  - **Not required**: [usdImaging] Preserve primType for materials under instancers
- [9fce1fe85fd3b541048ce867c83322971b997cfb](https://github.com/sideeffects/USD/commit/9fce1fe85fd3b541048ce867c83322971b997cfb):
  - **Not required**: Previously the deep-copy aborted recursion at this point. This change ensures we continue deep-copying the VectorDataSource's elements.
- [93cfdc406d1fb37d4b0ea28b992bb2a70a23242a](https://github.com/sideeffects/USD/commit/93cfdc406d1fb37d4b0ea28b992bb2a70a23242a):
  - **Not required**: UsdImagingDrawModeSceneIndex: fixing scene index leaking out prim type and data source.
- [396008afe5fc1e67a824da8eda7c18c99eccb95a](https://github.com/sideeffects/USD/commit/396008afe5fc1e67a824da8eda7c18c99eccb95a):
  - **Not required**: UsdImagingDrawModeStandin: Folding GetPrim and GetDescendantPrim into one.
- [041471f52de8fd0c8bd17a4cdabcdc7265676649](https://github.com/sideeffects/USD/commit/041471f52de8fd0c8bd17a4cdabcdc7265676649):
  - **Not required**: UsdImaging_PiPrototypeSceneIndex blocks applyDrawMode. Thus, we actually swap the propagated prototype with the draw mode standin instead of the prim made unrenderable.
- [81845879c1eda6fca9e2d1021c24f4d5336b9457](https://github.com/sideeffects/USD/commit/81845879c1eda6fca9e2d1021c24f4d5336b9457):
  - **Not required**: UsdImagingDrawModeSceneIndex: It no longer looks whether something is a native instance - and thus replaces a native instance with a draw mode stand-in if the native instance says so.
- [387609acf08e7949a127cfbecb55bb6b3214d309](https://github.com/sideeffects/USD/commit/387609acf08e7949a127cfbecb55bb6b3214d309):
  - **Not required**: [usdImaging] niInstanceAggregationSceneIndex now updates its internal instance-tracking data structures prior to sending notification to observers.  This ensures that observers will see the correct result if they query instancer topology.
- [774ebf6cce41fe1cec2edfc68c7b0c29d2c57b61](https://github.com/sideeffects/USD/commit/774ebf6cce41fe1cec2edfc68c7b0c29d2c57b61):
  - **Required**: [hd] Analysis of Hydra scene index structures in USD scenes with native instancing reveals that while most merging scene indexes have few inputs (2 or 3), native instancing can involve hundreds (350+) of inputs, one per prototype. To scale better for these cases, the merging scene index now builds an SdfPathTable tracking the inputs relevant for particular paths, and uses this to avoid an O(N) loop over the entire list for GetPrim() and GetChildPrimPaths().
- [a0cf12c585c25b25f293ce06b2fd11782c2064c9](https://github.com/sideeffects/USD/commit/a0cf12c585c25b25f293ce06b2fd11782c2064c9):
  - **Required**: HdMergingSceneIndex::\_GetInputEntriesByPath: return empty on miss.

- [ce702af6ce7c4864df60bc5668bef78b5d78f3d7](https://github.com/sideeffects/USD/commit/ce702af6ce7c4864df60bc5668bef78b5d78f3d7):
  - **Required**: HdMergingSceneIndex: adding vectorized API.
- [51aa55a85e50f3d0fb05dbcf56e7b36964643d4d](https://github.com/sideeffects/USD/commit/51aa55a85e50f3d0fb05dbcf56e7b36964643d4d):
  - **Not required**: UsdImagingNiInstanceAggregationSceneIndex: batching retained scene index operations.
- [9a1f3f90d83b54856f39fb131f3efc762a3b361f](https://github.com/sideeffects/USD/commit/9a1f3f90d83b54856f39fb131f3efc762a3b361f):
  - **Not required**: UsdImagingNiPrototypePropagatingSceneIndex: batching operations to merging scene index.
- [8d968954b7469fd82fefa5347fa6693b04e8416e](https://github.com/sideeffects/USD/commit/8d968954b7469fd82fefa5347fa6693b04e8416e):
  - **Not required**: Fix bug with removing instances in \_InstanceObserver::PrimsRemoved()
- [af7617d6e51e1f99c40d94da5bc53d9507cb4945](https://github.com/sideeffects/USD/commit/af7617d6e51e1f99c40d94da5bc53d9507cb4945):
  - **Not required**: All these headers were using the wrong symbols name to switch between IMPORT and EXPORT modes, causing linker warning on Windows about IMPORTing symbols defined in this library.
- [ac472d9e30f3fd979d586edebea11b51bb2e3db8](https://github.com/sideeffects/USD/commit/ac472d9e30f3fd979d586edebea11b51bb2e3db8):
  - **Not required**: Expose the HdPrimOriginSchemaTokens->primOrigin data source through the base draw mode prim source to allow HdxPrimOriginInfo::FromPickHits to properly resolve draw mode standin prims to the original USD prims they represent.
- [115584bfee121c7c62669d9b650f2618a5cadece](https://github.com/sideeffects/USD/commit/115584bfee121c7c62669d9b650f2618a5cadece):
  - **Required**: Improve performance of \_RebuildInputsPathTable().
- [336e4cd0916a0e1d5c05b2aaf7d50a5c7d5e4e74](https://github.com/sideeffects/USD/commit/336e4cd0916a0e1d5c05b2aaf7d50a5c7d5e4e74):
  - **Required**: Improve performance of HdMergingSceneIndex::InsertInputScenes().
- [6654fedfd7e5ce25c21204589bb35e68d48b4bd9](https://github.com/sideeffects/USD/commit/6654fedfd7e5ce25c21204589bb35e68d48b4bd9):
  - **Required**: [usdImagingGL] Adjust order by which UsdImagingGLEngine establishes state prior to drawing, with the goal of avoiding unnecessary invalidation of scene indexes.
- [d0d7d9c66ecd6363a0a7e35b45765375e6601aee](https://github.com/sideeffects/USD/commit/d0d7d9c66ecd6363a0a7e35b45765375e6601aee):
  - **Required**: Add UsdImaging_InstanceLocationTranslationSceneIndex.  This scene index translates SdfPath-based data sources that point below instances to point to corresponding paths under prototypes.
- [82bede2edab2ff2c10682c631ef86eb965c80030](https://github.com/sideeffects/USD/commit/82bede2edab2ff2c10682c631ef86eb965c80030):
  - **Not required**: Previously there was an *overlay* of setting visibility=true for point instancer prototypes. This prevents users from (even temporarily) making their prototypes invisible to remove them from the generated image. Instead, this change moves the data to an *underlay* where explicit visibility authoring will effectively be respected. This is linked to issue #3748 and also lays a bit more groundwork to address issue #3693
- [2239ebf3ee44ed3e743c90d7a69504083a7cfa51](https://github.com/sideeffects/USD/commit/2239ebf3ee44ed3e743c90d7a69504083a7cfa51):
  - **Not required**: hd: Add missing HD_API symbol exports
- [88b7a74390c6e65c7020c79e9e5dcf0d280fcaa7](https://github.com/sideeffects/USD/commit/88b7a74390c6e65c7020c79e9e5dcf0d280fcaa7):
  - **Not required**: Linking the prototype's underlay visibility to the instancer's visibility, including a dependency to track updates (i.e., if the instancer's visibility is dirtied, the prototype's visibility will be dirtied)
- [18b6d8f8bad86a1b60ff5ded9cb5d97e399fe8f8](https://github.com/sideeffects/USD/commit/18b6d8f8bad86a1b60ff5ded9cb5d97e399fe8f8):
  - **Not required**: Reinstating checks that were accidentally removed during the previous commit.
- [93910aa3eb2d7be16888143c0ecbd333c8c776bb](https://github.com/sideeffects/USD/commit/93910aa3eb2d7be16888143c0ecbd333c8c776bb):
  - **Not required**: In DidAddSpec and DidRemoveSpec, don't raise a coding error when passed the absolute root path "/". This happens quite frequently with PCP_ENABLE_MINIMAL_CHANGES_FOR_LAYER_OPERATIONS == 1, and the default action of "doing nothing" seems to provide correct behavior.
- [d687c15dd305b6eb9341df89596f6ac721562403](https://github.com/sideeffects/USD/commit/d687c15dd305b6eb9341df89596f6ac721562403):
  - **Not required**: Enable "propertyOrder" to work when specified on API schemas. Property order does not compose as of this change, but will be added in a follow-up change. Currently, strongest opinion wins with Typed schemas being stronger than API schemas.
- [665c811004301b92651a9bb46581ac0d7cc567c3](https://github.com/sideeffects/USD/commit/665c811004301b92651a9bb46581ac0d7cc567c3):
  - **Not required**: [usdImaging] Fix !resetXformStack! handling in UsdImaging and add testUsdImagingGLResetXformStack.
- [bc81617c482cfa6776038dd63dc0a54ebdfd5081](https://github.com/sideeffects/USD/commit/bc81617c482cfa6776038dd63dc0a54ebdfd5081):
  - **Not required**: Explicitly handle SdfAssetPath primvars specified on native instances, so that they aren't dropped on the Hydra instancer prim.
- [071e760efde06a38d59a8859694fb4d7e30d461a](https://github.com/sideeffects/USD/commit/071e760efde06a38d59a8859694fb4d7e30d461a):
  - **Not required**: When calculating extents, include default, proxy, and render purpose bounds. Otherwise setting a bound-based draw mode on a prim with no default geometry may return an invalid bounding box.
- [4a631cd4a7549f6cc18b264b4cee940e8492985a](https://github.com/sideeffects/USD/commit/4a631cd4a7549f6cc18b264b4cee940e8492985a):
  - **Not required**: Use HdRenderTagTokens->geometry as the "default purpose" token instead of HdTokens->geometry (which is the same token, but is semantically different - Hydra RenderTags map to USD Purpose).
- [d2f2844bbdde66dbdb00114fcd4fe6d3f63d5a32](https://github.com/sideeffects/USD/commit/d2f2844bbdde66dbdb00114fcd4fe6d3f63d5a32):
  - **Not required**: Removing log-generating check that "shouldn't" be there (i.e., it was removed upstream by Pixar in https://github.com/PixarAnimationStudios/OpenUSD/commit/2ee9c3bdf8423c2a4dc55356bb422407e7dcadd2).
- [4f0f425321fd3fc8b1e31a78a5c4e8d3d09256db](https://github.com/sideeffects/USD/commit/4f0f425321fd3fc8b1e31a78a5c4e8d3d09256db):
  - **Not required**: Fix data sharing id operation when using the scene index scene delegate. Adding the data sharing schema to the schema generator script.
- [b2b6084fcf83a14515d0955905e0cbe366fd0aac](https://github.com/sideeffects/USD/commit/b2b6084fcf83a14515d0955905e0cbe366fd0aac):
  - **Not required**: UsdAbc: translate arrayExtent metadata into elementSize. These values are equivalent and describe how consecutive array elements should be grouped.
- [93f506379ad366aef1fdc1f75dc4a282bcea5ea6](https://github.com/sideeffects/USD/commit/93f506379ad366aef1fdc1f75dc4a282bcea5ea6):
  - **Not required**: [usdImaging 2.0] Stateless instance proxy path translation.
- [0bd6962a84020722d7e7e9e45ae04b216f69eb23](https://github.com/sideeffects/USD/commit/0bd6962a84020722d7e7e9e45ae04b216f69eb23):
  - **Not required**: tf: Add Windows-specific code that retries rename attempts in Tf_AtomicRenameFileOver as well as Windows-specific env settings to control the behavior: PXR_WINDOWS_FILE_ATOMIC_RENAME_RETRIES and PXR_WINDOWS_FILE_ATOMIC_RENAME_RETRY_WAIT_MS.
- [756adad349e1087ff990f04ff0693ce5c08c63b2](https://github.com/sideeffects/USD/commit/756adad349e1087ff990f04ff0693ce5c08c63b2):
  - **Not required**: Revert "[usdImaging 2.0] Stateless instance proxy path translation." The reverted commit fixed a bug with material bindings on instanced geometry, but caused a regression in another case (see https://github.com/PixarAnimationStudios/OpenUSD/issues/4056).
- [f0bbc323f89734c964cc65799c1482faa2fecad0](https://github.com/sideeffects/USD/commit/f0bbc323f89734c964cc65799c1482faa2fecad0):
  - **Not required**: [usdImaging 2.0] Stateless instance proxy path translation.
- [535445b02657550ac066ae7172451e5a87fa5ba9](https://github.com/sideeffects/USD/commit/535445b02657550ac066ae7172451e5a87fa5ba9):
  - **Not required**: Reworking the logic of the instanceProxyPathTranslationSceneIndex so that rather than working our way through the path front-to-back (matching the shortest path with an instanceSchema), we go back-to-front (matching the longest path with an instanceSchema). This correctly matches both "fully-unexpanded" and "partially-expanded" paths.
- [ed685de021cc33bab192448520e0570c5e659578](https://github.com/sideeffects/USD/commit/ed685de021cc33bab192448520e0570c5e659578):
  - **Not required**: [usdImaging] Address code review notes: hold scene index by handle, not refptr; add iteration limit to avoid infinite recursion.
- [34685a686e7286a0ce55d6efec922bd6931d5ec9](https://github.com/sideeffects/USD/commit/34685a686e7286a0ce55d6efec922bd6931d5ec9):
  - **Not required**: Ensure Hydra dirty signals when a material's surface output is changed. Previously there was no Hydra 2 dirty signal if you changed the connection for a material from one shader to another.
- [7ce2ba931d6e09706f4b696ccd666ac2c71e3ebe](https://github.com/sideeffects/USD/commit/7ce2ba931d6e09706f4b696ccd666ac2c71e3ebe):
  - **Not required**: [usdImaging] Handle edge case of added prim going from "over" to "not over".
- [510f1ff96b1a47aec7fc587b1cdc33e3bf7fc430](https://github.com/sideeffects/USD/commit/510f1ff96b1a47aec7fc587b1cdc33e3bf7fc430):
  - **Not required**: [usdImaging] UsdImagingPiPrototypePropagatingSceneIndex now uses a hash of the string representation of SdfPath, rather than of the memory address of the internal SdfPath node, when generating names for instancers, in order to provide stable results run-to-run.
- [2c08c9251a17038d79c35d9e9abdb8cd09f301c4](https://github.com/sideeffects/USD/commit/2c08c9251a17038d79c35d9e9abdb8cd09f301c4):
  - **Not required**: Work around missing HdSceneIndexPrim::IsDefined method.
- [6add7da8c9c6a5f0bdab90e00a69226c771ddfe2](https://github.com/sideeffects/USD/commit/6add7da8c9c6a5f0bdab90e00a69226c771ddfe2):
  - **Not required**: Adding env var USDIMAGING_CORRECT_PROXY_PATH_TRANSLATION to allow rolling back the reworked instance proxy path translation.
- [3c464890211ca000d7c689216da11cf1284fb632](https://github.com/sideeffects/USD/commit/3c464890211ca000d7c689216da11cf1284fb632):
  - **Not required**: [usdImaging] Re-enable USDIMAGING_CORRECT_PROXY_PATH_TRANSLATION, with a fix to avoid a hang in certain scenarios combining point instancers with native instanced prototypes.
- [2a64543e653b093751f6b446fd2363dabf606f86](https://github.com/sideeffects/USD/commit/2a64543e653b093751f6b446fd2363dabf606f86):
  - **Not required**: Force const access into instance info arrays to avoid triggering "copy-on-write".
- [5ad793e7fb55b654545bece200a43a0b0077fc4f](https://github.com/sideeffects/USD/commit/5ad793e7fb55b654545bece200a43a0b0077fc4f):
  - **Not required**: Forward removals in HdMergingSceneIndex when the prim survives in another input. Fixes a crash when changing which payloads are loaded under a point instancer prototype.

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
export PYTHONPATH=$USD_ROOT/lib/python:$BRIDGE_ROOT/houdini/python2.7libs
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

The code in the 'src/houdini/lib/H_USD/gusd' directory of this repository
began as a direct copy of the 'third_party/houdini/lib/gusd' directory from
this USD repository, and so still contains the original Pixar copyright
notices.
