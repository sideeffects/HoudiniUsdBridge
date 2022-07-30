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
 *	Side Effects Software Inc.
 *	123 Front Street West, Suite 1401
 *	Toronto, Ontario
 *      Canada   M5J 2M2
 *	416-504-9876
 *
 */

#ifndef __HUSD_Utils_h__
#define __HUSD_Utils_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Path.h"
#include <UT/UT_StringHolder.h>
#include <UT/UT_IntArray.h>
#include <UT/UT_Lock.h>
#include <UT/UT_Map.h>

class HUSD_PathSet;
class HUSD_TimeCode;
class UT_String;
class PI_EditScriptedParm;
class PRM_Parm;
class OP_Node;

enum HUSD_PrimTraversalDemands {
    HUSD_TRAVERSAL_ACTIVE_PRIMS			= 0x00000001,
    HUSD_TRAVERSAL_DEFINED_PRIMS		= 0x00000002,
    HUSD_TRAVERSAL_LOADED_PRIMS			= 0x00000004,
    HUSD_TRAVERSAL_NONABSTRACT_PRIMS		= 0x00000008,
    HUSD_TRAVERSAL_ALLOW_INSTANCE_PROXIES	= 0x00000010,

    // This value is only used to create the scene graph tree through
    // HUSD_PrimHandle. It should never be used to find prims to edit.
    HUSD_TRAVERSAL_ALLOW_PROTOTYPES     	= 0x00000020,

    // This places no limitations on which prims to return, but will not
    // return instance proxies or prototype prims.
    HUSD_TRAVERSAL_NO_DEMANDS			= 0x00000000,

    // By default, place no demands on the traversal. This will even return
    // pure "over" primitives, which may have incomplete definitions.
    HUSD_TRAVERSAL_DEFAULT_DEMANDS		= HUSD_TRAVERSAL_NO_DEMANDS
};

// This enum specifies how a reference or sublayer or payload file reference
// is stored in the referring layer. The AUTO method stores paths specified
// as relative paths as relative paths, and paths specified as absolute paths
// as absolute paths. Paths specified as Search Paths (neither relative nor
// absolute) are always saved as-is.
enum HUSD_PathSaveStyle
{
    HUSD_PATH_SAVE_AUTO,
    HUSD_PATH_SAVE_RELATIVE,
    HUSD_PATH_SAVE_ABSOLUTE
};

// This is the order of the viewport overrides layers. Note that they are
// ordered strongest to weakest, so the "solo" layers override the base layer,
// and the "custom" layer overrides the "solo" layers.
enum HUSD_OverridesLayerId {
    HUSD_OVERRIDES_CUSTOM_LAYER = 0,
    HUSD_OVERRIDES_SOLO_LIGHTS_LAYER = 1,
    HUSD_OVERRIDES_SOLO_GEOMETRY_LAYER = 2,
    HUSD_OVERRIDES_BASE_LAYER = 3
};
#define HUSD_OVERRIDES_NUM_LAYERS 4

// Enum values that correspond to the SdfVariability values in the USD library.
enum HUSD_Variability {
    HUSD_VARIABILITY_VARYING,
    HUSD_VARIABILITY_UNIFORM
};

// Enum describing possible behaviors when layers are stripped off because of
// a layer break operation.
enum HUSD_StripLayerResponse {
    HUSD_IGNORE_STRIPPED_LAYERS,
    HUSD_WARN_STRIPPED_LAYERS,
    HUSD_ERROR_STRIPPED_LAYERS
};

// Enum describing the possible time sampling levels.
enum class HUSD_TimeSampling {
    NONE,	// no time samples; just the default value (not time varying)
    SINGLE,	// single time sample exists (value is not really time varying)
    MULTIPLE	// more than one time sample exists (value may be time varying)
};

// Callback function to be defined in the LOP library that returns a locked
// stage pointer for a LOP node given an "op:" prefixed path.
typedef HUSD_LockedStagePtr (*HUSD_LopStageResolver)(const UT_StringRef &path);

// A list of path strings that contain instance id numbers (possibly nested).
// Expressed with a typedef in case we decide to make this a more efficient
// data structure in the future.
typedef UT_StringArray HUSD_InstanceSelection;

// Configures the USD library for use within Houdini. The primary purpose is to
// set the prefered ArResolver to be the Houdini resolver. This should be
// called as soon as possible after loading the HUSD library.
HUSD_API void
HUSDinitialize();

// Set the callback function that is used by the HUSD library to resolve a
// LOP node path into an HUSD_LockedStagePtr. This callback is used to help
// populate the GusdStageCache for a USD packed primitive with a "file" path
// that points to a LOP node using an "op:" style path.
HUSD_API void
HUSDsetLopStageResolver(HUSD_LopStageResolver resolver);

