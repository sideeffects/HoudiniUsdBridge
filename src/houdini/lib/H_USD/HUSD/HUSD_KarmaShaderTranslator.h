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

#ifndef __HUSD_KarmaShaderTranslator_h__
#define __HUSD_KarmaShaderTranslator_h__

#include "HUSD_API.h"
#include "HUSD_ShaderTranslator.h"


class HUSD_KarmaShaderTranslator : public HUSD_ShaderTranslator
{
public:
    virtual bool matchesRenderMask( 
	    const UT_StringRef &render_mask ) override;

    virtual void createMaterialShader( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_material_path,
	    const HUSD_TimeCode &time_code,
	    OP_Node &shader_node, VOP_Type shader_type,
	    const UT_StringRef &output_name) override;

    virtual UT_StringHolder createShader( HUSD_AutoWriteLock &lock,
	    const UT_StringRef &usd_material_path,
	    const UT_StringRef &usd_parent_path,
	    const HUSD_TimeCode &time_code,
	    OP_Node &shader_node, 
	    const UT_StringRef &output_name) override;

    virtual UT_StringHolder getRenderContextName( OP_Node &shader_node, 
			const UT_StringRef &output_name) override;
};


#endif
