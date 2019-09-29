/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 */

#include "GEO_FileUtils.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GEO_HandleOtherPrimsTokens,
                        GEO_HANDLE_OTHER_PRIMS_TOKENS);

void
GEOconvertTokenToEnum(const TfToken &str_value, GEO_HandleOtherPrims &value)
{
    if (str_value == GEO_HandleOtherPrimsTokens->define)
        value = GEO_OTHER_DEFINE;
    else if (str_value == GEO_HandleOtherPrimsTokens->overlay)
        value = GEO_OTHER_OVERLAY;
    else if (str_value == GEO_HandleOtherPrimsTokens->xform)
    {
        value = GEO_OTHER_XFORM;
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