// Calls the GusdStageCache::SplitLopStageIdentifier method, without having to
// inclde the stageCache.h header, which is not allowed in the LOP library.
HUSD_API bool
HUSDsplitLopStageIdentifier(const UT_StringRef &identifier,
        OP_Node *&lop,
        bool &split_layers,
        fpreal &t);


/// Returns true if name is a valid identifier (ie, valid component of a path).
HUSD_API bool
HUSDisValidUsdName(const UT_StringRef &name);

// Modifies the passed in string to make sure it conforms to USD primitive
// naming restrictions. Illegal characters are replaced by underscores.
HUSD_API bool
HUSDmakeValidUsdName(UT_String &name, bool addwarnings);

// Returns the name of the node passed through the HUSDmakeValidUsdName method.
// This saves several lines of code every time we use this pattern.
HUSD_API UT_StringHolder
HUSDgetValidUsdName(OP_Node &node);

// Modifies the passed in string to make sure it conforms to USD primitive
// naming restrictions. Illegal characters are replaced by underscores. Each
// path component is validated separately. The returned path will always be
// an absolute path, prefixing "/" to any passed in relative path.
HUSD_API bool
HUSDmakeValidUsdPath(UT_String &path, bool addwarnings);

// As the above function, except it has the option of allowing the passed in
// and returned path to be a relative path.
HUSD_API bool
HUSDmakeValidUsdPath(UT_String &path, bool addwarnings, bool allow_relative);

// Like the above method, but accepts "defaultPrim" as well.
HUSD_API bool
HUSDmakeValidUsdPathOrDefaultPrim(UT_String &path, bool addwarnings);

// Ensures the given primitive path is unique and does not conflict 
// with any existing primitives on the stage given by the lock.
// If suffix is given, if the given path is colliding, the new path 
// will use it along with a digit to disambiguate it.
// Returns true if given path had to be changed; false otherwise.
HUSD_API bool
HUSDmakeUniqueUsdPath(UT_String &path, const HUSD_AutoAnyLock &lock,
	const UT_StringRef &suffix = UT_StringRef());

// Returns the path of the node passed through the HUSDmakeValidUsdPath method.
// This saves several lines of code every time we use this pattern.
HUSD_API UT_StringHolder
HUSDgetValidUsdPath(OP_Node &node);

// Modifies the passed in string to make sure it conforms to USD property
// naming restrictions. This includes allowing multiple nested namespaces
// in the name. Illegal characters are replaced by underscores.
HUSD_API bool
HUSDmakeValidUsdPropertyName(UT_String &name, bool addwarnings);

// Modifies the passed in string to make sure it conforms to USD variant
// naming restrictions. Note that these are different from normal primitive
// naming conventions, as defined in SdfSchemaBase::IsValidVariantIdentifier:
// One or more letter, number, '_', '|', or '-', with an optional leading '.'
// Illegal characters are replaced by underscores.
HUSD_API bool
HUSDmakeValidVariantName(UT_String &name, bool addwarnings);

// Modifies the passed in string to make sure it conforms to USD primitive
// naming restrictions. Leading slashes are thrown away. Illegal characters
// are considered an error and cause this function to return false.
HUSD_API bool
HUSDmakeValidDefaultPrim(UT_String &default_prim, bool addwarnings);

// Returns primitive name, given the primitive path.
HUSD_API UT_StringHolder
HUSDgetUsdName(const UT_StringRef &primpath);

// Returns primitive's parent path, given the primitive path.
HUSD_API UT_StringHolder
HUSDgetUsdParentPath(const UT_StringRef &primpath);

// Modifies the provided path set so that if all the children of a prim are
// in the set, the children are removed, and the parent prim is put in the
// set instead. This procedure is applied recursively. This converts some
// parameters to USD types then calls the XUSD_Utils version of this method.
HUSD_API void
HUSDgetMinimalPathsForInheritableProperty(
        bool skip_point_instancers,
        const HUSD_AutoAnyLock &lock,
        HUSD_PathSet &paths);

// Return the primary alias for the specified USD primitive type.
HUSD_API UT_StringHolder
HUSDgetPrimTypeAlias(const UT_StringRef &primtype);

// If layers are stripped during a flatten operation, this function handles
// the error creation based on the requested response. Returns true of the
// requested response is to generate an error, which usually means we should
// also stop processing.
HUSD_API bool
HUSDapplyStripLayerResponse(HUSD_StripLayerResponse response);

/// Enum of USD transform operation types.
/// Note, they need to correspond to UsdGeomXformOp::Type enum.
enum class HUSD_XformType {	
    Invalid,
    Translate,
    Scale, 
    RotateX, RotateY, RotateZ, 
    RotateXYZ, RotateXZY, RotateYXZ, RotateYZX, RotateZXY, RotateZYX, 
    Orient,
    Transform 
};

