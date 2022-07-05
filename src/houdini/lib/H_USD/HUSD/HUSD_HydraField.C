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
#include "HUSD_Constants.h"
#include "HUSD_Scene.h"
#include "XUSD_HydraField.h"
#include "XUSD_LockedGeoRegistry.h"
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
HUSD_HydraField::getVolumePrimitiveFromDetail(GU_ConstDetailHandle &gdh,
    const UT_StringRef &fieldname,
    int fieldindex,
    const UT_StringRef &fieldtype)
{
    if (gdh)
    {
        GU_DetailHandleAutoReadLock	 lock(gdh);
        const GU_Detail			*gdp = lock.getGdp();

        if (gdp)
        {
            const GEO_Primitive	*geoprim = nullptr;
            GA_Offset field_offset = GA_INVALID_OFFSET;

            // For Houdini volumes, the field index is the primary identifier,
            // and has no need to use the name.
            if (field_offset == GA_INVALID_OFFSET &&
                fieldtype == HusdHdPrimTypeTokens->
                    bprimHoudiniFieldAsset.GetString() &&
                fieldindex < gdp->getNumPrimitives())
                field_offset = gdp->primitiveOffset(GA_Index(fieldindex));

            if (field_offset == GA_INVALID_OFFSET && fieldname.isstring())
            {
                // Look for VDB volumes by default, Houdini volumes if
                // the fieldtype indicates a Houdini volume.
                GA_PrimCompat::TypeMask primtype;
                if (fieldtype == HusdHdPrimTypeTokens->
                    bprimHoudiniFieldAsset.GetString())
                    primtype = GEO_PrimTypeCompat::GEOPRIMVOLUME;
                else
                    primtype = GEO_PrimTypeCompat::GEOPRIMVDB;

                // For Houdini volumes, always use the first name match (the
                // field index, if it exists, is a prim number, not a match
                // number). For other volume types the field index is the
                // match number.
                int matchnumber = 0;
                if (fieldtype != HusdHdPrimTypeTokens->
                    bprimHoudiniFieldAsset.GetString())
                    matchnumber = (fieldindex >= 0 ? fieldindex : 0);

                const GEO_Primitive *prim = gdp->findPrimitiveByName(
                    fieldname, primtype, "name", matchnumber);
                if (prim)
                    field_offset = prim->getMapOffset();
            }

            if (field_offset != GA_INVALID_OFFSET)
                geoprim = gdp->getGEOPrimitive(field_offset);

            if (geoprim && geoprim->getTypeId().get() == GEO_PRIMVDB)
                return new GT_PrimVDB(gdh, geoprim);
            else if (geoprim && geoprim->getTypeId().get() == GEO_PRIMVOLUME)
                return new GT_PrimVolume(gdh, geoprim, GT_DataArrayHandle());
        }
    }

    return nullptr;
}

GT_Primitive *
HUSD_HydraField::getVolumePrimitive(const UT_StringRef &filepath,
        const UT_StringRef &fieldname,
        int fieldindex,
        const UT_StringRef &fieldtype)
{
    SdfFileFormat::FileFormatArguments	 args;
    std::string				 path;
    SdfLayer::SplitIdentifier(filepath.toStdString(), &path, &args);

    GU_ConstDetailHandle		 gdh;

    // Note that we might get a normal file path ending in ".volumes" when a
    // bgeo file that contains packed volumes is loaded from disk. In that case
    // we need to access that unpacked detail through the locked geo registry.
    if (filepath.startsWith(OPREF_PREFIX)
        || filepath.startsWith(HUSD_HAPI_PREFIX)
        || UT_StringRef(path).endsWith(HUSD_Constants::getVolumeSopSuffix()))
    {
	gdh = XUSD_LockedGeoRegistry::getGeometry(path, args);
    }
    else
    {
        GU_DetailHandle tmpgdh;
        tmpgdh.allocateAndSet(new GU_Detail());
        if (tmpgdh.gdpNC()->load(path.c_str()))
            gdh = tmpgdh;
    }

    return getVolumePrimitiveFromDetail(gdh, fieldname, fieldindex, fieldtype);
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

