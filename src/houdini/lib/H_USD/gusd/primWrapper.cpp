//
// Copyright 2017 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "primWrapper.h"

#include "context.h"
#include "GT_VtArray.h"
#include "GU_USD.h"
#include "tokens.h"
#include "USD_XformCache.h"
#include "UT_Gf.h"

#include "pxr/base/gf/half.h"
#include "pxr/usd/usdUtils/pipeline.h"
#include "pxr/usd/usdGeom/subset.h"
#include "pxr/usd/usdGeom/primvarsAPI.h"

#include <GA/GA_AttributeFilter.h>
#include <GT/GT_DAIndexedString.h>
#include <GT/GT_DAIndirect.h>
#include <GT/GT_DAVaryingArray.h>
#include <GT/GT_PrimInstance.h>
#include <GT/GT_RefineParms.h>
#include <GT/GT_Util.h>
#include <SYS/SYS_Version.h>
#include <UT/UT_ParallelUtil.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMMPattern.h>
#include <UT/UT_VarEncode.h>

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::map;
using std::set;
using std::vector;

#ifdef DEBUG
#define DBG(x) x
#else
#define DBG(x)
#endif

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((lengthsSuffix, ":lengths"))
);

namespace {

// XXX Temporary until UsdTimeCode::NextTime implemented
const double TIME_SAMPLE_DELTA = 0.001;

GT_PrimitiveHandle _nullPrimReadFunc(
        const UsdGeomImageable&, UsdTimeCode, GusdPurposeSet)
{
    return GT_PrimitiveHandle();
}
        
} // anon namespace

GusdPrimWrapper::GTTypeInfoMap GusdPrimWrapper::s_gtTypeInfoMap;

GusdPrimWrapper::
USDTypeToDefineFuncMap GusdPrimWrapper::s_usdTypeToFuncMap
    = USDTypeToDefineFuncMap();

GusdPrimWrapper::
GTTypeSet GusdPrimWrapper::s_supportedNativeGTTypes
    = GTTypeSet();

namespace {

int 
getPrimType( const GT_PrimitiveHandle &prim )
{
    int primType = prim->getPrimitiveType();
    if( primType == GT_PRIM_INSTANCE ) {
        const GT_PrimInstance *inst = 
            UTverify_cast<const GT_PrimInstance *>(prim.get());
        if( inst && inst->geometry() ) {
            primType = inst->geometry()->getPrimitiveType();
        }
    }   
    return primType;
}

} // close namespace

/// static
GT_PrimitiveHandle GusdPrimWrapper::
defineForWrite(const GT_PrimitiveHandle& sourcePrim,
               const UsdStagePtr& stage,
               const SdfPath& path,
               const GusdContext& ctxt)
{
    GT_PrimitiveHandle gtUsdPrimHandle;

    if( !sourcePrim || !stage )
        return gtUsdPrimHandle;

    int primType = getPrimType( sourcePrim );

    auto mapIt = s_gtTypeInfoMap.find( primType );
    if( mapIt != s_gtTypeInfoMap.end() ) {
        gtUsdPrimHandle = mapIt->second.writeFunc(sourcePrim,
                                                  stage,
                                                  path,
                                                  ctxt);
    }
    return gtUsdPrimHandle;
}

// static
bool GusdPrimWrapper::
getPrimName( const GT_PrimitiveHandle &sourcePrim,
             std::string &primName )
{
    int primType = getPrimType( sourcePrim );

    auto mapIt = s_gtTypeInfoMap.find( primType );
    if( mapIt != s_gtTypeInfoMap.end() &&
            mapIt->second.primNameFunc ) {
        return mapIt->second.primNameFunc(sourcePrim, primName);
    }
    return false;
}

/* static */
const char* GusdPrimWrapper::
getUsdName( int primType )
{
    auto mapIt = s_gtTypeInfoMap.find( primType );
    if( mapIt != s_gtTypeInfoMap.end() ) {
        return mapIt->second.templateName;
    }
    return NULL;
}

/* static */
bool GusdPrimWrapper::
isGroupType( int primType )
{
    auto mapIt = s_gtTypeInfoMap.find( primType );
    if( mapIt != s_gtTypeInfoMap.end() ) {
        return mapIt->second.isGroupType;
    }
    return false;
}


GT_PrimitiveHandle GusdPrimWrapper::
defineForRead( const UsdGeomImageable&  sourcePrim, 
               UsdTimeCode              time,
               GusdPurposeSet           purposes )
{
    GT_PrimitiveHandle gtUsdPrimHandle;

    // Find the function registered for the source prim's type
    // to define the prim from read and call that function.
    if(sourcePrim) {
        const TfToken& typeName = sourcePrim.GetPrim().GetTypeName();
        USDTypeToDefineFuncMap::const_accessor caccessor;
        if(s_usdTypeToFuncMap.find(caccessor, typeName)) {
            gtUsdPrimHandle = caccessor->second(sourcePrim,time,purposes);
        }
        else {
            // If no function is registered for the prim's type, try to
            // find a supported base type.
            const TfType& baseType = TfType::Find<UsdSchemaBase>();
            const TfType& derivedType
                = baseType.FindDerivedByName(typeName.GetText());

            vector<TfType> ancestorTypes;
            derivedType.GetAllAncestorTypes(&ancestorTypes);

            for(size_t i=1; i<ancestorTypes.size(); ++i) {
                const TfType& ancestorType = ancestorTypes[i];
                vector<string> typeAliases = baseType.GetAliases(ancestorType);
                typeAliases.push_back(ancestorType.GetTypeName());

                for(auto const& typeAlias : typeAliases) {
                    if(s_usdTypeToFuncMap.find(caccessor, TfToken(typeAlias))) {
                        gtUsdPrimHandle = caccessor->second(sourcePrim,time,purposes);
                        USDTypeToDefineFuncMap::accessor accessor;
                        s_usdTypeToFuncMap.insert(accessor, typeName);
                        accessor->second = caccessor->second;
                        TF_WARN("Type \"%s\" not registered, using base type \"%s\".",
                                typeName.GetText(), typeAlias.c_str());
                        break;
                    }
                }
                if(gtUsdPrimHandle) break;
            }

            if(!gtUsdPrimHandle) {
                // If we couldn't find a function for the prim's type or any 
                // of it's base types, register a function which returns an
                // empty prim handle.
                registerPrimDefinitionFuncForRead(typeName, _nullPrimReadFunc);
                TF_WARN("Couldn't read unsupported USD prim type \"%s\".",
                        typeName.GetText());
            }
        }
    }
    return gtUsdPrimHandle;
}


bool GusdPrimWrapper::
registerPrimDefinitionFuncForWrite(int gtPrimId,
                                   DefinitionForWriteFunction writeFunc,
                                   GetPrimNameFunction primNameFunc,
                                   bool isGroupType,
                                   const char *typeTemplateName )
{
    if(s_gtTypeInfoMap.find(gtPrimId) != s_gtTypeInfoMap.end()) {
        return false;
    }

    s_gtTypeInfoMap[gtPrimId] = GTTypeInfo( writeFunc, primNameFunc, 
                                            isGroupType, typeTemplateName );
    s_supportedNativeGTTypes.insert(gtPrimId);

    return true;
}


bool GusdPrimWrapper::
registerPrimDefinitionFuncForRead(const TfToken& usdTypeName,
                                  DefinitionForReadFunction func)
{
    USDTypeToDefineFuncMap::accessor accessor;
    if(! s_usdTypeToFuncMap.insert(accessor, usdTypeName)) {
        return false;
    }

    accessor->second = func;

    return true;
}

bool GusdPrimWrapper::
isGTPrimSupported(const GT_PrimitiveHandle& prim)
{
    if (!prim) return false;
    
    const int primType = prim->getPrimitiveType();

    return s_supportedNativeGTTypes.find(primType)
        != s_supportedNativeGTTypes.end();
}

//-------------------------------------------------------------------------

