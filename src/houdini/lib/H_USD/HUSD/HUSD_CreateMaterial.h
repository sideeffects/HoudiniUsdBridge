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

#ifndef __HUSD_CreateMaterial_h__
#define __HUSD_CreateMaterial_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_Overrides.h"
#include "HUSD_TimeCode.h"
#include <UT/UT_IntArray.h>


class VOP_Node;
class OP_Node;
class UT_Options;

class HUSD_API HUSD_CreateMaterial
{
public:
    /// Standard c-tor.
		HUSD_CreateMaterial(HUSD_AutoWriteLock &lock,
			const HUSD_OverridesPtr &overrides = HUSD_OverridesPtr()
			);

    /// Defines a USD material primitive at a given @p usd_mat_path 
    /// based on given @p material_vop material node.
    /// @param auto_create_preview_shader If true, an attempt is made 
    ///		to ensure the created material has a preview shader
    ///		(for the universal render context). Ie, if the material node
    ///		does not contain any explicit preview shader node to translate, 
    ///		then an ad-hoc preview shader USD primitive will be generated.
    bool	createMaterial( VOP_Node &material_vop, 
			const UT_StringRef &usd_mat_path,
			bool auto_create_preview_shader ) const;

    /// Re-translates the shader parameters given the shader VOP node.
    bool	updateShaderParameters( VOP_Node &shader_vop,
                        const UT_StringArray &parameter_names,
			const UT_StringRef &usd_shader_path ) const;

    /// Creates a new USD material primitive at @p usd_mat_path, which inherits 
    /// from the material given by @p base_material_path, and sets 
    /// the parameter override values on the created material.
    /// The @material_parameters keys are used for Material input attribute 
    /// name (eg, "diffuseColor"), but they can also have a shader primitive
    /// path prefix (eg, "preview_surface/diffuseColor"), in which case
    /// the override value for "diffuseColor" will be authored on the 
    /// "preview_surface" Shader primitive inside the @p usd_mat_path Material.
    bool	createDerivedMaterial( 
			const UT_StringRef &base_material_path,
			const UT_Options &material_parameters,
			const UT_StringRef &usd_mat_path) const;

    /// Creates a new USD light filter primitive at @p usd_light_filter_path,
    /// from the filter node given by @light_filter_vop.
    bool	createLightFilter( VOP_Node &light_filter_vop, 
			const UT_StringRef &usd_light_filter_path ) const;

    /// Creates a new standard USD preview shader primitive 
    /// based on the given render-specific shader USD primitive. 
    bool	createPreviewShader(
			const UT_StringRef &main_shader_path ) const;

    /// Destroys auto-created preview shader, that was created based on
    /// the given render-specific shader.
    bool	deletePreviewShader( 
			const UT_StringRef &main_shader_path ) const;

    /// Returns ture if the given shader has a corresponding preview shader,
    /// auto-created based on its input attributes.
    bool	hasPreviewShader( const UT_StringRef &main_shader_path );

    /// Clears the auto-created metadata flag on the preview shader primitive.
    bool	clearAutoCreateFlag( const UT_StringRef &preview_shader_path );

    /// Sets the time code at which shader parameters are evaluated.
    void	setTimeCode( const HUSD_TimeCode &time_code )
			{ myTimeCode = time_code; }

    /// Sets the primitive type that should be used when creating parents
    /// that don't exist yet in the USD hierarchy.
    void	setParentPrimType( const UT_StringHolder &type )
			{ myParentType = type; }

    /// Adds the given node as a potential dependent on the shader nodes
    /// being translated.
    void	addDependent( OP_Node *node );

    /// Configures the material creator to reference any Render Vars scope
    /// created by shader translator into the global /Render/Products.
    /// This will allow Karma to readily render these AOVs/RenderVars.
    void        setShouldReferenceRenderVars( bool flag )
                        { myShouldReferenceRenderVars = flag; }

private:
    HUSD_AutoWriteLock	&myWriteLock;
    UT_StringHolder	 myParentType;	// Type of intermediate ancestors.
    HUSD_TimeCode	 myTimeCode;	// Time at which to eval shader parms.
    UT_IntArray		 myDependentIDs;// Node IDs of dependants.
    HUSD_OverridesPtr	 myOverrides;	// Viewport override layer.
    bool                 myShouldReferenceRenderVars;
};


#endif

