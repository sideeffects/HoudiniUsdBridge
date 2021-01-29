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
#include "GU_PackedUSD.h"

#include "GT_PackedUSD.h"
#include "GT_Utils.h"
#include "xformWrapper.h"
#include "meshWrapper.h"
#include "pointsWrapper.h"
#include "primWrapper.h"

#include "UT_Gf.h"
#include "GU_USD.h"
#include "stageEdit.h"

#include "USD_StdTraverse.h"
#include "GT_PrimCache.h"
#include "USD_XformCache.h"
#include "boundsCache.h"

#include "pxr/usd/usd/primRange.h"

#include "pxr/usd/usdGeom/pointBased.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/points.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdGeom/xformable.h"


#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/stringUtils.h"

#include <OP/OP_Node.h>
#include <OP/OP_Channels.h>
#include <OP/OP_DataTypes.h>
#include <OP/OP_Director.h>
#include <CH/CH_Manager.h>
#include <GA/GA_SaveMap.h>
#include <GT/GT_PrimInstance.h>
#include <GT/GT_GEODetail.h>
#include <GT/GT_GEOPrimPacked.h>
#include <GT/GT_PrimPointMesh.h>
#include <GT/GT_PrimPolygonMesh.h>
#include <GT/GT_RefineCollect.h>
#include <GT/GT_RefineParms.h>
#include <GT/GT_TransformArray.h>
#include <GT/GT_Util.h>
#include <GU/GU_MergeUtils.h>
#include <GU/GU_PackedFactory.h>
#include <GU/GU_PrimPacked.h>
#include <UT/UT_DMatrix4.h>
#include <UT/UT_Map.h>
#include <SYS/SYS_TypeTraits.h>

#include <mutex>
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;

#ifdef DEBUG
#define DBG(x) x
#else
#define DBG(x)
#endif

namespace {

class UsdPackedFactory : public GU_PackedFactory
{
public:
    UsdPackedFactory()
        : GU_PackedFactory("PackedUSD", "Packed USD")
        , theDefaultImpl(new GusdGU_PackedUSD())
    {
        registerIntrinsic("usdFileName",
            StringHolderGetterCast(&GusdGU_PackedUSD::intrinsicFileName),
            StringHolderSetterCast(&GusdGU_PackedUSD::setFileName));
        registerIntrinsic("usdAltFileName",
            StringHolderGetterCast(&GusdGU_PackedUSD::intrinsicAltFileName),
            StringHolderSetterCast(&GusdGU_PackedUSD::setAltFileName));
        registerIntrinsic("usdPrimPath",
            StringHolderGetterCast(&GusdGU_PackedUSD::intrinsicPrimPath),
            StringHolderSetterCast(&GusdGU_PackedUSD::setPrimPath));
        // The USD prim's localToWorldTransform is stored in this intrinsic.
        // This may differ from the packed prim's actual transform.
        registerTupleIntrinsic("usdLocalToWorldTransform",
            IntGetterCast(&GusdGU_PackedUSD::usdLocalToWorldTransformSize),
            F64VectorGetterCast(&GusdGU_PackedUSD::usdLocalToWorldTransform),
            NULL);
        registerIntrinsic("usdFrame",
            FloatGetterCast(&GusdGU_PackedUSD::intrinsicFrame),
            FloatSetterCast(&GusdGU_PackedUSD::setFrame));
        registerIntrinsic("usdSrcPrimPath",
            StringHolderGetterCast(&GusdGU_PackedUSD::intrinsicSrcPrimPath),
            StringHolderSetterCast(&GusdGU_PackedUSD::setSrcPrimPath));
        registerIntrinsic("usdIndex",
            IntGetterCast(&GusdGU_PackedUSD::index),
            IntSetterCast(&GusdGU_PackedUSD::setIndex));
        registerIntrinsic("usdType",
            StringHolderGetterCast(&GusdGU_PackedUSD::intrinsicType));
        registerTupleIntrinsic("usdViewportPurpose",
            IntGetterCast(&GusdGU_PackedUSD::getNumPurposes),
            StringArrayGetterCast(&GusdGU_PackedUSD::getIntrinsicPurposes),
            StringArraySetterCast(&GusdGU_PackedUSD::setIntrinsicPurposes));
    }
    ~UsdPackedFactory() override {}

    const UT_IntrusivePtr<GU_PackedImpl> &defaultImpl() const override
    {
        return theDefaultImpl;
    }

    GU_PackedImpl *create() const override
    {
        return new GusdGU_PackedUSD();
    }

    exint clearCachedGeometry() override
    {
        GusdStageCacheWriter cache;
        return cache.ClearEntriesFromDisk();
    }