map<GT_Owner, TfToken> GusdPrimWrapper::s_ownerToUsdInterp {
    {GT_OWNER_POINT,    UsdGeomTokens->vertex},
    {GT_OWNER_VERTEX,   UsdGeomTokens->faceVarying},
    {GT_OWNER_UNIFORM,  UsdGeomTokens->uniform},
    {GT_OWNER_CONSTANT, UsdGeomTokens->constant}};

map<GT_Owner, TfToken> GusdPrimWrapper::s_ownerToUsdInterpCurve {
    {GT_OWNER_VERTEX,   UsdGeomTokens->vertex},
    {GT_OWNER_UNIFORM,  UsdGeomTokens->uniform},
    {GT_OWNER_CONSTANT, UsdGeomTokens->constant}};

GusdPrimWrapper::GusdPrimWrapper()
    : m_time( UsdTimeCode::Default() )
    , m_visible( true )
    , m_lastXformSet( UsdTimeCode::Default() )
    , m_lastXformCompared( UsdTimeCode::Default() )
{
}

GusdPrimWrapper::GusdPrimWrapper( 
        const UsdTimeCode &time, 
        const GusdPurposeSet &purposes )
    : GT_Primitive()
    , m_time( time )
    , m_purposes( purposes )
    , m_visible( true )
    , m_lastXformSet( UsdTimeCode::Default() )
    , m_lastXformCompared( UsdTimeCode::Default() )
{
}

GusdPrimWrapper::GusdPrimWrapper( const GusdPrimWrapper &in )
    : GT_Primitive(in)
    , m_time( in.m_time )
    , m_purposes( in.m_purposes )
    , m_visible( in.m_visible )
    , m_lastXformSet( in.m_lastXformSet )
    , m_lastXformCompared( in.m_lastXformCompared )
{
}

GusdPrimWrapper::~GusdPrimWrapper()
{
}


bool 
GusdPrimWrapper::isValid() const
{ 
    return false;
}

/// Record the "usdxform" point attribute with the transform that was applied
/// to the geometry, so that the inverse transform can be applied when
/// round-tripping.
static void
Gusd_RecordXformAttrib(GU_Detail &destgdp, const GA_Range &ptrange,
                       const UT_Matrix4D &xform)
{
    static constexpr UT_StringLit theUsdXformAttrib("usdxform");
    static constexpr GA_AttributeOwner owner = GA_ATTRIB_POINT;
    static constexpr int tuple_size = UT_Matrix4D::tuple_size;

    GA_RWHandleM4D xform_attrib =
        destgdp.findFloatTuple(owner, theUsdXformAttrib.asRef(), tuple_size);
    if (!xform_attrib.isValid())
    {
        xform_attrib = destgdp.addFloatTuple(
                owner, theUsdXformAttrib.asHolder(), tuple_size,
                GA_Defaults(GA_Defaults::matrix4()), nullptr, nullptr,
                GA_STORE_REAL64);

        // Do not set any typeinfo - the usdxform attribute shouldn't be
        // modified by xform SOPs.
        xform_attrib->setTypeInfo(GA_TYPE_VOID);
    }

    for (GA_Offset offset : ptrange)
        xform_attrib.set(offset, xform);
}

/// Record the "usdvisibility" prim attribute for round-tripping, if visibility
/// was authored.
static void
Gusd_RecordVisibilityAttrib(GU_Detail &destgdp, const GA_Range &primrange,
                            const UsdGeomImageable &usdprim,
                            const UsdTimeCode &timecode)
{
    static constexpr UT_StringLit theUsdVisibilityAttribName("usdvisibility");

    UsdAttribute vis_attr = usdprim.GetVisibilityAttr();
    if (!vis_attr || !vis_attr.IsAuthored())
        return;

    TfToken visibility_token;
    vis_attr.Get(&visibility_token, timecode);

    GA_RWBatchHandleS usdvisibility_attrib = destgdp.addStringTuple(
        GA_ATTRIB_PRIMITIVE, theUsdVisibilityAttribName.asHolder(), 1);
    if (!usdvisibility_attrib.isValid())
        return;

    const UT_StringHolder visibility_str =
        GusdUSD_Utils::TokenToStringHolder(visibility_token);

    usdvisibility_attrib.set(primrange, visibility_str);
}

/// Mark the specified attributes as non-transforming.
static void
Gusd_MarkNonTransformingAttribs(GU_Detail &gdp,
                                const UT_StringRef &non_transforming_primvars)
{
    static constexpr GA_AttributeOwner owners[] = {
        GA_ATTRIB_POINT, GA_ATTRIB_VERTEX, GA_ATTRIB_PRIMITIVE,
        GA_ATTRIB_DETAIL};

    UT_Array<GA_Attribute *> attribs;
    auto filter =
        GA_AttributeFilter::selectByPattern(non_transforming_primvars);

    gdp.getAttributes().matchAttributes(
        filter, owners, SYSarraySize(owners), attribs);

    for (GA_Attribute *attrib : attribs)
        attrib->setTypeInfo(GA_TYPE_VOID);
}

static void
Gusd_CreatePathAttrib(
        GU_Detail& gdp,
        GA_AttributeOwner owner,
        const GT_RefineParms& rparms,
        const UT_StringRef& filename,
        const UsdGeomImageable& prim)
{
    if (GT_RefineParms::getBool(&rparms, GUSD_REFINE_ADDPATHATTRIB, true))
    {
        GA_RWBatchHandleS path_attr(
                gdp.addStringTuple(owner, GUSD_PATH_ATTR, 1));
        path_attr.set(GA_Range(gdp.getIndexMap(owner)), filename);
    }

    if (GT_RefineParms::getBool(&rparms, GUSD_REFINE_ADDPRIMPATHATTRIB, true))
    {
        GA_RWBatchHandleS prim_path_attr(
                gdp.addStringTuple(owner, GUSD_PRIMPATH_ATTR, 1));

        prim_path_attr.set(
                GA_Range(gdp.getIndexMap(owner)), prim.GetPath().GetString());
    }
}

bool
GusdPrimWrapper::unpack(
        UT_Array<GU_DetailHandle>& details,
        const UT_StringRef& fileName,
        const SdfPath& primPath,
        const UT_Matrix4D& xform,
        fpreal frame,
        const char* viewportLod,
        GusdPurposeSet purposes,
        const GT_RefineParms& rparms) const
{
    UsdGeomImageable prim = getUsdPrim();

    UT_IntrusivePtr<const GT_Primitive> gtPrim = this;
    if (prim.GetPrim().IsInPrototype())
        gtPrim = copyTransformed(new GT_Transform(&xform, 1));

    const exint start = details.entries();
    GT_Util::makeGEO(details, *gtPrim, &rparms);

    // For the details that were created, create the prim path attributes,
    // etc, and apply the prim xform.
    for (exint i = start, n = details.entries(); i < n; ++i)
    {
        GU_DetailHandle& gdh = details[i];
        GU_DetailHandleAutoWriteLock gdp(gdh);

        // Add usdpath and usdprimpath attributes to unpacked geometry.
        Gusd_CreatePathAttrib(
                *gdp, GA_ATTRIB_PRIMITIVE, rparms, fileName, prim);
        if (gdp->getNumPrimitives() == 0 && gdp->getNumPoints() > 0)
        {
            // Record path on the points if we're importing only points. The
            // prim attrib needs to also exist for merging with other prim
            // types like meshes (to avoid losing the prim attrib from the
            // promotion in GUmatchAttributesAndMerge())
            Gusd_CreatePathAttrib(
                    *gdp, GA_ATTRIB_POINT, rparms, fileName, prim);
        }

        // Only create the usdxform attribute for point-based prims.
        // Transforming primitives already store the USD xform as part of
        // their transform, and the compensation is handled by Adjust
        // Transforms for Input Hierarchy on SOP Import.
        if (!gdp->hasTransformingPrimitives()
            && GT_RefineParms::getBool(
                    &rparms, GUSD_REFINE_ADDXFORMATTRIB, true))
        {
            Gusd_RecordXformAttrib(*gdp, gdp->getPointRange(), xform);
        }

        if (GT_RefineParms::getBool(
                    &rparms, GUSD_REFINE_ADDVISIBILITYATTRIB, true))
        {
            Gusd_RecordVisibilityAttrib(
                    *gdp, gdp->getPrimitiveRange(), prim, m_time);
        }

        UT_String non_transforming_primvars;
        rparms.import(
                GUSD_REFINE_NONTRANSFORMINGPATTERN, non_transforming_primvars);
        Gusd_MarkNonTransformingAttribs(*gdp, non_transforming_primvars);

        // Apply the prim's transform. Note that this is done after marking
        // any non-transforming attributes above.
        gdp->transform(xform);
    }

    return true;
}

