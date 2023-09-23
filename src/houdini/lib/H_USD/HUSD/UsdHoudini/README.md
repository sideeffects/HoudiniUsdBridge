# Adding a new schema

At present, schema generation for `UsdHoudini` is not part of the build process (though hopefully this will change in future - see `USDKarma` for inspiration). As such, there are a number of steps involved in adding a new schema.

## Defining the new schema

This step is basically the same as you'd do for starting new USD schemas completely independent of Houdini: update `schema.usda` with your new schema definition.

### Best practices

Make sure you prefix the schema name with `Houdini`, and any attributes with `houdini:`. For example, the `HoudiniViewportGuideAPI` schema provides a `houdini:guidescale` attribute.

If the schema is multiple-apply, also ensure the `propertyNamespacePrefix` has a `houdini` prefix. For example, the `HoudiniProceduralAPI` schema specifies `token propertyNamespacePrefix = "houdiniProcedural"`.

## Generating the schema and wrappers

Once you're done with `schema.usda` launch a terminal in this directory and run:

```bash
USD_DISABLE_PRIM_DEFINITIONS_FOR_USDGENSCHEMA=1 $HFS/bin/usdGenSchema -t $HT/codegenTemplates
```

> **IMPORTANT**
> Note the explicit setting of `USD_DISABLE_PRIM_DEFINITIONS_FOR_USDGENSCHEMA=1` in the command above. The `usdGenSchema` script attempts to set it internally but, at time of writing, executing the script via `hython` means that the relevant USD libraries are already loaded/initialised (via `import hou`) before the script has an opportunity to set the value and influence the importing. You might be lucky and everything will "just work" without setting the environment variable explicitly, but with the move to 23.08 we've seen failure for custom light filter schemas when it's been left unset.

This will modify a number of files:
* `generatedSchema.usda`
* `plugInfo.json`
* `tokens.cpp`
* `tokens.h`
* `wrapTokens.cpp`

(the last three files will only be modified if the new schema defines attributes - which is the most common case)

`usdGenSchema` will also create a number of new files. Again using the example of `HoudiniViewportGuideAPI`:

* `houdiniViewportGuideAPI.cpp`
* `houdiniViewportGuideAPI.h`
* `wrapHoudiniViewportGuideAPI.cpp`

## Build system integration

Now that you have all the generated files, CMake needs to know about them. This involves modifying three files. For the sake of consistency, we'll again use the example of `HoudiniViewportGuideAPI`.

1. `$HUSD/CMakeSources.cmake`
   * Add `UsdHoudini/houdiniViewportGuideAPI.cpp` to the `husd_sources` section
   * Add `UsdHoudini/houdiniViewportGuideAPI.h` to the `husd_internal_headers` section
2. `$HUSD/UsdHoudini/CMakeLists.txt`
   * Add `houdiniViewportGuideAPI.h` to the `hdk_headers` section
3. `$HUSD/UsdHoudini/CMakeSources.cmake`
   * Add `wrapHoudiniViewportGuideAPI.cpp` to the `sources` section

## Expose schema to Python (optional)

If you want to be able to use your schema in Python, for example:
```python
from husd import UsdHoudini
api = UsdHoudini.HoudiniViewportGuideAPI(prim)
guidescale = api.GetHoudiniGuidescaleAttr()
```

Then also modify `module.cpp` in the current directory, adding a `TF_WRAP(...)` entry for your schema. 

## Commit everything to SVN

All the modified and new files mentioned above (both from `usdGenSchema` and CMake updates) need to be committed to SVN.

Echoing the opening to this document, schema generation is *not* part of the build system, so we need to store both the pre-generation source (i.e., `schema.usda`) and generated files in our repos.