    UT_IntrusivePtr<GU_PackedImpl> theDefaultImpl;
};

static UsdPackedFactory *theFactory = NULL;
const char* k_typeName = "PackedUSD";

} // close namespace 

GusdPackedUSDTracker GusdGU_PackedUSD::thePackedUSDTracker;

void
GusdGU_PackedUSD::setPackedUSDTracker(GusdPackedUSDTracker tracker)
{
    // This callback should only be set once.
    UT_ASSERT(!thePackedUSDTracker);
    thePackedUSDTracker = tracker;
}

/* static */
GU_PrimPacked* 
GusdGU_PackedUSD::Build( 
    GU_Detail&              detail, 
    const UT_StringHolder&  fileName, 
    const SdfPath&          primPath, 
    UsdTimeCode             frame, 
    const char*             lod,
    GusdPurposeSet          purposes,
    const UsdPrim&          prim,
    const UT_Matrix4D*      xform,
    PivotLocation           pivotloc )
{   
    auto packedPrim = GU_PrimPacked::build( detail, k_typeName );
    auto impl = UTverify_cast<GusdGU_PackedUSD *>(packedPrim->hardenImplementation());
    impl->m_fileName = fileName;
    impl->m_primPath = primPath;
    impl->m_frame = frame;

    if( prim && !prim.IsA<UsdGeomBoundable>() )
    {
        UsdGeomImageable geom = UsdGeomImageable(prim);
        std::vector<UsdGeomPrimvar> authoredPrimvars = geom.GetAuthoredPrimvars();
        GT_DataArrayHandle buffer;

        for( const UsdGeomPrimvar &primvar : authoredPrimvars ) {
            // XXX This is temporary code, we need to factor the usd read code into GT_Utils.cpp
            // to avoid duplicates and read for types GfHalf,double,int,string ...
            GT_DataArrayHandle gtData = GusdPrimWrapper::convertPrimvarData( primvar, frame );
	    if (!gtData)
		continue;

            const UT_String  name(primvar.GetPrimvarName());
            const GT_Storage gtStorage = gtData->getStorage();
            const GT_Size    gtTupleSize = gtData->getTupleSize();

            GA_Attribute *anAttr = detail.addTuple(GT_Util::getGAStorage(gtStorage), GA_ATTRIB_PRIMITIVE, name,
                                                   gtTupleSize);

            if( !anAttr ) {
                // addTuple could fail for various reasons, like if there's a
                // non-alphanumeric character in the primvar name.
                continue;
            }

            if( const GA_AIFTuple *aIFTuple = anAttr->getAIFTuple()) {

                const float* flatArray = gtData->getF32Array( buffer );
                aIFTuple->set( anAttr, packedPrim->getMapOffset(), flatArray, gtTupleSize );

            }  else {

                //TF_WARN( "Unsupported primvar type: %s, %s, tupleSize = %zd", 
                //         GT_String( name ), GTstorage( gtStorage ), gtTupleSize );
            }
        }
    }

    if( lod )
    {
        impl->intrinsicSetViewportLOD( packedPrim, lod );
    }
    impl->setPurposes( packedPrim, purposes );

    // It seems that Houdini may reuse memory for packed implementations with
    // out calling the constructor to initialize data. 
    impl->resetCaches();

    // If a UsdPrim was passed in, make sure it is used.
    impl->m_usdPrim = prim;

    impl->initializePivot(packedPrim, pivotloc);

    if (xform) {
        impl->setTransform(packedPrim, *xform);
    } else {
        impl->updateTransform(packedPrim);
    }

    // Register this newly built packed USD prim. The m_usdPrim is already
    // set, so we want to register right away.
    if (thePackedUSDTracker)
        thePackedUSDTracker(impl, true);

    return packedPrim;
}

/* static */
GU_PrimPacked* 
GusdGU_PackedUSD::Build( 
    GU_Detail&              detail, 
    const UT_StringHolder&  fileName, 
    const SdfPath&          primPath, 
    const SdfPath&          srcPrimPath,
    int                     index,
    UsdTimeCode             frame, 
    const char*             lod,
    GusdPurposeSet          purposes,
    const UsdPrim&          prim,
    const UT_Matrix4D*      xform,
    PivotLocation           pivotloc )
{   
    auto packedPrim = GU_PrimPacked::build( detail, k_typeName );
    auto impl = UTverify_cast<GusdGU_PackedUSD *>(packedPrim->hardenImplementation());
    impl->m_fileName = fileName;
    impl->m_primPath = primPath;
    impl->m_srcPrimPath = srcPrimPath;
    impl->m_index = index;
    impl->m_frame = frame;
    if( lod ) {
        impl->intrinsicSetViewportLOD( packedPrim, lod );
    }
    impl->setPurposes( packedPrim, purposes );

    // It seems that Houdini may reuse memory for packed implementations with
    // out calling the constructor to initialize data. 
    impl->resetCaches();

    // If a UsdPrim was passed in, make sure it is used.
    impl->m_usdPrim = prim;

    impl->initializePivot(packedPrim, pivotloc);

    if (xform) {
        impl->setTransform(packedPrim, *xform);
    } else {
        impl->updateTransform(packedPrim);
    }

    // Register this newly built packed USD prim. The m_usdPrim is already
    // set, so we want to register right away.
    if (thePackedUSDTracker)
        thePackedUSDTracker(impl, true);

    return packedPrim;
}