bool
GusdPrimWrapper::redefine( 
   const UsdStagePtr& stage,
   const SdfPath& path,
   const GusdContext& ctxt,
   const GT_PrimitiveHandle& sourcePrim )
{
    return false;
}

bool 
GusdPrimWrapper::updateFromGTPrim(
    const GT_PrimitiveHandle&  sourcePrim,
    const UT_Matrix4D&         houXform,
    const GusdContext&         ctxt,
    GusdSimpleXformCache&      xformCache)
{
    // Set the active state of the UsdPrim if any "usdactive" attributes exist
    updateActiveFromGTPrim(sourcePrim, ctxt.time);

    return true;
}

void
GusdPrimWrapper::setVisibility(const TfToken& visibility, UsdTimeCode time)
{
    if( visibility == UsdGeomTokens->invisible ) {
        m_visible = false;
    } else {
        m_visible = true;
    }

    const UsdAttribute visAttr = getUsdPrim().GetVisibilityAttr();
    if( visAttr.IsValid() ) {
        TfToken oldVal;
        if( !visAttr.Get( &oldVal, 
                          UsdTimeCode::Default() ) || oldVal != UsdGeomTokens->invisible ) {
            visAttr.Set(UsdGeomTokens->invisible, UsdTimeCode::Default()); 
        }
        visAttr.Set(visibility, time); 
    }
}

void
GusdPrimWrapper::updateVisibilityFromGTPrim(
        const GT_PrimitiveHandle& sourcePrim,
        UsdTimeCode time,
        bool forceWrite )
{
    // If we're tracking visibility, set this prim's default state to
    // invisible. File-per-frame exports rely on this if the prim isn't
    // persistent throughout the frame range.
    GT_Owner attrOwner;
    GT_DataArrayHandle houAttr
        = sourcePrim->findAttribute(GUSD_VISIBLE_ATTR, attrOwner, 0);
    if(houAttr) {
        GT_String visible = houAttr->getS(0);
        if (visible) {
            if (strcmp(visible, "inherited") == 0) {
                setVisibility(UsdGeomTokens->inherited, time);
            } else if (strcmp(visible, "invisible") == 0) {
                setVisibility(UsdGeomTokens->invisible, time);
            }
        }
    }
    else if ( forceWrite ) {
        if(isVisible()) {
            setVisibility(UsdGeomTokens->inherited, time);
        } else {
            setVisibility(UsdGeomTokens->invisible, time);
        }
    }
}

void
GusdPrimWrapper::updateActiveFromGTPrim(
        const GT_PrimitiveHandle& sourcePrim,
        UsdTimeCode time)
{
    UsdPrim prim = getUsdPrim().GetPrim();

    GT_Owner attrOwner;
    GT_DataArrayHandle houAttr
        = sourcePrim->findAttribute(GUSD_ACTIVE_ATTR, attrOwner, 0);
    if (houAttr) {
        GT_String state = houAttr->getS(0);
        if (state) {
            if (strcmp(state, "active") == 0) {
                prim.SetActive(true);
            } else if (strcmp(state, "inactive") == 0) {
                prim.SetActive(false);
            }
        }
    }
}

void
GusdPrimWrapper::updateTransformFromGTPrim( const GfMatrix4d &xform, 
                                            UsdTimeCode time, bool force )
{
    UsdGeomImageable usdGeom = getUsdPrim();
    UsdGeomXformable prim( usdGeom );

    // Determine if we need to clear previous transformations from a stronger
    // opinion on the stage before authoring ours.
    UsdStagePtr stage = usdGeom.GetPrim().GetStage();
    UsdEditTarget currEditTarget = stage->GetEditTarget();

    // If the edit target does no mapping, it is most likely the session
    // layer which means it is in the local layer stack and can overlay
    // any xformOps.
    if ( !currEditTarget.GetMapFunction().IsNull() && 
         !currEditTarget.GetMapFunction().IsIdentity() ) {
        bool reset;
        std::vector<UsdGeomXformOp> xformVec = prim.GetOrderedXformOps(&reset);

        // The xformOps attribute is static so we only check if we haven't
        // changed anything yet. In addition nothing needs to be cleared if it
        // was previously empty.
        if (m_lastXformSet.IsDefault() && !xformVec.empty()) {
            // Load the root layer for temp, stronger opinion changes.
            stage->GetRootLayer()->SetPermissionToSave(false);
            stage->SetEditTarget(stage->GetRootLayer());
            UsdGeomXformable stagePrim( getUsdPrim() );

            // Clear the xformOps on the stronger layer, so our weaker edit
            // target (with mapping across a reference) can write out clean,
            // new transforms.
            stagePrim.ClearXformOpOrder();
            stage->SetEditTarget(currEditTarget);
        }
    }

    if( !prim )
        return;

    // Try to avoid setting the transform when we can.
    // If force it true, always write the transform (used when writting per frame)
    bool setKnot = true;
    if( !force ) {
        
        // Has the transform has been set at least once
        if( !m_lastXformSet.IsDefault() ) {

            // Is the transform at this frame the same as the last frame
            if( GfIsClose(xform, m_xformCache, 1e-10) ) {
                setKnot = false;
                m_lastXformCompared = time;
            }
            else {
                // If the transform has been held for more than one frame, 
                // set a knot on the last frame
                if( m_lastXformCompared != m_lastXformSet ) {
                    prim.MakeMatrixXform().Set( m_xformCache, m_lastXformCompared );
                }
            }
        }
        else {
            // If the transform is an identity, don't set it
            if( GfIsClose(xform, GfMatrix4d(1), 1e-10) ) {
                setKnot = false;
                m_lastXformCompared = time;
            }
            else {

                // If the transform was identity and now isn't, set a knot on the last frame
                if( !m_lastXformCompared.IsDefault() ) {
                    prim.MakeMatrixXform().Set( GfMatrix4d(1.0), m_lastXformCompared );
                }
            }
        }
    }

    if( setKnot ) {
        prim.MakeMatrixXform().Set( xform, time );
        m_xformCache = xform;
        m_lastXformSet = time;
        m_lastXformCompared = time;
    }
}

bool
GusdPrimWrapper::updateAttributeFromGTPrim( 
    GT_Owner owner, 
    const std::string& name,
    const GT_DataArrayHandle& houAttr, 
    UsdAttribute& usdAttr, 
    UsdTimeCode time )
{
    // return true if we need to set the value
    if( !houAttr || !usdAttr )
       return false;

    // Check to see if the current value of this attribute has changed 
    // from the last time we set the value.

    AttrLastValueKeyType key(owner, name);
    auto it = m_lastAttrValueDict.find( key );
    if( it == m_lastAttrValueDict.end()) { 

        // Set the value for the first time
        m_lastAttrValueDict.emplace(
            key, AttrLastValueEntry( time, houAttr->harden()));

        GusdGT_Utils::setUsdAttribute(usdAttr, houAttr, time);
        return true;
    } 
    else {
        AttrLastValueEntry& entry = it->second;
        if( houAttr->isEqual( *entry.data )) {

            // The value are the as before. Don't set.
            entry.lastCompared = time;
            return false;
        }
        else {
            if( entry.lastCompared != entry.lastSet ) {
                // Set a value on the last frame the previous value was valid.
                GusdGT_Utils::setUsdAttribute(usdAttr, entry.data, entry.lastCompared);
            }
            
            // set the new value
            GusdGT_Utils::setUsdAttribute(usdAttr, houAttr, time);

            // save this value to compare on later frames
            entry.data = houAttr->harden();
            entry.lastSet = entry.lastCompared = time;
            return true;
        }
    }
    return false;
}

