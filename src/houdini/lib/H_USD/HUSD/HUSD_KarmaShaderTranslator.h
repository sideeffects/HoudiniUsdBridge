/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
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