/* static */
GU_PrimPacked* 
GusdGU_PackedUSD::Build( 
    GU_Detail&              detail,
    const UsdPrim&          prim,
    UsdTimeCode             frame,
    const char*             lod,
    GusdPurposeSet          purposes,
    const UT_Matrix4D*      xform,
    PivotLocation           pivotloc )
{
    const std::string &filename =
        prim.GetStage()->GetRootLayer()->GetIdentifier();
    return GusdGU_PackedUSD::Build(detail, filename,
                                   prim.GetPath(), frame, lod,
                                   purposes, prim, xform, pivotloc);
}


GusdGU_PackedUSD::GusdGU_PackedUSD()
    : GU_PackedImpl()
    , m_transformCacheValid(false)
    , m_masterPathCacheValid(false)
    , m_index(-1)
    , m_frame(std::numeric_limits<float>::min())
    , m_purposes( GusdPurposeSet( GUSD_PURPOSE_DEFAULT | GUSD_PURPOSE_PROXY ))
{
}

GusdGU_PackedUSD::GusdGU_PackedUSD( const GusdGU_PackedUSD &src )
    : GU_PackedImpl( src )
    , m_fileName( src.m_fileName )
    , m_altFileName( src.m_altFileName )
    , m_primPath( src.m_primPath )
    , m_srcPrimPath( src.m_srcPrimPath )
    , m_index( src.m_index )
    , m_frame( src.m_frame )
    , m_purposes( src.m_purposes )
    , m_usdPrim( src.m_usdPrim )
    , m_transformCacheValid( src.m_transformCacheValid )
    , m_transformCache( src.m_transformCache )
    , m_masterPathCacheValid( src.m_masterPathCacheValid )
    , m_masterPathCache( src.m_masterPathCache )
    , m_gtPrimCache( NULL )
{
    // Register this new packed USD prim if m_usdPrim has already been set.
    // Otherwise we can wait until getUsdPrim is called.
    if (m_usdPrim && thePackedUSDTracker)
        thePackedUSDTracker(this, true);
}

GusdGU_PackedUSD::~GusdGU_PackedUSD()
{
    // Deregister this packed USD prim.
    if (thePackedUSDTracker)
        thePackedUSDTracker(this, false);
}

void
GusdGU_PackedUSD::install( GA_PrimitiveFactory &gafactory )
{
    if (theFactory)
        return;

    theFactory = new UsdPackedFactory();
    GU_PrimPacked::registerPacked( &gafactory, theFactory );  

    const GA_PrimitiveDefinition* def = 
        GU_PrimPacked::lookupTypeDef( k_typeName );    

    // Bind GEOPrimCollect for collecting GT prims for display in the viewport
    static GusdGT_PrimCollect *collector = new GusdGT_PrimCollect();
    collector->bind(def->getId());  
}

GA_PrimitiveTypeId 
GusdGU_PackedUSD::typeId()
{
    return GU_PrimPacked::lookupTypeId( k_typeName );
}

void
GusdGU_PackedUSD::resetCaches()
{
    clearBoxCache();
    m_usdPrim = UsdPrim();
    m_transformCacheValid = false;
    m_gtPrimCache = GT_PrimitiveHandle();
}

void
GusdGU_PackedUSD::updateTransform( GU_PrimPacked* prim )
{
    // Just mark as dirty - getLocalTransform() will provide the updated USD
    // xform.
    prim->transformDirty();
}

void
GusdGU_PackedUSD::setTransform( GU_PrimPacked* prim, const UT_Matrix4D& mx )
{
    UT_Matrix4D xform = getUsdTransform();
    xform.invert();
    xform *= mx;

    UT_Vector3 pivot;
    prim->getPivot(pivot);

    prim->setLocalTransform(UT_Matrix3D(xform));
    prim->setPos3(0, pivot * xform);
}