bool
GusdPrimWrapper::updatePrimvarFromGTPrim( 
    const TfToken&            name,
    const GT_Owner&           owner,
    const TfToken&            interpolation,
    UsdTimeCode               time,
    const GT_DataArrayHandle& dataIn )
{
    GT_DataArrayHandle data = dataIn;
    UsdGeomImageable prim( getUsdPrim() );

    // cerr << "updatePrimvarFromGTPrim: " 
    //         << prim.GetPrim().GetPath() << ":" << name << ", " << interpolation 
    //         << ", entries = " << dataIn->entries() << endl;

    AttrLastValueKeyType key(owner, name);
    auto it = m_lastAttrValueDict.find( key );
    if( it == m_lastAttrValueDict.end() ) {

        // If we're creating an overlay this primvar might already be
        // authored on the prim. If the primvar is indexed we need to 
        // block the indices attribute, because we flatten indexed
        // primvars.
        if( UsdGeomPrimvar primvar = prim.GetPrimvar(name) ) {
            if( primvar.IsIndexed() ) {
                primvar.BlockIndices();
            }
        }

        m_lastAttrValueDict.emplace(
            key, AttrLastValueEntry( time, data->harden()));

        GusdGT_Utils::setPrimvarSample( prim, name, data, interpolation, time );
    }
    else {
        AttrLastValueEntry& entry = it->second;
        if( data->isEqual( *entry.data )) {
            entry.lastCompared = time;
            return false;
        }
        else {
            if( entry.lastCompared != entry.lastSet ) {
                GusdGT_Utils::setPrimvarSample( prim, name, entry.data, interpolation, entry.lastCompared );
            }
            
             if( UsdGeomPrimvar primvar = prim.GetPrimvar(name) ) {
                if( primvar.IsIndexed() ) {
                    primvar.BlockIndices();
                }
            }

            GusdGT_Utils::setPrimvarSample( prim, name, data, interpolation, time );
            entry.data = data->harden();
            entry.lastSet = entry.lastCompared = time;
            return true;
        }
    }
    return true;
}

bool
GusdPrimWrapper::updatePrimvarFromGTPrim( 
    const GT_AttributeListHandle& gtAttrs,
    const GusdGT_AttrFilter&      primvarFilter,
    const TfToken&                interpolation,
    UsdTimeCode                   time )
{
    UsdGeomImageable prim( getUsdPrim() );
    const GT_AttributeMapHandle attrMapHandle = gtAttrs->getMap();

    for(GT_AttributeMap::const_names_iterator mapIt=attrMapHandle->begin();
            !mapIt.atEnd(); ++mapIt) {

        string attrname = mapIt->first.toStdString();

        if(!primvarFilter.matches(attrname)) 
            continue;

        const int attrIndex = attrMapHandle->get(attrname);
        const GT_Owner owner = attrMapHandle->getOriginalOwner(attrIndex);
        GT_DataArrayHandle attrData = gtAttrs->get(attrIndex);

        // Decode Houdini geometry attribute names to get back the original
        // USD primvar name. This allows round tripping of namespaced
        // primvars from USD -> Houdini -> USD.
        UT_StringHolder name = UT_VarEncode::decodeAttrib(attrname);

        updatePrimvarFromGTPrim( 
                    TfToken( name.toStdString() ),
                    owner, 
                    interpolation, 
                    time, 
                    attrData );
    }
    return true;
}

void
GusdPrimWrapper::clearCaches()
{
    m_lastAttrValueDict.clear();
}

void
GusdPrimWrapper::addLeadingBookend( double curFrame, double startFrame )
{
    if( curFrame != startFrame ) {
        double bookendFrame = curFrame - TIME_SAMPLE_DELTA;

        // Ensure the stage start frame <= bookendFrame
        UsdStagePtr stage = getUsdPrim().GetPrim().GetStage();
        if(stage) {
            double startFrame = stage->GetStartTimeCode();
            if( startFrame > bookendFrame) {
                stage->SetStartTimeCode(bookendFrame);
            }
        }

        const UsdAttribute attr = getUsdPrim().GetVisibilityAttr();
        attr.Set(UsdGeomTokens->invisible, UsdTimeCode(bookendFrame));
        attr.Set(UsdGeomTokens->inherited, UsdTimeCode(curFrame));
    }
}

void
GusdPrimWrapper::addTrailingBookend( double curFrame )
{
    double bookendFrame = curFrame - TIME_SAMPLE_DELTA;

    const UsdAttribute attr = getUsdPrim().GetVisibilityAttr();
    attr.Set(UsdGeomTokens->inherited, UsdTimeCode(bookendFrame));
    attr.Set(UsdGeomTokens->invisible, UsdTimeCode(curFrame));
}


