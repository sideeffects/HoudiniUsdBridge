# HoudiniUsdBridge
Houdini libraries that must be recompiled to use Houdini with a custom build of
the USD library.

The master branch of this repository should never be used. Instead, switch to
the branch that corresponds to the Houdini release for which you wish to build
the HoudiniUsdBridge. On that branch, search for the tag that corresponds to
the exact Houdini build number you will be using. If there is no exact match,
use the tag that is closest to, but lower than, your Houdini build number. This
is the HoudiniUsdBridge baseline that will be compatible with your Houdini
build.

## Acknowledgements

The USD library on which these libraries are built is created by Pixar:
https://github.com/PixarAnimationStudios/USD
SideFX also maintains a fork of this repository:
https://github.com/sideeffects/USD

The code in the 'src/houdini/lib/H_USD/gusd' directory of this repository
began as a direct copy of the 'third_party/houdini/lib/gusd' directory from
this USD repository, and so still contains the original Pixar copyright
notices.