void
GusdGU_PackedUSD::initializePivot(GU_PrimPacked *prim, PivotLocation pivotloc)
{
    switch (pivotloc)
    {
    case PivotLocation::Origin:
    {
        // Place at the origin of the coordinate frame.
        UT_Vector3 pivot;
        getUsdTransform().getTranslates(pivot);

        prim->setPivot(pivot);
        prim->setPos3(0, pivot + prim->getPos3(0));
        break;
    }

    case PivotLocation::Centroid:
    {
        UT_BoundingBox bbox;
        if (getBounds(bbox))
        {
            // getBounds() returns the untransformed bounds, so transform the
            // center to world space.
            const UT_Vector3 pivot = bbox.center() * getUsdTransform();

            prim->setPivot(pivot);
            prim->setPos3(0, pivot + prim->getPos3(0));
        }
        break;
    }
    }
}

void
GusdGU_PackedUSD::setFileName( GU_PrimPacked* prim, const UT_StringHolder& fileName )
{
    if( fileName != m_fileName )
    {
        // Deregister this USD prim because the file name is about to change.
        // The resetCaches call clears the m_usdPrim member, and so we will be
        // re-registered when getUsdPrim is called.
        if (thePackedUSDTracker)
            thePackedUSDTracker(this, false);
        m_fileName = fileName;
        resetCaches();
        // Notify base primitive that topology has changed
        prim->topologyDirty();
        updateTransform(prim);
    }
}

void
GusdGU_PackedUSD::setAltFileName( const UT_StringHolder& fileName ) 
{
    if( fileName != m_altFileName )
    {
        m_altFileName = fileName;
    }
}

void
GusdGU_PackedUSD::setPrimPath( GU_PrimPacked* prim, const UT_StringHolder& p ) 
{
    SdfPath path;
    GusdUSD_Utils::CreateSdfPath(p, path);
    setPrimPath(prim, path);
}


void
GusdGU_PackedUSD::setPrimPath( GU_PrimPacked* prim, const SdfPath &path ) 
{
    if( path != m_primPath )
    {
        m_primPath = path;
        resetCaches();
        // Notify base primitive that topology has changed
        prim->topologyDirty();
        updateTransform(prim);
    }
}

void
GusdGU_PackedUSD::setSrcPrimPath( const UT_StringHolder& p )
{
    SdfPath path;
    GusdUSD_Utils::CreateSdfPath(p, path);
    setSrcPrimPath(path);
}

void
GusdGU_PackedUSD::setSrcPrimPath( const SdfPath &path ) 
{
    if( path != m_srcPrimPath ) {
        m_srcPrimPath = path;
    }
}

void
GusdGU_PackedUSD::setIndex( exint index ) 
{
    if( index != m_index ) {
        m_index = index;
    }
}

void
GusdGU_PackedUSD::setFrame( GU_PrimPacked* prim, UsdTimeCode frame ) 
{
    if( frame != m_frame )
    {
        m_frame = frame;
        resetCaches();
        // Notify base primitive that topology has changed
        prim->topologyDirty();
        updateTransform(prim);
    }
}

void
GusdGU_PackedUSD::setFrame( GU_PrimPacked* prim, fpreal frame )
{
    setFrame(prim, UsdTimeCode(frame));
}

exint
GusdGU_PackedUSD::getNumPurposes() const
{
    exint rv = 0;
    if( m_purposes & GUSD_PURPOSE_PROXY )
        ++rv;
    if( m_purposes & GUSD_PURPOSE_RENDER )
        ++rv;
    if( m_purposes & GUSD_PURPOSE_GUIDE )
        ++rv;
    return rv;
}

void 
GusdGU_PackedUSD::setPurposes( GU_PrimPacked* prim, GusdPurposeSet purposes )
{
    m_purposes = purposes;
    if (prim)
        prim->topologyDirty();
    resetCaches();
}

void 
GusdGU_PackedUSD::getIntrinsicPurposes( UT_StringArray& purposes ) const
{
    purposes.clear();
    if( m_purposes & GUSD_PURPOSE_PROXY )
        purposes.append( UT_StringHolder( UT_StringHolder::REFERENCE, "proxy" ));
    if( m_purposes & GUSD_PURPOSE_RENDER )
        purposes.append( UT_StringHolder( UT_StringHolder::REFERENCE, "render" ));
    if( m_purposes & GUSD_PURPOSE_GUIDE )
        purposes.append( UT_StringHolder( UT_StringHolder::REFERENCE, "guide" ));
}

void 
GusdGU_PackedUSD::setIntrinsicPurposes( GU_PrimPacked* prim, const UT_StringArray& purposes )
{
    // always includ default purpose
    setPurposes(prim,
        GusdPurposeSet(GusdPurposeSetFromArray(purposes)|
                               GUSD_PURPOSE_DEFAULT));
}