/// Enum of rotation axis.
enum class HUSD_XformAxis	{ X, Y, Z };

/// Enum of rotation order.
enum class HUSD_XformAxisOrder	{ XYZ, XZY, YXZ, YZX, ZXY, ZYX };

/// @{ Functions for obtaining transform name, suffix, and type.
HUSD_API bool		 HUSDgetXformTypeAndSuffix(HUSD_XformType &xform_type,
				UT_StringHolder &xform_namesuffix,
				const UT_StringRef& xform_fullname);
HUSD_API HUSD_XformType  HUSDgetXformType(const UT_StringRef &xform_fullname);
HUSD_API UT_StringHolder HUSDgetXformSuffix(const UT_StringRef &xform_fullname);
HUSD_API UT_StringHolder HUSDgetXformName(HUSD_XformType xform_type, 
				const UT_StringRef &xform_namesuffix);
HUSD_API bool		 HUSDisXformAttribute(const UT_StringRef &attr,
				UT_StringHolder *xform_type = nullptr,
				UT_StringHolder *xform_name = nullptr);
/// @}

/// @{ Manipulate collection paths and components. The individual components
/// must be validated (see HUSDmakeValidName and HUSDmakeValidPath) before
/// calling these methods.
HUSD_API UT_StringHolder HUSDmakeCollectionPath( const UT_StringRef &prim_path,
				const UT_StringRef &collection_name);
HUSD_API bool		 HUSDsplitCollectionPath( UT_StringHolder &prim_path,
				UT_StringHolder &collection_name,
				const UT_StringRef &collection_path);
HUSD_API bool		 HUSDisValidCollectionPath(const UT_StringRef &path);
/// @}

/// @{ Create property paths from their components. The individual components
/// must be validated (see HUSDmakeValidName and HUSDmakeValidPath) before
/// calling these methods.
HUSD_API UT_StringHolder HUSDmakePropertyPath(const UT_StringRef &prim_path,
				const UT_StringRef &property_name);
HUSD_API UT_StringHolder HUSDmakeAttributePath(const UT_StringRef &prim_path,
				const UT_StringRef &attribute_name);
HUSD_API UT_StringHolder HUSDmakeRelationshipPath(const UT_StringRef &prim_path,
				const UT_StringRef &relationship_name);
/// @}

/// Returns the attribute name of the given primvar
HUSD_API UT_StringHolder HUSDgetPrimvarAttribName(const UT_StringRef &primvar);

/// Returns the string name of the Usd Sdf type best suited for the parameter.
HUSD_API UT_StringHolder HUSDgetAttribTypeName(const PI_EditScriptedParm &p);

/// Returns the time code at which to author an attribute value.
HUSD_API HUSD_TimeCode	 HUSDgetEffectiveTimeCode(
				const HUSD_TimeCode &timecode,
				HUSD_TimeSampling time_sampling);

/// Returns true if there are more than one time samples.
HUSD_API bool	    HUSDisTimeVarying(HUSD_TimeSampling time_sampling);

/// Returns true if there is at least one time sample.
HUSD_API bool	    HUSDisTimeSampled(HUSD_TimeSampling time_sampling);

/// Set a parameter value from the value of a USD property.
HUSD_API bool       HUSDsetParmFromProperty(HUSD_AutoAnyLock &lock,
                                const UT_StringRef &primpath,
                                const UT_StringRef &attribname,
                                const HUSD_TimeCode &tc,
                                PRM_Parm &parm,
                                HUSD_TimeSampling &timesampling);

/// Gets a list of primitives that are siblings or ancestors (or siblings
/// of ancestors) of any of the provided prims, and are also have any
/// of these prims as direct or indirect sources (via UsdConnectableAPI).
/// This method will work for materials, light filters, or any other
/// connectable prim type.
HUSD_API UT_StringArray  HUSDgetConnectedPrimsToBumpForHydra(
                                const HUSD_AutoAnyLock &anylock,
                                const UT_StringArray &modified_primpaths);
/// Bump metadata on a USD primitive to force a hydra update.
HUSD_API bool            HUSDbumpPrimsForHydra(
                                const HUSD_AutoWriteLock &writelock,
                                const UT_StringArray &bump_primpaths);

/// Return a lock object that should be obtained by any code that is going
/// to call a USD method that reloads a layer, and by any code that needs to
/// be protected against layers being reloaded on another thread. This exists
/// primarily to protect background render delegate update threads from
/// reload calls happening while reading from the viewport stage.
HUSD_API UT_Lock        &HUSDgetLayerReloadLock();

#endif
