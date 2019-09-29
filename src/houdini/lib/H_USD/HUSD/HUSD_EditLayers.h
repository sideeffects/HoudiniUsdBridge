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

#ifndef __HUSD_EditLayers_h__
#define __HUSD_EditLayers_h__

#include "HUSD_API.h"
#include "HUSD_DataHandle.h"
#include "HUSD_LayerOffset.h"
#include <GU/GU_DetailHandle.h>
#include <UT/UT_StringHolder.h>
#include <UT/UT_StringMap.h>

class HUSD_API HUSD_EditLayers
{
public:
			 HUSD_EditLayers(HUSD_AutoWriteLock &lock);
			~HUSD_EditLayers();

    // Controls whether this object should edit the layers on the root layer
    // of the stage, or edit the sublayers on the active layer.
    void		 setEditRootLayer(bool edit_root_layer)
			 { myEditRootLayer = edit_root_layer; }
    bool		 editRootLayer() const
			 { return myEditRootLayer; }

    // Controls the position where new layers should be added. A value of
    // zero indicates the strongest layer position, and -1 indicates the
    // weakest layer position.
    void		 setAddLayerPosition(int position)
			 { myAddLayerPosition = position; }
    int			 addLayerPosition() const
			 { return myAddLayerPosition; }

    bool		 removeLayer(const UT_StringRef &filepath) const;
    bool		 addLayer(const UT_StringRef &filepath,
				const HUSD_LayerOffset &offset =
				    HUSD_LayerOffset(),
				const UT_StringMap<UT_StringHolder> &refargs =
				    UT_StringMap<UT_StringHolder>(),
				const GU_DetailHandle &gdh =
				    GU_DetailHandle()) const;
    bool		 addLayerForEdit(const UT_StringRef &filepath,
				const UT_StringMap<UT_StringHolder> &refargs =
				    UT_StringMap<UT_StringHolder>(),
				const GU_DetailHandle &gdh =
				    GU_DetailHandle()) const;
    bool		 addLayerFromSource(const UT_StringRef &usdsource,
				bool allow_editing) const;
    bool		 addLayer() const;
    bool		 applyLayerBreak() const;

private:
    HUSD_AutoWriteLock	&myWriteLock;
    int			 myAddLayerPosition;
    bool		 myEditRootLayer;
};

#endif