UT_StringHolder
GusdGU_PackedUSD::intrinsicType() const
{
    // Return the USD prim type so it can be displayed in the spreadsheet.
    UsdPrim prim = getUsdPrim();
    return UT_StringHolder( prim.GetTypeName().GetText() );
}

const UT_Matrix4D &
GusdGU_PackedUSD::getUsdTransform() const
{
    if( m_transformCacheValid )
        return m_transformCache;

    UsdPrim prim = getUsdPrim();

    if( !prim ) {
        TF_WARN( "Invalid prim! %s", m_primPath.GetText() );
        m_transformCache = UT_Matrix4D(1);
        return m_transformCache;
    }

    if( prim.IsA<UsdGeomXformable>() )
    {
        GusdUSD_XformCache::GetInstance().GetLocalToWorldTransform( 
             prim, m_frame, m_transformCache );
        m_transformCacheValid = true;
    }
    else
        m_transformCache = UT_Matrix4D(1);

    return m_transformCache;
}

void
GusdGU_PackedUSD::usdLocalToWorldTransform(fpreal64* val, exint size) const
{
    UT_ASSERT(size == 16);

    if( isPointInstance() )
    {
        UT_Matrix4D ident(1);
        std::copy( ident.data(), ident.data()+16, val );
    }
    else
    {
        const UT_Matrix4D &m = getUsdTransform();
        std::copy( m.data(), m.data()+16, val );
    }
}

GU_PackedFactory*
GusdGU_PackedUSD::getFactory() const
{
    return theFactory;
}

GU_PackedImpl*
GusdGU_PackedUSD::copy() const
{
    return new GusdGU_PackedUSD(*this);
}

void
GusdGU_PackedUSD::clearData()
{
}

bool
GusdGU_PackedUSD::isValid() const
{
    return bool(m_usdPrim);
}

bool
GusdGU_PackedUSD::load(GU_PrimPacked *prim, const UT_Options &options, const GA_LoadMap &map)
{
    OP_Node     *lop = nullptr;
    fpreal       t;
    bool         s;

    update( prim, options );

    // If we are loading a packed USD prim that points to a LOP node as its
    // "file", we need to set up a dependency from the source LOP node to the
    // node that has caused this USD packed prim to be loaded from disk.
    if (GusdStageCache::SplitLopStageIdentifier(m_fileName, lop, s, t))
    {
        int          tid = SYSgetSTID();
        CH_Manager  *chman = CHgetManager();
        OP_Channels *destch = (OP_Channels *)chman->getEvalCollection(tid);
        OP_Node     *destnode = destch ? destch->getNode() : nullptr;

        if (destnode)
            destnode->addExtraInput(lop, OP_INTEREST_DATA);
    }

    return true;
}

void    
GusdGU_PackedUSD::update(GU_PrimPacked *prim, const UT_Options &options)
{
    UT_StringHolder fileName, altFileName, primPath;
    if( options.importOption( "usdFileName", fileName ) || 
        options.importOption( "fileName", fileName ))
    {
        // Deregister this USD prim because the file name is about to change.
        // The resetCaches call clears the m_usdPrim member, and so we will be
        // re-registered when getUsdPrim is called.
        if (thePackedUSDTracker)
            thePackedUSDTracker(this, false);
        m_fileName = fileName;
    }

    if( options.importOption( "usdAltFileName", altFileName ) ||
        options.importOption( "altFileName", altFileName ))
    {
        setAltFileName( altFileName );
    }

    if( options.importOption( "usdPrimPath", primPath ) ||
        options.importOption( "nodePath", primPath ))
    {
        GusdUSD_Utils::CreateSdfPath(primPath, m_primPath);
    }

    if( options.importOption( "usdSrcPrimPath", primPath ))
    {
        GusdUSD_Utils::CreateSdfPath(primPath, m_srcPrimPath);
    }

    exint index;
    if( options.importOption( "usdIndex", index ))
    {
        m_index = index;
    }

    fpreal frame;
    if( options.importOption( "usdFrame", frame ) ||
        options.importOption( "frame", frame ))
    {
        m_frame = frame;
    }

    UT_StringArray purposes;
    if( options.importOption( "usdViewportPurpose", purposes ))
    {
        setIntrinsicPurposes( prim, purposes );
    }
    resetCaches();
}
    
bool
GusdGU_PackedUSD::save(UT_Options &options, const GA_SaveMap &map) const
{
    options.setOptionS( "usdFileName", m_fileName );
    options.setOptionS( "usdAltFileName", m_altFileName );
    options.setOptionS( "usdPrimPath", m_primPath.GetText() );
    options.setOptionS( "usdSrcPrimPath", m_srcPrimPath.GetText() );
    options.setOptionI( "usdIndex", m_index );
    options.setOptionF( "usdFrame", GusdUSD_Utils::GetNumericTime(m_frame) );

    UT_StringArray purposes;
    getIntrinsicPurposes( purposes );
    options.setOptionSArray( "usdViewportPurpose", purposes );
    return true;
}