namespace {


const char*
Gusd_GetCStr(const std::string& o)  { return o.c_str(); }

const char*
Gusd_GetCStr(const TfToken& o)      { return o.GetText(); }

const char*
Gusd_GetCStr(const SdfAssetPath& o) { return o.GetAssetPath().c_str(); }


/// Convert a value to a GT_DataArray.
/// The value is either a POD type or a tuple of PODs.
template <class ELEMTYPE, class GTARRAY, GT_Type GT_TYPE=GT_TYPE_NONE>
GT_DataArray*
Gusd_ConvertTupleToGt(const VtValue& val)
{
    TF_DEV_AXIOM(val.IsHolding<ELEMTYPE>());

    const auto& heldVal = val.UncheckedGet<ELEMTYPE>();

    return new GTARRAY((const typename GTARRAY::data_type*)&heldVal,
                       1, GusdGetTupleSize<ELEMTYPE>(), GT_TYPE);
}

/// Returns the element size if the attribute is a primvar, or 1 otherwise.
int
Gusd_GetElementSize(const UsdAttribute &attr)
{
    UsdGeomPrimvar primvar(attr);
    return primvar ? primvar.GetElementSize() : 1;
}

/// Convert a VtArray to a GT_DataArray.
/// The elements of the array are either PODs, or tuples of PODs (eg., vectors).
template <class ELEMTYPE, class GTARRAY, GT_Type GT_TYPE=GT_TYPE_NONE>
GT_DataArray*    
Gusd_ConvertTupleArrayToGt(const UsdAttribute& attr, const VtValue& val)
{
    TF_DEV_AXIOM(val.IsHolding<VtArray<ELEMTYPE> >());

    const int tupleSize = GusdGetTupleSize<ELEMTYPE>();

    const auto& array = val.UncheckedGet<VtArray<ELEMTYPE> >();
    if (array.size() > 0) {
        const int elementSize = Gusd_GetElementSize(attr);
        if (elementSize > 0) {

            // Only lookup primvar role for non POD types
            // (vectors, matrices, etc.), and only if it has not
            // been specified via template argument.
            GT_Type type = GT_TYPE;
            if (type == GT_TYPE_NONE) {
                // A GT_Type has not been specified using template args.
                // We can try to derive a type from the role on the primvar's 
                // type name, but only worth doing for types that can
                // actually have roles (eg., not scalars)
                if (tupleSize > 1) {
                    type = GusdGT_Utils::getType(attr.GetTypeName());
                }
            }

            if (elementSize == 1) {
                return new GusdGT_VtArray<ELEMTYPE>(array, type);
            } else {
                const size_t numTuples = array.size()/elementSize;
                const int gtTupleSize = elementSize*tupleSize;

                if (numTuples*elementSize == array.size()) {
                    return new GTARRAY(
                        (const typename GTARRAY::data_type*)array.cdata(),
                        numTuples, gtTupleSize);
                } else {
                    GUSD_WARN().Msg(
                        "Invalid primvar <%s>: array size [%zu] is not a "
                        "multiple of the elementSize [%d].",
                        attr.GetPath().GetText(),
                        array.size(), elementSize);
                }
            }
        } else {
            GUSD_WARN().Msg(
                "Invalid primvar <%s>: illegal elementSize [%d].",
                attr.GetPath().GetText(),
                elementSize);
        }
    }
    return nullptr;
}


/// Convert a string-like value to a GT_DataArray.
template <typename ELEMTYPE>
GT_DataArray*
Gusd_ConvertStringToGt(const VtValue& val)
{
    TF_DEV_AXIOM(val.IsHolding<ELEMTYPE>());
    
    auto gtString = new GT_DAIndexedString(1);
    gtString->setString(0, 0, Gusd_GetCStr(val.UncheckedGet<ELEMTYPE>()));
    return gtString;
}


/// Convert a VtArray of string-like values to a GT_DataArray.
template <typename ELEMTYPE>
GT_DataArray*
Gusd_ConvertStringArrayToGt(const UsdAttribute& attr, const VtValue& val)
{
    TF_DEV_AXIOM(val.IsHolding<VtArray<ELEMTYPE> >());

    const auto& array = val.UncheckedGet<VtArray<ELEMTYPE> >();
    if (array.size() > 0) {
        const int elementSize = Gusd_GetElementSize(attr);
        if (elementSize > 0) {
            const size_t numTuples = array.size()/elementSize;
            if (numTuples*elementSize == array.size()) {
                const ELEMTYPE* values = array.cdata();

                auto gtStrings = new GT_DAIndexedString(numTuples, elementSize);

                for (size_t i = 0; i < numTuples; ++i) {
                    for (int cmp = 0; cmp < elementSize; ++cmp, ++values) {
                        gtStrings->setString(i, cmp, Gusd_GetCStr(*values));
                    }
                }
                return gtStrings;
            } else {
                GUSD_WARN().Msg(
                    "Invalid primvar <%s>: array size [%zu] is not a "
                        "multiple of the elementSize [%d].",
                        attr.GetPath().GetText(),
                        array.size(), elementSize);
            }
        }  else {
            GUSD_WARN().Msg(
                "Invalid primvar <%s>: illegal elementSize [%d].",
                attr.GetPath().GetText(),
                elementSize);
        }
    }
    return nullptr;
}

GT_DataArrayHandle
Gusd_CreateConstantIndirect(exint n, const GT_DataArrayHandle &constant_data)
{
    UT_IntrusivePtr<GT_DANumeric<exint>> indirect =
        new GT_DANumeric<exint>(n, 1);
    exint *data = indirect->data();
    std::fill(data, data + n, 0);

    return new GT_DAIndirect(indirect, constant_data);
}

template <typename T>
static GT_DataArrayHandle
Gusd_ExpandSTToUV(const GT_DataArrayHandle &st)
{
    UT_IntrusivePtr<GT_DANumeric<T>> uv =
        new GT_DANumeric<T>(st->entries(), 3, GT_TYPE_TEXTURE);

    // Copy first and second components from st.
    UT_ASSERT(st->getTupleSize() == 2);
    st->fillArray(uv->data(), 0, st->entries(), /* tsize */ 2,
                  /* stride */ 3);

    // Initialize third component to 0.
    T *data = uv->data();
    for (exint i = 2, n = 3 * uv->entries(); i < n; i += 3)
        data[i] = 0;

    return uv;
}

/// Add the attribute data to the appropriate GT_AttributeList based on the
/// interpolation and array size.
static void
Gusd_AddAttribute(const UsdAttribute &attr,
                  GT_DataArrayHandle data,
                  const UT_StringHolder &attrname,
                  const TfToken &interpolation,
                  int min_uniform,
                  int min_point,
                  int min_vertex,
                  const string &prim_path,
                  const GT_DataArrayHandle &remap_indices,
                  GT_AttributeListHandle *vertex,
                  GT_AttributeListHandle *point,
                  GT_AttributeListHandle *primitive,
                  GT_AttributeListHandle *constant,
                  UT_StringArray &constant_attribs,
                  UT_StringArray &scalar_constant_attribs,
                  UT_StringArray &bool_attribs)
{
    if (interpolation == UsdGeomTokens->vertex ||
        interpolation == UsdGeomTokens->varying)
    {
        // remap_indices is only used for expanding per-segment
        // primvars to point attributes.
        if (remap_indices && interpolation == UsdGeomTokens->varying)
        {
            if (data->entries() < min_vertex)
            {
                TF_WARN("Not enough values found for attribute: %s:%s. "
                        "%zd value(s) given for %d segment end points.",
                        prim_path.c_str(), attr.GetName().GetText(),
                        size_t(data->entries()), min_vertex);
                return;
            }

            data = new GT_DAIndirect(remap_indices, data);
        }

        if (data->entries() < min_point)
        {
            TF_WARN("Not enough values found for attribute: %s:%s. "
                    "%zd values given for %d points.",
                    prim_path.c_str(), attr.GetName().GetText(),
                    size_t(data->entries()), min_point);
        }
        else
        {
            if (point)
                *point = (*point)->addAttribute(attrname, data, true);
        }
    }
    else if (interpolation == UsdGeomTokens->faceVarying)
    {
        if (data->entries() < min_vertex)
        {
            TF_WARN("Not enough values found for attribute: %s:%s. "
                    "%zd values given for %d vertices.",
                    prim_path.c_str(), attr.GetName().GetText(),
                    size_t(data->entries()), min_vertex);
        }
        else if (vertex)
            *vertex = (*vertex)->addAttribute(attrname, data, true);
    }
    else if (interpolation == UsdGeomTokens->uniform)
    {
        if (data->entries() < min_uniform)
        {
            TF_WARN("Not enough values found for attribute: %s:%s. "
                    "%zd values given for %d faces.",
                    prim_path.c_str(), attr.GetName().GetText(),
                    size_t(data->entries()), min_uniform);
        }
        else if (primitive)
            *primitive = (*primitive)->addAttribute(attrname, data, true);
    }
    else if (interpolation == UsdGeomTokens->constant)
    {
        // Promote down to a prim / point attribute if possible.
        // GU_MergeUtils might do this anyways, so it's better to have it
        // happen consistently so that attributes don't move around
        // unexpectedly. We record these attributes in
        // usdconfigconstantattribs to improve round-tripping.
        if (primitive)
        {
            GT_DataArrayHandle indirect = Gusd_CreateConstantIndirect(
                min_uniform, data);
            *primitive = (*primitive)->addAttribute(attrname, indirect, true);
        }
        else if (point)
        {
            *point = (*point)->addAttribute(
                attrname, Gusd_CreateConstantIndirect(min_point, data), true);
        }
        else if (constant)
        {
            *constant = (*constant)->addAttribute(attrname.c_str(), data, true);
        }

        if (primitive || point)
        {
            if (attr.GetTypeName().IsScalar())
                scalar_constant_attribs.append(attrname);
            else
                constant_attribs.append(attrname);
        }
    }

    if (attr.GetTypeName().GetScalarType() == SdfValueTypeNames->Bool)
        bool_attribs.append(attrname);
}

static void
Gusd_RecordAttribPattern(
        const UT_StringArray& attrib_list,
        GT_AttributeListHandle& constant,
        const UT_StringHolder& config_attrib)
{
    if (!attrib_list.isEmpty() && constant)
    {
        UT_WorkBuffer buf;
        buf.append(attrib_list, " ");

        UT_StringHolder attrib_pattern(std::move(buf));

        auto da = UTmakeIntrusive<GT_DAIndexedString>(1);
        da->setString(0, 0, attrib_pattern);
        constant = constant->addAttribute(config_attrib, da, true);
    }
}

} // namespace


/* static */
GT_DataArrayHandle
GusdPrimWrapper::convertPrimvarData( const UsdGeomPrimvar& primvar, UsdTimeCode time )
{
    VtValue val;
    if (!primvar.ComputeFlattened(&val, time)) {
        return nullptr;
    }
    return convertAttributeData(primvar, val);
}


