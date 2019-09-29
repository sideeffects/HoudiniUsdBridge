/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	HUSD_HydraPrim.C (HUSD Library, C++)
 *
 * COMMENTS:	Base class container for a hydra prim class
 */

#include "HUSD_HydraPrim.h"
#include "XUSD_Tokens.h"
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usdGeom/tokens.h>

#include <UT/UT_Debug.h>
HUSD_HydraPrimData::HUSD_HydraPrimData() : myOffset(GA_INVALID_OFFSET) {}
HUSD_HydraPrimData::~HUSD_HydraPrimData() {}

static SYS_AtomicInt<int> theUniqueId(0);


HUSD_HydraPrim::HUSD_HydraPrim(HUSD_Scene &scene,
                               const char *path)
    : myPrimPath(path),
      myGeoID(path),
      myScene(scene),
      myExtraData(nullptr),
      myVersion(0),
      myID(newUniqueId()),
      mySelectDirty(false),
      myRenderTag(TagDefault)
{
    myTransform.identity();
}

HUSD_HydraPrim::~HUSD_HydraPrim()
{
    delete myExtraData;
}

void
HUSD_HydraPrim::setExtraData(HUSD_HydraPrimData *data)
{
    if(myExtraData)
	delete myExtraData;
    myExtraData = data;
}

int
HUSD_HydraPrim::newUniqueId()
{
    return theUniqueId.exchangeAdd(1);
}
    

bool
HUSD_HydraPrim::getBounds(UT_BoundingBox &box) const
{
    // Increase the bounds by the origin of the object. Useful for lights and
    // cameras.
    UT_Vector4D origin(0,0,0,1);
    origin *= myTransform;
    box.enlargeBounds(UT_Vector4(origin));
    return true;
}


bool
HUSD_HydraPrim::hasPathID(int id) const
{
    if(id == myID)
	return true;

    for(int i=0; i<myInstanceIDs.entries(); i++)
	if(id == myInstanceIDs(i))
	    return true;

    return false;
    
}

HUSD_HydraPrim::RenderTag
HUSD_HydraPrim::renderTag(const PXR_NS::TfToken &pass)
{
    if (pass == PXR_NS::HusdHdPrimValueTokens()->render)
	return HUSD_HydraPrim::TagRender;
    if (pass == PXR_NS::HdRenderTagTokens->guide)
	return HUSD_HydraPrim::TagGuide;
    if (pass == PXR_NS::HdRenderTagTokens->proxy)
	return HUSD_HydraPrim::TagProxy;
    return HUSD_HydraPrim::TagDefault;
}

const PXR_NS::TfToken &
HUSD_HydraPrim::renderTag(RenderTag tag)
{
    switch (tag)
    {
	case TagDefault:
	    return PXR_NS::HdTokens->geometry;
	case TagGuide:
	    return PXR_NS::HdRenderTagTokens->guide;
	case TagProxy:
	    return PXR_NS::HdRenderTagTokens->proxy;
	case TagRender:
	    return PXR_NS::HusdHdPrimValueTokens()->render;
	case TagInvisible:
	    return PXR_NS::UsdGeomTokens->invisible;
	case NumRenderTags:
	    break;
    }
    static PXR_NS::TfToken	invalid("invalid");
    return invalid;
}