bool
GusdGU_PackedUSD::getBounds(UT_BoundingBox &box) const
{
    UsdPrim prim = getUsdPrim();

    if( !prim ) {
        UT_ASSERT_MSG(0, "Invalid USD prim");
    }

    if(UsdGeomImageable visPrim = UsdGeomImageable(prim))
    {
        TfTokenVector purposes = GusdPurposeSetToTokens(m_purposes);

        if ( GusdBoundsCache::GetInstance().ComputeUntransformedBound(
                prim,
                UsdTimeCode( m_frame ),
                purposes,
                box )) {
            return true;
        }
    }
    box.makeInvalid();
    return false;
}

bool
GusdGU_PackedUSD::getRenderingBounds(UT_BoundingBox &box) const
{
    return getBoundsCached(box);
}

void
GusdGU_PackedUSD::getVelocityRange(UT_Vector3 &min, UT_Vector3 &max) const
{

}

void
GusdGU_PackedUSD::getWidthRange(fpreal &min, fpreal &max) const
{

}

bool
GusdGU_PackedUSD::getLocalTransform(UT_Matrix4D &m) const
{
    m = getUsdTransform();
    return true;
}

static constexpr UT_StringLit
    theConstantAttribsName("usdconfigconstantattribs");
static constexpr UT_StringLit
    theScalarConstantAttribsName("usdconfigscalarconstantattribs");

static void
Gusd_GetConstantAttribNames(
        GU_Detail &gdp,
        UT_StringSet &unique_names,
        const UT_StringRef &config_attrib)
{
    GA_ROHandleS constant_attribs = gdp.findStringTuple(
            GA_ATTRIB_DETAIL, config_attrib, 1);
    if (!constant_attribs.isValid())
        return;

    UT_String pattern(constant_attribs.get(GA_DETAIL_OFFSET));

    UT_StringArray attrib_names;
    pattern.tokenize(attrib_names, " ");
    unique_names.insert(attrib_names.begin(), attrib_names.end());

    // Remove the attribute - it will be created on the dest gdp after merging
    // to avoid any unwanted promotion.
    gdp.destroyAttribute(GA_ATTRIB_DETAIL, config_attrib);
}

/// Accumulate "usdconfigconstantattribs" for the details that will be merged
/// together.
static UT_StringHolder
Gusd_AccumulateConstantAttribs(GU_Detail &destgdp,
                               UT_Array<GU_DetailHandle> &details,
                               const UT_StringRef &config_attrib)
{
    UT_StringSet unique_names;

    Gusd_GetConstantAttribNames(destgdp, unique_names, config_attrib);
    for (GU_DetailHandle &gdh : details)
    {
        GU_DetailHandleAutoWriteLock gdp(gdh);
        Gusd_GetConstantAttribNames(*gdp, unique_names, config_attrib);
    }

    UT_StringHolder pattern;
    if (!unique_names.empty())
    {
        // Sort the list of names.
        UT_StringArray attrib_names;
        attrib_names.setCapacity(unique_names.size());
        for (const UT_StringHolder &name : unique_names)
            attrib_names.append(name);
        attrib_names.sort();

        UT_WorkBuffer buf;
        buf.append(attrib_names, " ");
        buf.stealIntoStringHolder(pattern);
    }

    return pattern;
}

bool
GusdGU_PackedUSD::unpackPrim(
    UT_Array<GU_DetailHandle> &details,
    const GU_Detail*        srcgdp,
    const GA_Offset         srcprimoff,
    UsdGeomImageable        prim, 
    const SdfPath&          primPath,
    const UT_Matrix4D&      xform,
    const GT_RefineParms&   rparms ) const
{
    GT_PrimitiveHandle gtPrim = 
        GusdPrimWrapper::defineForRead( 
                    prim,
                    m_frame,
                    m_purposes );

    if( !gtPrim ) {
        const TfToken &type = prim.GetPrim().GetTypeName();
	static const TfToken PxHairman("PxHairman");
        static const TfToken PxProcArgs("PxProcArgs");
        if( type != PxHairman && type != PxProcArgs ) {
            TF_WARN( "Can't convert prim for unpack. %s. Type = %s.", 
                      prim.GetPrim().GetPath().GetText(),
                      type.GetText() );
	}
        return false;
    }

    auto wrapper = UTverify_cast<const GusdPrimWrapper *>(gtPrim.get());
    return wrapper->unpack(
            details, fileName(), primPath, xform, intrinsicFrame(),
            srcgdp ? intrinsicViewportLOD(UTverify_cast<const GU_PrimPacked *>(
                    srcgdp->getPrimitive(srcprimoff))) :
                     "full",
            m_purposes, rparms);
}

