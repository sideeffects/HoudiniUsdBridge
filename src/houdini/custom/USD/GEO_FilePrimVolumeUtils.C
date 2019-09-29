/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 */

#include "GEO_FilePrimVolumeUtils.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GEO_VolumePrimTokens, GEO_VOLUME_PRIM_TOKENS);

int GT_PrimVolumeCollection::thePrimitiveType = GT_PRIM_UNDEFINED;

int
GT_PrimVolumeCollection::getStaticPrimitiveType()
{
    if (thePrimitiveType == GT_PRIM_UNDEFINED)
        thePrimitiveType = GT_Primitive::createPrimitiveTypeId();
    return thePrimitiveType;
}

PXR_NAMESPACE_CLOSE_SCOPE
