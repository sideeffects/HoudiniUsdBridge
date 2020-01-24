/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdAOVBuffer.h
 *
 * COMMENTS:
 */

#ifndef __aovBuffer__
#define __aovBuffer__

#include <pxr/pxr.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <BRAY/BRAY_Interface.h>
#include <SYS/SYS_AtomicInt.h>
#include <UT/UT_UniquePtr.h>

PXR_NAMESPACE_OPEN_SCOPE

class BRAY_HdAOVBuffer : public HdRenderBuffer
{
public:
    BRAY_HdAOVBuffer(const SdfPath &id);
    virtual ~BRAY_HdAOVBuffer();

    virtual bool	Allocate(const GfVec3i &dimensions,
				HdFormat format,
				bool multiSampled) override final;

    virtual HdFormat	GetFormat() const override final;
    virtual uint	GetDepth() const override final { return 1; }
    virtual uint	GetWidth() const override final;
    virtual uint	GetHeight() const override final;
    virtual bool	IsMultiSampled() const override final
				{ return myMultiSampled; }

    virtual void	*Map() override final;
    virtual void	Unmap() override final;
    virtual bool	IsMapped() const override final;

    virtual bool	IsConverged() const override final;
    void		setConverged();
    void		clearConverged();

    virtual void	Resolve() override final;

    bool		isValid() const { return myAOVBuffer.isValid(); }
    const BRAY::AOVBufferPtr	&aovBuffer() const { return myAOVBuffer; }
    void		setAOVBuffer(const BRAY::AOVBufferPtr &aov)
    {
	myAOVBuffer = aov;
    }

private:
    virtual void	_Deallocate() override final;

    BRAY::AOVBufferPtr		myAOVBuffer;
    UT_UniquePtr<uint8_t[]>	myTempbuf;
    SYS_AtomicInt32		myConverged;
    int				myWidth, myHeight;
    HdFormat			myFormat;
    bool			myMultiSampled;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