bool
GusdGU_PackedUSD::unpackGeometry(
    GU_Detail &destgdp
    , const GU_Detail* srcgdp
    , const GA_Offset srcprimoff
    , const UT_StringRef &primvarPattern
    , const UT_StringRef &attributePattern
    , bool translateSTtoUV
    , const UT_StringRef& nonTransformingPrimvarPattern
    , const UT_Matrix4D* transform
    , const GT_RefineParms *refineParms
) const
{
    UT_Array<GU_DetailHandle> details;
    if (!unpackGeometry(details, srcgdp, srcprimoff, primvarPattern,
                        attributePattern, translateSTtoUV,
                        nonTransformingPrimvarPattern, *transform, refineParms))
    {
        return false;
    }

    mergeGeometry(destgdp, details);

    return true;
}

void
GusdGU_PackedUSD::mergeGeometry(GU_Detail &destgdp,
                                UT_Array<GU_DetailHandle> &details)
{
    UT_StringHolder constant_attribs_pattern = Gusd_AccumulateConstantAttribs(
            destgdp, details, theConstantAttribsName.asRef());
    UT_StringHolder scalar_attribs_pattern = Gusd_AccumulateConstantAttribs(
            destgdp, details, theScalarConstantAttribsName.asRef());

    UT_Array<GU_Detail *> gdps;
    for (GU_DetailHandle &gdh : details)
    {
        UT_ASSERT(gdh.isValid());
        gdps.append(gdh.gdpNC());
    }

    GUmatchAttributesAndMerge(destgdp, gdps);

    // Add usdconfigconstantattribs attribute to the unpacked geometry.
    if (constant_attribs_pattern.isstring())
    {
        GA_RWHandleS constant_attribs = destgdp.addStringTuple(
            GA_ATTRIB_DETAIL, theConstantAttribsName.asHolder(), 1);
        constant_attribs.set(GA_DETAIL_OFFSET, constant_attribs_pattern);
    }
    if (scalar_attribs_pattern.isstring())
    {
        GA_RWHandleS scalar_attribs = destgdp.addStringTuple(
                GA_ATTRIB_DETAIL, theScalarConstantAttribsName.asHolder(), 1);
        scalar_attribs.set(GA_DETAIL_OFFSET, scalar_attribs_pattern);
    }
}

bool
GusdGU_PackedUSD::unpackGeometry(
    UT_Array<GU_DetailHandle> &details,
    const GU_Detail *srcgdp,
    const GA_Offset srcprimoff,
    const UT_StringRef &primvarPattern,
    const UT_StringRef &attributePattern,
    bool translateSTtoUV,
    const UT_StringRef &nonTransformingPrimvarPattern,
    const UT_Matrix4D &transform,
    const GT_RefineParms *refineParms) const
{
    UsdPrim usdPrim = getUsdPrim();

    if (!usdPrim)
    {
        TF_WARN("Invalid prim found");
        return false;
    }

    GT_RefineParms rparms;
    if (refineParms)
        rparms = *refineParms;

    // Need to manually force polysoup to be turned off.
    rparms.setAllowPolySoup(false);

    rparms.set(
        GUSD_REFINE_NONTRANSFORMINGPATTERN, nonTransformingPrimvarPattern);
    rparms.set(GUSD_REFINE_TRANSLATESTTOUV, translateSTtoUV);
    if (primvarPattern)
        rparms.set(GUSD_REFINE_PRIMVARPATTERN, primvarPattern);
    if (attributePattern)
        rparms.set(GUSD_REFINE_ATTRIBUTEPATTERN, attributePattern);

    return unpackPrim(details, srcgdp, srcprimoff, UsdGeomImageable(usdPrim),
                      m_primPath, transform, rparms);
}

bool
GusdGU_PackedUSD::unpack(GU_Detail &destgdp, const UT_Matrix4D *transform) const
{
    // FIXME: The downstream code should support accepting a null transform.
    //        We shouldn't have to make a redundant identity matrix here.
    UT_Matrix4D temp;
    if( !transform ) {
        temp.identity();
    }
    // Unpack with "*" as the primvar pattern, meaning unpack all primvars.
    return unpackGeometry(destgdp, nullptr, GA_INVALID_OFFSET, "*",
                          UT_StringHolder::theEmptyString, true, GA_Names::rest,
                          transform ? transform : &temp);
}

