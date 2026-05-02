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
#ifndef HUSD_Compositor_h
#define HUSD_Compositor_h

#include <PXL/PXL_Common.h>

class PXL_Raster;

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
    int                  width() const
                         { return myWidth; }
    int                  height() const
                         { return myHeight; }

    // Update the GL color buffer texture.
    virtual void	 updateColorBuffer(void *data,
                                           PXL_DataFormat df,
                                           int num_components) = 0;
    virtual void	 updateColorTexture(int id)
                         { }
    // Update the GL depth buffer texture.
    virtual void	 updateDepthBuffer(void *data,
                                           PXL_DataFormat df,
                                           int num_components) = 0;
    virtual void	 updateDepthTexture(int id)
                         { }
    // Prim IDs for picking
    virtual void	 updatePrimIDBuffer(void *data,
                                            PXL_DataFormat df,
                                            bool stealdata = false,
                                            bool keeptexture = false) = 0;
    virtual void	 updatePrimIDTexture(int id)
                         { }
    virtual void	 updateInstanceIDBuffer(void *data,
                                                PXL_DataFormat df,
                                                bool stealdata = false,
                                                bool keeptexture = false) = 0;
    virtual void	 updateInstIDTexture(int id)
                         { }
    
    virtual const PXL_Raster *primID() const = 0;
    virtual const PXL_Raster *instanceID() const = 0;
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
