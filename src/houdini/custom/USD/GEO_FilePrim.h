//
// Copyright 2016 Pixar
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

    // Add metadata, custom data, or attributes to a primitive.
    // The "add" methods use emplace, and so do not replace existing values.
    void			 addChild(const TfToken &child_name);
    void			 addMetadata(const TfToken &key,
					const VtValue &value);
    void			 addCustomData(const TfToken &key,
					const VtValue &value);
    GEO_FileProp		*addProperty(const TfToken &prop_name,
					const SdfValueTypeName &type_name,
					GEO_FilePropSource *prop_source);
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