bool
GusdGU_PackedUSD::unpackUsingPolygons(GU_Detail &destgdp, const GU_PrimPacked *prim) const
{
    UT_Matrix4D xform;
    if( prim ) {
        prim->getFullTransform4(xform);
    }
    else {
        // FIXME: The downstream code should support accepting a null transform.
        //        We shouldn't have to make a redundant identity matrix here.
        xform.identity();
    }
    // Unpack with "*" as the primvar pattern, meaning unpack all primvars.
    return unpackGeometry(
        destgdp, prim ? (const GU_Detail *)&prim->getDetail() : nullptr,
        prim ? prim->getMapOffset() : GA_INVALID_OFFSET, "*",
        UT_StringHolder::theEmptyString, true, GA_Names::rest, &xform);
}

bool
GusdGU_PackedUSD::unpackWithPrim(
    GU_Detail& destgdp,
    const UT_Matrix4D* transform,
    const GU_PrimPacked* prim) const
{
    return unpackGeometry(
        destgdp, prim ? (const GU_Detail *)&prim->getDetail() : nullptr,
        prim ? prim->getMapOffset() : GA_INVALID_OFFSET, "*",
        UT_StringHolder::theEmptyString, true, GA_Names::rest, transform);
}

bool
GusdGU_PackedUSD::getInstanceKey(UT_Options& key) const
{
    key.setOptionS("f", m_fileName);
    key.setOptionS("n", m_primPath.GetString());
    key.setOptionF("t", GusdUSD_Utils::GetNumericTime(m_frame));
    key.setOptionI("p", m_purposes );
    
    if( !m_masterPathCacheValid ) {
        UsdPrim usdPrim = getUsdPrim();

        if( !usdPrim ) {
            return true;
        }

        // Disambiguate masters of instances by including the stage pointer.
        // Sometimes instances are opened on different stages, so their
        // path will both be "/__Master_1" even if they are different prims.
        // TODO: hash by the Usd instancing key if it becomes exposed.
        std::ostringstream ost;
        ost << (void const *)get_pointer(usdPrim.GetStage());
        std::string stagePtr = ost.str();
        if( usdPrim.IsValid() && usdPrim.IsInstance() ) {
            m_masterPathCache = stagePtr +
                usdPrim.GetMaster().GetPrimPath().GetString();
        } 
        else if( usdPrim.IsValid() && usdPrim.IsInstanceProxy() ) {
            m_masterPathCache = stagePtr +
                usdPrim.GetPrimInMaster().GetPrimPath().GetString();
        } 
        else{
            m_masterPathCache = "";
        }
        m_masterPathCacheValid = true;
    }

    if( !m_masterPathCache.empty() ) {
        // If this prim is an instance, replace the prim path with the 
        // master's path so that instances can share GT prims.
        key.setOptionS("n", m_masterPathCache );
    }

    return true;
}

int64 
GusdGU_PackedUSD::getMemoryUsage(bool inclusive) const
{
    int64 mem = inclusive ? sizeof(*this) : 0;

    // Don't count the (shared) GU_Detail, since that will greatly
    // over-estimate the overall memory usage.
    // mem += _detail.getMemoryUsage(false);

    return mem;
}

void 
GusdGU_PackedUSD::countMemory(UT_MemoryCounter &counter, bool inclusive) const
{
    // TODO
}

bool
GusdGU_PackedUSD::visibleGT() const
{
    return true;
}

UsdPrim 
GusdGU_PackedUSD::getUsdPrim(UT_ErrorSeverity sev) const
{
    if(m_usdPrim)
        return m_usdPrim;

    m_masterPathCacheValid = false;

    SdfPath primPathWithoutVariants;
    GusdStageEditPtr edit;
    GusdStageEdit::GetPrimPathAndEditFromVariantsPath(
        m_primPath, primPathWithoutVariants, edit);

    GusdStageCacheReader cache;
    m_usdPrim = cache.GetPrim(m_fileName, primPathWithoutVariants, edit,
                              GusdStageOpts::LoadAll(), sev).first;

    // Register this packed USD prim now that we have set the m_usdPrim member.
    if (thePackedUSDTracker)
        thePackedUSDTracker(this, true);

    return m_usdPrim;
}


GT_PrimitiveHandle
GusdGU_PackedUSD::fullGT() const
{
    if( m_gtPrimCache )
        return m_gtPrimCache;

    if(UsdPrim usdPrim = getUsdPrim()) {
        m_gtPrimCache = GusdGT_PrimCache::GetInstance().GetPrim( 
                            m_usdPrim, 
                            m_frame,
                            m_purposes );
    }
    return m_gtPrimCache;
}

PXR_NAMESPACE_CLOSE_SCOPE
