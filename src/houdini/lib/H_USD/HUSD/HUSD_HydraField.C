/*
 * Copyright 2019 Side Effects Software Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Produced by:
 *	Side Effects Software Inc
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *	Canada   M5J 2M2
 *	416-504-9876
 *
 * NAME:	HUSD_HydraGeoPrim.h (HUSD Library, C++)
 *
 * COMMENTS:	Container for GT prim repr of a hydro geometry (R) prim
 */
#include "HUSD_HydraField.h"
#include "HUSD_Scene.h"
#include "XUSD_HydraField.h"
#include "XUSD_TicketRegistry.h"
#include "XUSD_Tokens.h"
#include <OP/OP_Node.h>
#include <GT/GT_Primitive.h>
#include <GT/GT_PrimVDB.h>
#include <GT/GT_PrimVolume.h>
#include <GU/GU_Detail.h>
#include <FS/UT_DSO.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/fileFormat.h>

PXR_NAMESPACE_USING_DIRECTIVE

GT_Primitive *
HUSD_HydraField::getVolumePrimitive(const UT_StringRef &filepath,
        const UT_StringRef &fieldname,
        int fieldindex,
        const UT_StringRef &fieldtype)
{
    SdfFileFormat::FileFormatArguments	 args;
    std::string				 path;
    GU_DetailHandle			 gdh;

    if (filepath.startsWith(OPREF_PREFIX) || filepath.startsWith(HUSD_HAPI_PREFIX))
    {
	SdfLayer::SplitIdentifier(filepath.toStdString(), &path, &args);
	gdh = XUSD_TicketRegistry::getGeometry(path, args);
    }
    else
    {
	GU_Detail			*gdp = new GU_Detail();

	if (gdp->load(filepath))
	    gdh.allocateAndSet(gdp);
    }

    if (gdh)
    {
	GU_DetailHandleAutoReadLock	 lock(gdh);
	const GU_Detail			*gdp = lock.getGdp();

	if (gdp)
	{
	    const GA_Primitive	*gaprim = nullptr;
	    const GEO_Primitive	*geoprim = nullptr;
	    GA_Offset field_offset = GA_INVALID_OFFSET;

	    if (fieldname.isstring())
	    {
		GA_ROHandleS	 nameattrib(gdp, GA_ATTRIB_PRIMITIVE, "name");

		if (nameattrib.isValid())
		{
		    GA_StringIndexType nameindex;

		    // Find the index value for our field name.
		    nameindex = nameattrib->lookupHandle(fieldname);
		    if (nameindex != GA_INVALID_STRING_INDEX)
		    {
			for (GA_Iterator it(gdp->getPrimitiveRange());
			     !it.atEnd(); ++it)
			{
			    // Check for any prim with our field name.
			    if (nameattrib->getStringIndex(*it) == nameindex)
			    {
				// Make sure the prim type matches what we
				// expect. If so, we are done searching.
				if (fieldtype == HusdHdPrimTypeTokens()->
					bprimHoudiniFieldAsset.GetString())
				{
				    if (gdp->getPrimitive(*it)->
					getTypeId().get() == GA_PRIMVOLUME)
				    {
					field_offset = it.getOffset();
					break;
				    }
				}
				else
				{
				    if (gdp->getPrimitive(*it)->
					getTypeId().get() == GA_PRIMVDB)
				    {
					field_offset = it.getOffset();
					break;
				    }
				}
			    }
			}
		    }
		}
	    }

	    // If we didn't find the primitive looking for the field name,
	    // look at the field index (for native volumes).
	    if (field_offset == GA_INVALID_OFFSET &&
		fieldtype == HusdHdPrimTypeTokens()->
		    bprimHoudiniFieldAsset.GetString())
		field_offset = gdp->primitiveOffset(GA_Index(fieldindex));

	    if (field_offset != GA_INVALID_OFFSET)
	    {
		gaprim = gdp->getPrimitive(field_offset);
		geoprim = static_cast<const GEO_Primitive *>(gaprim);
	    }

	    if (geoprim && geoprim->getTypeId().get() == GEO_PRIMVDB)
		return new GT_PrimVDB(gdh, geoprim);
	    else if (geoprim && geoprim->getTypeId().get() == GEO_PRIMVOLUME)
		return new GT_PrimVolume(gdh, geoprim, GT_DataArrayHandle());
	}
    }

    return nullptr;
}

HUSD_HydraField::HUSD_HydraField(PXR_NS::TfToken const& typeId,
				 PXR_NS::SdfPath const& primId,
				 HUSD_Scene &scene)
    : HUSD_HydraPrim(scene, primId.GetText()),
      myFieldIndex(0)
{
    myHydraField = new PXR_NS::XUSD_HydraField(typeId, primId, *this);
}

HUSD_HydraField::~HUSD_HydraField()
{
    delete myHydraField;
}

GT_PrimitiveHandle
HUSD_HydraField::getGTPrimitive() const
{
    const UT_StringHolder &fieldtype = myHydraField->getFieldType();
    GT_Primitive *prim = nullptr;

    prim = getVolumePrimitive(FilePath(), FieldName(), FieldIndex(), fieldtype);

    return GT_PrimitiveHandle(prim);
}