/* static */
GT_DataArrayHandle
GusdPrimWrapper::convertAttributeData(const UsdAttribute &attr,
                                      const VtValue &val)
{
#define _CONVERT_TUPLE(elemType, gtArray, gtType)                              \
    if (val.IsHolding<elemType>())                                             \
    {                                                                          \
        return Gusd_ConvertTupleToGt<elemType, gtArray, gtType>(val);          \
    }                                                                          \
    else if (val.IsHolding<VtArray<elemType>>())                               \
    {                                                                          \
        return Gusd_ConvertTupleArrayToGt<elemType, gtArray, gtType>(          \
            attr, val);                                                        \
    }

    // Check for most common value types first.
    _CONVERT_TUPLE(GfVec3f, GT_Real32Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(GfVec2f, GT_Real32Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(float,   GT_Real32Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(int,     GT_Int32Array,  GT_TYPE_NONE);

    // Scalars
    _CONVERT_TUPLE(double,  GT_Real64Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(GfHalf,  GT_Real16Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(int64,   GT_Int64Array,  GT_TYPE_NONE);
    _CONVERT_TUPLE(unsigned char, GT_UInt8Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(bool,    GT_UInt8Array,  GT_TYPE_NONE);

    // TODO: UInt, UInt64 (convert to int32/int64?)
    
    // Vec2
    _CONVERT_TUPLE(GfVec2d, GT_Real64Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(GfVec2h, GT_Real16Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(GfVec2i, GT_Int32Array, GT_TYPE_NONE);

    // Vec3
    _CONVERT_TUPLE(GfVec3d, GT_Real64Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(GfVec3h, GT_Real16Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(GfVec3i, GT_Int32Array,  GT_TYPE_NONE);

    // Vec4
    _CONVERT_TUPLE(GfVec4d, GT_Real64Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(GfVec4f, GT_Real32Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(GfVec4h, GT_Real16Array, GT_TYPE_NONE);
    _CONVERT_TUPLE(GfVec4i, GT_Int32Array,  GT_TYPE_NONE);

    // Quat
    _CONVERT_TUPLE(GfQuatd, GT_Real64Array, GT_TYPE_QUATERNION);
    _CONVERT_TUPLE(GfQuatf, GT_Real32Array, GT_TYPE_QUATERNION);
    _CONVERT_TUPLE(GfQuath, GT_Real16Array, GT_TYPE_QUATERNION);

    // Matrices
    _CONVERT_TUPLE(GfMatrix3d, GT_Real64Array, GT_TYPE_MATRIX3);
    _CONVERT_TUPLE(GfMatrix4d, GT_Real64Array, GT_TYPE_MATRIX);
    // TODO: Correct GT_Type for GfMatrix2d?
    _CONVERT_TUPLE(GfMatrix2d, GT_Real64Array, GT_TYPE_NONE);

#undef _CONVERT_TUPLE

#define _CONVERT_STRING(elemType)                                              \
    if (val.IsHolding<elemType>())                                             \
    {                                                                          \
        return Gusd_ConvertStringToGt<elemType>(val);                          \
    }                                                                          \
    else if (val.IsHolding<VtArray<elemType>>())                               \
    {                                                                          \
        return Gusd_ConvertStringArrayToGt<elemType>(attr, val);               \
    }

    _CONVERT_STRING(std::string);
    _CONVERT_STRING(TfToken);
    _CONVERT_STRING(SdfAssetPath);

#undef _CONVERT_STRING

    return nullptr;
}

static bool
Gusd_HasSchemaAttrib(const UsdPrimDefinition &prim_defn,
                     const TfToken &attr_name)
{
    return prim_defn.GetSpecType(attr_name) != SdfSpecTypeUnknown;
}

void
GusdPrimWrapper::loadPrimvars( 
    const UsdPrimDefinition&  prim_defn,
    UsdTimeCode               time,
    const GT_RefineParms*     rparms,
    int                       minUniform,
    int                       minPoint,
    int                       minVertex,
    const string&             primPath,
    GT_AttributeListHandle*   vertex,
    GT_AttributeListHandle*   point,
    GT_AttributeListHandle*   primitive,
    GT_AttributeListHandle*   constant,
    const GT_DataArrayHandle& remapIndicies ) const
{
    // Primvars will be loaded if they match a provided pattern.
    // By default, set the pattern to match only "Cd". Then write
    // over this pattern if there is one provided in rparms.
    const char* Cd = "Cd";
    UT_String primvarPatternStr(Cd);
    bool importInheritedPrimvars = false;

    if (rparms) {
        rparms->import(GUSD_REFINE_PRIMVARPATTERN, primvarPatternStr);
        rparms->import(
                GUSD_REFINE_IMPORTINHERITEDPRIMVARS, importInheritedPrimvars);
    }

    UT_StringMMPattern primvarPattern;
    if (primvarPatternStr) {
        primvarPattern.compile(primvarPatternStr);
    }

    std::vector<UsdGeomPrimvar> primvars;
    bool hasCdPrimvar = false;

    const TfToken stName = UsdUtilsGetPrimaryUVSetName();
    bool translateSTtoUV = true;
    if (rparms) {
        rparms->import(GUSD_REFINE_TRANSLATESTTOUV, translateSTtoUV);
    }

    {
        UsdGeomImageable prim = getUsdPrim();

        // Don't translate st -> uv if uv already exists.
        if (translateSTtoUV &&
            (prim.GetPrimvar(GusdTokens->uv) || !prim.GetPrimvar(stName))) {
            translateSTtoUV = false;
        }

        UsdGeomPrimvar colorPrimvar = prim.GetPrimvar(GusdTokens->Cd);
        if (colorPrimvar && colorPrimvar.GetAttr().HasAuthoredValue()) {
            hasCdPrimvar = true;
        }

        // It's common for "Cd" to be the only primvar to load.
        // In this case, avoid getting all other authored primvars.
        if (primvarPatternStr == Cd) {
            if (hasCdPrimvar) {
                primvars.push_back(colorPrimvar);
            } else {
                // There is no authored "Cd" primvar.
                // Try to find "displayColor" instead.
                colorPrimvar = prim.GetPrimvar(UsdGeomTokens->primvarsDisplayColor);
                if (colorPrimvar &&
                    colorPrimvar.GetAttr().HasAuthoredValue()) {
                    primvars.push_back(colorPrimvar);
                }
            }
        } else if (!primvarPattern.isEmpty()) {
            UsdGeomPrimvarsAPI pv_api(prim);
            if (importInheritedPrimvars)
                primvars = pv_api.FindPrimvarsWithInheritance();
            else
                primvars = pv_api.GetAuthoredPrimvars();
        }
    }

    // Is it better to sort the attributes and build the attributes all at once.

    UT_StringArray constant_attribs;
    UT_StringArray scalar_attribs;
    UT_StringArray bool_attribs;
    for( const UsdGeomPrimvar &primvar : primvars )
    {
        // The :lengths primvar for an array attribute is handled when the main
        // data array is encountered.
        if (TfStringEndsWith(primvar.GetName(), _tokens->lengthsSuffix))
            continue;

        DBG(cerr << "loadPrimvar " << primvar.GetPrimvarName() << "\t" << primvar.GetTypeName() << "\t" << primvar.GetInterpolation() << endl);

        UT_StringHolder name =
            GusdUSD_Utils::TokenToStringHolder(primvar.GetPrimvarName());

        // One special case we always handle here is to change
        // the name of the USD "displayColor" primvar to "Cd",
        // as long as there is not already a "Cd" primvar.
        if (!hasCdPrimvar && 
            primvar.GetName() == UsdGeomTokens->primvarsDisplayColor) {
            name = Cd;
        }

        // For UsdGeomPointBased, 'primvars:normals' has precedence over the
        // 'normals' attribute.
        if (name == UsdGeomTokens->normals &&
            Gusd_HasSchemaAttrib(prim_defn, UsdGeomTokens->normals))
        {
            name = GA_Names::N;
        }

        // Similarly, rename st to uv if necessary.
        if (translateSTtoUV && name == stName) {
            name = GA_Names::uv;
        }

        // If the name of this primvar doesn't
        // match the primvarPattern, skip it.
        if (!name.multiMatch(primvarPattern)) {
            continue;
        }

        // Compute the value before calling convertPrimvarData, so that
        // we can distinguish between primvars with no authored value
        // and primvars whose authored value can't be converted.
        // Note that the 'authored' primvars above are only known to have
        // scene description, and still may have no value!
        VtValue val;
        if (!primvar.ComputeFlattened(&val, time)) {
            continue;
        }

        TfToken interpolation = primvar.GetInterpolation();

        // If this is a constant array and there is a ":lengths" array, convert
        // the pair back to an array attribute.
        // The lengths array has the appropriate interpolation type for the
        // array attribute.
        UsdGeomPrimvar lengths_pv;
        VtValue lengths_val;
        if (interpolation == UsdGeomTokens->constant)
        {
            TfToken lengths_pv_name(primvar.GetName().GetString() +
                                    _tokens->lengthsSuffix.GetString());
            lengths_pv = UsdGeomPrimvar(
                primvar.GetAttr().GetPrim().GetAttribute(lengths_pv_name));

            if (lengths_pv && lengths_pv.ComputeFlattened(&lengths_val, time))
                interpolation = lengths_pv.GetInterpolation();
        }

        GT_DataArrayHandle gtData;
        if (!lengths_val.IsEmpty())
        {
            GT_DataArrayHandle flat_data = convertAttributeData(primvar, val);
            GT_DataArrayHandle lengths_data =
                convertAttributeData(lengths_pv, lengths_val);

            if (flat_data && lengths_data)
                gtData = new GT_DAVaryingArray(flat_data, lengths_data);
        }
        else
            gtData = convertAttributeData(primvar, val);

        if( !gtData )
        {
            TF_WARN( "Failed to convert primvar %s:%s %s.", 
                        primPath.c_str(),
                        primvar.GetPrimvarName().GetText(),
                        primvar.GetTypeName().GetAsToken().GetText() );
            continue;
        }

        // If we're translating 'st' to 'uv', and 'st' has tuple size 2, expand
        // out to the standard tuple size of 3 for 'uv'.
        if (translateSTtoUV && name == GA_Names::uv) {
            const GT_Storage storage = gtData->getStorage();

            if (GTisFloat(storage) && gtData->getTupleSize() == 2) {
                if (storage == GT_STORE_FPREAL16)
                    gtData = Gusd_ExpandSTToUV<fpreal16>(gtData);
                else if (storage == GT_STORE_FPREAL32)
                    gtData = Gusd_ExpandSTToUV<fpreal32>(gtData);
                else if (storage == GT_STORE_FPREAL64)
                    gtData = Gusd_ExpandSTToUV<fpreal64>(gtData);
            }
        }

        // Encode the USD primvar names into something safe for the Houdini
        // geometry attribute name. This allows round tripping of namespaced
        // primvars from USD -> Houdini -> USD.
        UT_StringHolder attrname = UT_VarEncode::encodeAttrib(name);

        Gusd_AddAttribute(
                primvar, gtData, attrname, interpolation, minUniform, minPoint,
                minVertex, primPath, remapIndicies, vertex, point, primitive,
                constant, constant_attribs, scalar_attribs, bool_attribs);
    }

    // Import custom attributes.
    {
        UT_StringMMPattern attrib_pattern;
        if (rparms)
        {
            UT_String attrib_pattern_str;
            rparms->import(GUSD_REFINE_ATTRIBUTEPATTERN, attrib_pattern_str);
            attrib_pattern.compile(attrib_pattern_str);
        }

        UsdPrim usd_prim = getUsdPrim().GetPrim();
        for (const UsdAttribute &attr : usd_prim.GetAuthoredAttributes())
        {
            UT_StringHolder name =
                GusdUSD_Utils::TokenToStringHolder(attr.GetName());
            if (!name.multiMatch(attrib_pattern))
                continue;

            // Skip attributes that are primvars (or primvar indices) or the
            // subset family type (e.g. 'subsetFamily:foo:familyType'), etc
            if (TfStringStartsWith(attr.GetName(), "primvars:") ||
                TfStringStartsWith(attr.GetName(), "subsetFamily:") ||
                UsdGeomXformOp::IsXformOp(attr.GetName()))
            {
                continue;
            }

            // Skip any attributes from the prim's schema that should have
            // already been explicitly converted (e.g. 'points' -> 'P').
            if (Gusd_HasSchemaAttrib(prim_defn, attr.GetName()))
                continue;

            VtValue val;
            if (!attr.Get(&val, time))
                continue;

            GT_DataArrayHandle data = convertAttributeData(attr, val);
            if (!data)
            {
                TF_WARN("Failed to convert attribute %s:%s %s.",
                        primPath.c_str(), attr.GetName().GetText(),
                        attr.GetTypeName().GetAsToken().GetText());
                continue;
            }

            UT_StringHolder attrname = UT_VarEncode::encodeAttrib(name);

            TfToken interpolation;
            if (!attr.GetMetadata(UsdGeomTokens->interpolation, &interpolation))
            {
                // Unlike primvars, attributes aren't expected to specify an
                // interpolation, so make our best guess based on the length of
                // the data array.
                if (point && data->entries() == minPoint)
                    interpolation = UsdGeomTokens->vertex;
                else if (vertex && data->entries() == minVertex)
                    interpolation = UsdGeomTokens->faceVarying;
                else if (primitive && data->entries() == minUniform)
                    interpolation = UsdGeomTokens->uniform;
                else if (constant)
                    interpolation = UsdGeomTokens->constant;
            }

            Gusd_AddAttribute(
                    attr, data, attrname, interpolation, minUniform, minPoint,
                    minVertex, primPath, remapIndicies, vertex, point,
                    primitive, constant, constant_attribs,
                    scalar_attribs, bool_attribs);
        }
    }

    // Record usdconfigconstantattribs for constant attributes that were
    // promoted down.
    if (constant)
    {
        using namespace UT::Literal;
        Gusd_RecordAttribPattern(
                constant_attribs, *constant, "usdconfigconstantattribs"_sh);
        Gusd_RecordAttribPattern(
                scalar_attribs, *constant, "usdconfigscalarconstantattribs"_sh);
        Gusd_RecordAttribPattern(
                bool_attribs, *constant, "usdconfigboolattribs"_sh);
    }
}

/* static */
GfMatrix4d
GusdPrimWrapper::computeTransform( 
        const UsdPrim&              prim,
        UsdTimeCode                 time,
        const UT_Matrix4D&          houXform,
        const GusdSimpleXformCache& xformCache ) {

    // We need the transform into the prims space.
    // If the prim is in a hierarchy that we have written on this frame, 
    // its transform will be in the xformCache. Otherwise, we can read it 
    // from the global cache. 
    //
    // The transform cache is necessary because the gobal cache 
    // will only contain transform that we read from the stage and 
    // not anything that we have modified.

    UT_Matrix4D primXform;
    if( !prim.GetPath().IsPrimPath() ) {
        // We can get a invalid prim path if we are computing a transform relative to the parent of the root node.
        primXform.identity();
    }
    else {
        auto it = xformCache.find( prim.GetPath() );
        if( it != xformCache.end() ) {
            primXform = it->second;
        }
        else if( !GusdUSD_XformCache::GetInstance().GetLocalToWorldTransform( 
                        prim,
                        time,
                        primXform )) {
            TF_WARN( "Failed to get transform for %s.", prim.GetPath().GetText() );
            primXform.identity();
        }
    }

    return GusdUT_Gf::Cast( houXform ) / GusdUT_Gf::Cast( primXform );
}

using Gusd_SubsetFamilyMap =
    TfHashMap<TfToken, std::vector<UsdGeomSubset>, TfToken::HashFunctor>;

static void
Gusd_FindSubsets(const UsdGeomImageable &prim,
                 Gusd_SubsetFamilyMap &partition_subsets,
                 std::vector<UsdGeomSubset> &unrestricted_subsets)
{
    // First, organize the subsets by family and check whether the familyType
    // is 'partition' or 'nonOverlapping', which we can represent with an
    // attribute.
    for (const UsdGeomSubset &subset : UsdGeomSubset::GetAllGeomSubsets(prim))
    {
        TfToken elementType;
        if (!subset.GetElementTypeAttr().Get(&elementType) ||
            elementType != UsdGeomTokens->face)
        {
            // UsdGeomSubset only supports faces currently ...
            continue;
        }

        TfToken familyName;
        if (!subset.GetFamilyNameAttr().Get(&familyName) ||
            familyName.IsEmpty())
        {
            unrestricted_subsets.push_back(subset);
            continue;
        }

        TfToken familyType = UsdGeomSubset::GetFamilyType(prim, familyName);
        if (familyType == UsdGeomTokens->partition ||
            familyType == UsdGeomTokens->nonOverlapping)
        {
            partition_subsets[familyName].push_back(subset);
        }
        else
        {
            // unrestricted subsets (or any invalid type) are converted to
            // primitive groups.
            unrestricted_subsets.push_back(subset);
        }
    }
}

static GT_FaceSetMapPtr
Gusd_ConvertGeomSubsetsToGroups(
    const std::vector<UsdGeomSubset> &subsets,
    UsdTimeCode time)
{
    GT_FaceSetMapPtr facesets;

    for (const UsdGeomSubset &subset : subsets)
    {
        VtArray<int> indices;
        if (!subset.GetIndicesAttr().Get(&indices, time))
            continue;

        GT_FaceSetPtr faceset = new GT_FaceSet();
        faceset->addFaces(indices.data(), indices.size());

        if (!facesets)
            facesets = new GT_FaceSetMap();

        UT_StringHolder group_name =
            GusdUSD_Utils::TokenToStringHolder(subset.GetPrim().GetName());
        facesets->add(group_name, faceset);
    }

    return facesets;
}

/// Build a partition attribute from a family of geometry subsets.
static GT_DataArrayHandle
_buildPartitionAttribute(
        const UT_StringRef& familyName,
        const std::vector<UsdGeomSubset>& subsets,
        int numFaces,
        UsdTimeCode time)
{
    VtArray<int> indices;
    TfToken partitionValueToken("partitionValue");

    /// Houdini authors the 'partitionValue' custom data, which stores the
    /// original int / string value - we can use this for nicer round-tripping.
    VtValue firstValue =
        subsets[0].GetPrim().GetCustomDataByKey(partitionValueToken);
    if (firstValue.IsHolding<std::string>())
    {
        UT_IntrusivePtr<GT_DAIndexedString> attrib =
            new GT_DAIndexedString(numFaces);

        for (const UsdGeomSubset &subset : subsets)
        {
            VtValue partitionValue =
                subset.GetPrim().GetCustomDataByKey(partitionValueToken);
            if (!partitionValue.IsHolding<std::string>())
            {
                TF_WARN("Unexpected data type for 'partitionValue' metadata in "
                        "subset '%s', expected 'string'.",
                        subset.GetPath().GetText());
                continue;
            }

            const UT_StringHolder value(partitionValue.Get<std::string>());

            indices.clear();
            if (!subset.GetIndicesAttr().Get(&indices, time))
                continue;

            for (int i : indices)
            {
                if (i >= 0 && i < numFaces)
                    attrib->setString(i, 0, value);
            }
        }

        return attrib;
    }
    else if (firstValue.IsHolding<int64>())
    {
        UT_IntrusivePtr<GT_DANumeric<int>> attrib =
            new GT_DANumeric<int>(numFaces, 1);
        std::fill(attrib->data(), attrib->data() + numFaces, -1);

        for (const UsdGeomSubset &subset : subsets)
        {
            VtValue partitionValue =
                subset.GetPrim().GetCustomDataByKey(partitionValueToken);
            if (!partitionValue.IsHolding<int64>())
            {
                TF_WARN("Unexpected data type for 'partitionValue' metadata in "
                        "subset '%s', expected 'int64'.",
                        subset.GetPath().GetText());
                continue;
            }

            // Just write out a normal precision int attribute for now.
            const int value = partitionValue.Get<int64>();

            indices.clear();
            if (!subset.GetIndicesAttr().Get(&indices, time))
                continue;

            for (int i : indices)
            {
                if (i >= 0 && i < numFaces)
                    attrib->data()[i] = value;
            }
        }

        return attrib;
    }
    else
    {
        if (!firstValue.IsEmpty())
        {
            TF_WARN("Unexpected data type for 'partitionValue' metadata in "
                    "subset '%s'.",
                    subsets[0].GetPath().GetText());
        }

        // No custom data - just set up a string attribute based on the subset
        // names.
        UT_IntrusivePtr<GT_DAIndexedString> attrib =
            new GT_DAIndexedString(numFaces);

        UT_WorkBuffer familyPrefix;
        familyPrefix.format("{0}_", familyName);

        for (const UsdGeomSubset &subset : subsets)
        {
            UT_StringHolder value =
                GusdUSD_Utils::TokenToStringHolder(subset.GetPrim().GetName());

            // If the subset is prefixed with the family name (e.g.
            // 'name_piece0'), strip the prefix so that importing back to LOPs
            // via SOP Import produces the same subset names.
            if (value.length() > familyPrefix.length() &&
                value.startsWith(familyPrefix))
            {
                value.substitute(familyPrefix.buffer(), "", /* all */ false);
            }

            indices.clear();
            if (!subset.GetIndicesAttr().Get(&indices, time))
                continue;

            for (int i : indices)
            {
                if (i >= 0 && i < numFaces)
                    attrib->setString(i, 0, value);
            }
        }

        return attrib;
    }
}

static GT_AttributeListHandle
Gusd_ConvertGeomSubsetsToPartitionAttribs(
    const Gusd_SubsetFamilyMap &families,
    const GT_RefineParms *parms,
    GT_AttributeListHandle uniform_attribs,
    const int numFaces,
    UsdTimeCode time)
{
    UT_String attribPatternStr;
    if (parms)
        parms->import(GUSD_REFINE_PRIMVARPATTERN, attribPatternStr);

    UT_StringMMPattern attribPattern;
    if (attribPatternStr)
        attribPattern.compile(attribPatternStr);

    // Attempt to create an attribute for each family of subsets.
    for (auto &&entry : families)
    {
        UT_StringHolder familyName =
            GusdUSD_Utils::TokenToStringHolder(entry.first);
        const std::vector<UsdGeomSubset> &subsets = entry.second;

        if (!familyName.multiMatch(attribPattern))
            continue;

        GT_DataArrayHandle attrib =
            _buildPartitionAttribute(familyName, subsets, numFaces, time);
        UT_ASSERT(attrib);

        uniform_attribs =
            uniform_attribs->addAttribute(familyName, attrib, false);
    }

    return uniform_attribs;
}

/* static */
void
GusdPrimWrapper::loadSubsets(const UsdGeomImageable &prim,
                             GT_FaceSetMapPtr &facesets,
                             GT_AttributeListHandle &uniform_attribs,
                             const GT_RefineParms *parms,
                             const int numFaces,
                             UsdTimeCode time)
{
    Gusd_SubsetFamilyMap partition_subsets;
    std::vector<UsdGeomSubset> unrestricted_subsets;
    Gusd_FindSubsets(prim, partition_subsets, unrestricted_subsets);

    facesets = Gusd_ConvertGeomSubsetsToGroups(unrestricted_subsets, time);
    uniform_attribs = Gusd_ConvertGeomSubsetsToPartitionAttribs(
        partition_subsets, parms, uniform_attribs, numFaces, time);
}

PXR_NAMESPACE_CLOSE_SCOPE
