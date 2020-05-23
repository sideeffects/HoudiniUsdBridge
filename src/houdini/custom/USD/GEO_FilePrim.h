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
 */

#ifndef __GEO_FILE_PRIM_H__
#define __GEO_FILE_PRIM_H__
 
#include "pxr/pxr.h"
#include "GEO_FileProp.h"
#include "GEO_FileUtils.h"
#include <UT/UT_ConcurrentHashMap.h>
#include <UT/UT_UniquePtr.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/pathTable.h>
#include <pxr/usd/sdf/abstractData.h>

PXR_NAMESPACE_OPEN_SCOPE

#define GEO_FILE_PRIM_TOKENS  \
    ((familyType,       "familyType")) \
    ((partitionValue,   "partitionValue")) \
    ((primvarsNormals,  "primvars:normals")) \
    ((subsetFamily,     "subsetFamily")) \
    ((XformOpBase,      "xformOp:transform"))

#define GEO_FILE_PRIM_TYPE_TOKENS  \
    ((BasisCurves,	"BasisCurves")) \
    ((BlendShape,	"BlendShape")) \
    ((Cone,		"Cone")) \
    ((Cylinder,		"Cylinder")) \
    ((GeomSubset,	"GeomSubset")) \
    ((HoudiniFieldAsset,"HoudiniFieldAsset")) \
    ((Mesh,		"Mesh")) \
    ((NurbsCurves,	"NurbsCurves")) \
    ((OpenVDBAsset,	"OpenVDBAsset")) \
    ((PointInstancer,	"PointInstancer")) \
    ((Points,		"Points")) \
    ((Scope,		"Scope")) \
    ((SkelAnimation,	"SkelAnimation")) \
    ((Skeleton,		"Skeleton")) \
    ((SkelRoot,		"SkelRoot")) \
    ((Sphere,		"Sphere")) \
    ((Volume,		"Volume")) \
    ((Xform,		"Xform"))

TF_DECLARE_PUBLIC_TOKENS(GEO_FilePrimTokens, GEO_FILE_PRIM_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(GEO_FilePrimTypeTokens, GEO_FILE_PRIM_TYPE_TOKENS);

/// \class GEO_FilePrim
///
class GEO_FilePrim
{
public:
				 GEO_FilePrim();
				~GEO_FilePrim();

    const GEO_FileProp		*getProp(const SdfPath& id) const;
    const GEO_FilePropMap	&getProps() const
				 { return myProps; }
    GEO_FilePropMap		&getProps()
				 { return myProps; }
    const TfTokenVector		&getChildNames() const
				 { return myChildNames; }
    const TfTokenVector		&getPropNames() const
				 { return myPropNames; }
    const GEO_FileMetadata	&getMetadata() const
				 { return myMetadata; }
    const GEO_FileMetadata	&getCustomData() const
				 { return myCustomData; }

    const SdfPath		&getPath() const
				 { return myPath; }
    void			 setPath(const SdfPath &path)
				 { myPath = path; }

    const TfToken		&getTypeName() const
				 { return myTypeName; }
    void			 setTypeName(const TfToken &type_name)
				 { myTypeName = type_name; }

    bool			 getIsDefined() const
				 { return myIsDefined; }
    void			 setIsDefined(bool defined)
				 { myIsDefined = defined; }

    bool			 getInitialized() const
				 { return myInitialized; }
    void			 setInitialized()
				 { myInitialized = true; }

    /// Adds an attribute to the primitive. Replaces any existing attribute
    /// with the same name.
    GEO_FileProp		*addProperty(const TfToken &prop_name,
					const SdfValueTypeName &type_name,
					GEO_FilePropSource *prop_source);

    // Add metadata, custom data, or attributes to a primitive.
    // The "add" methods use emplace, and so do not replace existing values.
    void			 addChild(const TfToken &child_name);
    void			 addMetadata(const TfToken &key,
					const VtValue &value);
    void			 addCustomData(const TfToken &key,
					const VtValue &value);
    GEO_FileProp		*addRelationship(const TfToken &prop_name,
					const SdfPathVector &targets);
    // The "replace" methods will replace any existing value.
    void			 replaceMetadata(const TfToken &key,
					const VtValue &value);

private:
    SdfPath			 myPath;
    GEO_FilePropMap		 myProps;
    TfTokenVector		 myChildNames;
    TfTokenVector		 myPropNames;
    TfToken			 myTypeName;
    GEO_FileMetadata		 myMetadata;
    GEO_FileMetadata		 myCustomData;
    bool			 myInitialized;
    bool			 myIsDefined;
};

typedef SdfPathTable<GEO_FilePrim> GEO_FilePrimMap;

PXR_NAMESPACE_CLOSE_SCOPE

#endif // __GEO_FILE_PRIM_H__
