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
#ifndef HUSD_Compositor_h
#define HUSD_Compositor_h

#include <PXL/PXL_Common.h>

class HUSD_Compositor 
{
public:
			 HUSD_Compositor()
			    : myWidth(0), myHeight(0)
			 { }
    virtual		~HUSD_Compositor()
			 { }

    void		 setResolution(int w, int h)
			 { myWidth = w; myHeight = h; }
    int            width() const  { return myWidth; }
    int            height() const { return myHeight; }

    // Update the GL color buffer texture.
    virtual void	 updateColorBuffer(void *data,
				PXL_DataFormat df,
				int num_components) = 0;
    // Update the GL depth buffer texture.
    virtual void	 updateDepthBuffer(void *data,
				PXL_DataFormat df,
				int num_components) = 0;
    // Save the buffers to images on disk for debugging. Provide a default
    // empty implementation because subclasses don't need to implement this.
    virtual void	 saveBuffers(const UT_StringHolder &colorfile,
				const UT_StringHolder &depthfile) const
			 { }

protected:
    int			 myWidth;
    int			 myHeight;
};

#endif
