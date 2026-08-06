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
 *      Side Effects Software Inc.
 *      123 Front Street West, Suite 1401
 *      Toronto, Ontario
 *      Canada   M5J 2M2
 *      416-504-9876
 *
 */

#include "HDUtil.h"
#include "HDTokens.h"
#include <SYS/SYS_Time.h>
#include <UT/UT_ArenaInfo.h>
#include <UT/UT_Date.h>
#include <UT/UT_Lock.h>
#include <UT/UT_StackBuffer.h>
#include <UT/UT_StringMap.h>

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/meshTopology.h>

PXR_NAMESPACE_OPEN_SCOPE        // [

namespace HDUtil {

namespace
{
    static UT_Lock      theMemLock;
    static UT_Lock      theObjLock;
    static UT_Lock      thePrimvarLock;

    static int64
    memoryUsage(const VtValue &val)
    {
        if (val.IsEmpty())
            return 0;
        if (val.IsArrayValued())
        {
            size_t asize = val.GetArraySize();
#define CHECK_ARRAY(TYPE) \
            if (val.IsHolding<VtArray<TYPE>>()) { \
                return sizeof(TYPE)*asize; \
            } \
            /* end macro */
            CHECK_ARRAY(int8);
            CHECK_ARRAY(int16);
            CHECK_ARRAY(int32);
            CHECK_ARRAY(int64);
            CHECK_ARRAY(uint8);
            CHECK_ARRAY(uint16);
            CHECK_ARRAY(uint32);
            CHECK_ARRAY(uint64);
            CHECK_ARRAY(float);
            CHECK_ARRAY(double);
            CHECK_ARRAY(GfVec2i);
            CHECK_ARRAY(GfVec2h);
            CHECK_ARRAY(GfVec2f);
            CHECK_ARRAY(GfVec2d);
            CHECK_ARRAY(GfVec3i);
            CHECK_ARRAY(GfVec3h);
            CHECK_ARRAY(GfVec3f);
            CHECK_ARRAY(GfVec3d);
            CHECK_ARRAY(GfVec4i);
            CHECK_ARRAY(GfVec4h);
            CHECK_ARRAY(GfVec4f);
            CHECK_ARRAY(GfVec4d);
            CHECK_ARRAY(TfToken);       // TODO: Should count the string mem
            CHECK_ARRAY(SdfPath);       // TODO: Should count the string mem
            CHECK_ARRAY(std::string);   // TODO: Should count the string mem
#undef CHECK_ARRAY
            UTformat(stderr, "Unhandled array type: {}\n", val.GetTypeName());
            return val.GetArraySize();
        }
        return val.GetType().GetSizeof();
    }

    struct PeakCount
    {
        PeakCount       &operator+=(int64 a)
        {
            myCurrent += a;
            if (a > 0)
            {
                myCalls++;
                myPeak = SYSmax(myPeak, myCurrent);
            }
            return *this;
        }
	PeakCount	&operator-=(int64 a) { return operator+=(-a); }
	int64		 myCurrent = 0;
	int64		 myPeak = 0;
	int64		 myCalls = 0;
    };

    static UT_Map<TfToken, PeakCount>   thePrimvarTable;
    static UT_StringMap<PeakCount>      theObjTable;
    static UT_StringMap<PeakCount>      theMemTable;

    static UT_StringHolder
    printTime(fpreal tm, const char *fmt = nullptr)
    {
        UT_WorkBuffer   buf;
        UT_Date::printSeconds(buf, tm, true, true, true);
        if (!UTisstring(fmt))
            return buf;

        UT_WorkBuffer   msg;
        msg.format(fmt, buf);
        return msg;
    }
    static UT_StringHolder
    printMem(int64 mem, const char *fmt = nullptr)
    {
        UT_WorkBuffer   buf;
        buf.printMemory(mem);
        if (!UTisstring(fmt))
            return buf;

        UT_WorkBuffer   msg;
        msg.format(fmt, buf);
        return msg;
    }

    static const char *toString(const TfToken &t) { return t.GetText(); }
    static const char *toString(const UT_StringHolder &t) { return t.c_str(); }

    template <typename TABLE_T>
    static UT_StringHolder
    saveTable(const TABLE_T &table, bool as_mem)
    {
        struct Item
        {
	    PeakCount	 mem;
	    const char	*name;
        };
        UT_Array<Item>  items(table.size());
        int64           ctotal = 0;
        int64           ptotal = 0;
        for (auto &&mem : table)
        {
            items.append({ mem.second, toString(mem.first) });
            ctotal += mem.second.myCurrent;
            ptotal += mem.second.myPeak;
        }
        items.append({ PeakCount{ctotal, ptotal, 0}, "Total" });
        items.sort([](const auto &a, const auto &b) -> bool {
            return a.mem.myPeak > b.mem.myPeak;
        });
        UT_WorkBuffer   buf;
        for (auto &&item : items)
        {
            if (as_mem)
            {
                buf.appendFormat("  {:50}[{:3}] : {} ({} peak)\n", item.name,
                        item.mem.myCalls,
                        printMem(item.mem.myCurrent),
                        printMem(item.mem.myPeak));
            }
            else
            {
                buf.appendFormat("  {:50}[{:3}] : {} ({} peak)\n", item.name,
                        item.mem.myCalls,
                        item.mem.myCurrent,
                        item.mem.myPeak);
            }
        }
        return buf;
    }

#define DUMP_TABLE(METHOD, TABLE, LOCK, AS_MEMORY) \
    UT_StringHolder METHOD() { \
        UT_Lock::Scope  lock(LOCK); \
        return saveTable(TABLE, AS_MEMORY); \
    } \
    /* end macro */

    static void
    storePrimvar(Primvars &primvars,
            const TfToken &name,
            const VtValue *values,
            const VtIntArray *indices,
            int size)
    {
        bool    has_value = false;
        bool    has_indices = false;
        for (int i = 0; i < size; ++i)
        {
            if (!values[i].IsEmpty())
                has_value = true;
            if (indices && indices[i].size())
                has_indices = true;
        }
        if (!has_value)
            return;

        Primvars::Sample        sample;
        sample.myValues = std::make_unique<VtValue[]>(size);
        std::copy(values, values+size, sample.myValues.get());
        if (has_indices)
        {
            sample.myIndirect = std::make_unique<VtIntArray[]>(size);
            std::copy(indices, indices+size, sample.myIndirect.get());
        }
        sample.mySize =size;
        primvars.myMemory += sample.computeMemory(name);
        primvars.myVars.emplace(name, std::move(sample));

        UT_Lock::Scope  lock(thePrimvarLock);
        thePrimvarTable[name] += sample.myMemory;
    }

    static void
    storePrimvar(Primvars &primvars,
                const TfToken &name,
                const VtValue &value)
    {
        storePrimvar(primvars, name, &value, nullptr, 1);
    }

    static void
    getPrimvar(Primvars &primvars,
            HdSceneDelegate &sd,
            const TfToken &name)
    {
        VtIntArray      indices;
        VtValue value = sd.GetIndexedPrimvar(primvars.myId, name, &indices);
        if (value.IsEmpty())
            value = sd.Get(primvars.myId, name);
        storePrimvar(primvars, name, &value, &indices, 1);
    }

    static void
    samplePrimvar(Primvars &primvars,
            HdSceneDelegate &sd,
            const TfToken &name,
            int size = 3)
    {
        UT_StackBuffer<float>         times(size);
        UT_StackBuffer<VtValue>       values(size);
        UT_StackBuffer<VtIntArray>    indices(size);
        int     segs = sd.SampleIndexedPrimvar(primvars.myId, name, size,
                                times.array(), values.array(), indices.array());
        if (segs > size)
        {
            samplePrimvar(primvars, sd, name, segs);
            return;
        }
        if (segs == 0)
        {
            getPrimvar(primvars, sd, name);
            return;
        }
        storePrimvar(primvars, name, values.array(), indices.array(), size);
    }

    template <typename T>
    void
    clearTable(T &table, UT_Lock &table_lock)
    {
        UT_Lock::Scope  lock(table_lock);
        table.clear();
    }

} // end namespace

void
clearTables()
{
    clearTable(theObjTable, theObjLock);
    clearTable(theMemTable, theMemLock);
    clearTable(thePrimvarTable, thePrimvarLock);
}

void
Primvars::setId(const SdfPath &id)
{
    HDEBUG_ASSERT(myMemory == 0 && myVars.size() == 0);
    myId = id;
    UT_WorkBuffer       name;
    name.format("all_primvars:{}", id);
    myName = std::move(name);
}

void
Primvars::clear()
{
    if (HDOptions::memory())
    {
        {
            UT_Lock::Scope  lock(thePrimvarLock);
            for (auto &&var : myVars)
                thePrimvarTable[var.first] -= var.second.myMemory;
        }
        {
            UT_Lock::Scope  lock(theMemLock);
            theMemTable[myName] -= myMemory;
        }
    }
    myVars.clear();
    myMemory = 0;
}

UT_StringHolder
Primvars::toString() const
{
    UT_WorkBuffer       msg;
    for (const auto &it : myVars)
    {
        msg.format("\t{:50}: {} samples -> {}\n", it.first,
                it.second.mySize, printMem(it.second.myMemory));
    }
    return msg;
}

int64
Primvars::Sample::computeMemory(const TfToken &name)
{
    myMemory = 0;
    if (myValues)
    {
        for (int i = 0; i < mySize; ++i)
        {
            if (myValues[i].IsEmpty())
                continue;

            myMemory += memoryUsage(myValues[i]);
            if (myIndirect)
                myMemory += sizeof(int) * myIndirect[i].size();
        }
    }
    return myMemory;
}

DUMP_TABLE(dumpPrimvarMemory, thePrimvarTable, thePrimvarLock, true)

void
trackObjects(const UT_StringHolder &label, int64 count)
{
    UT_Lock::Scope      lock(theObjLock);
    theObjTable[label] += count;
}

DUMP_TABLE(dumpObjects, theObjTable, theObjLock, false)

void
trackMemory(const UT_StringHolder &label, int64 count)
{
    UT_Lock::Scope      lock(theMemLock);
    theMemTable[label] += count;
}

DUMP_TABLE(dumpMemory, theMemTable, theMemLock, true)

void
load(Primvars &primvars,
        bool sample_time,
        HdSceneDelegate &sd,
        HDParam &parm,
        const SdfPath &id,
        const HdInterpolation *interp,
        int ninterp)
{
    primvars.clear();
    primvars.setId(id);
    for (int i = 0; i < ninterp; ++i)
    {
        const auto &descs = sd.GetPrimvarDescriptors(id, interp[i]);
        for (int pi = 0, npv = descs.size(); pi < npv; ++pi)
        {
            if (sample_time)
                samplePrimvar(primvars, sd, descs[pi].name);
            else
                getPrimvar(primvars, sd, descs[pi].name);
        }
    }
    if (HDOptions::memory())
        trackMemory(primvars.myName, primvars.myMemory);
    if (!HDOptions::store())
        primvars.clear();
}

void
load(Primvars &primvars,
        const SdfPath &id,
        const HdMeshTopology &top)
{
    // Meshes will likely have to keep all the topology information for rendering
    primvars.clear();
    primvars.setId(id);

    storePrimvar(primvars, HDebugTokens->topology_vertex_counts,
                    VtValue(top.GetFaceVertexCounts()));
    storePrimvar(primvars, HDebugTokens->topology_vertex_indices,
                    VtValue(top.GetFaceVertexIndices()));
    if (top.GetHoleIndices().size())
    {
        storePrimvar(primvars, HDebugTokens->topology_hole_indices,
                        VtValue(top.GetHoleIndices()));
    }
}

void
resolveMaterial(HdSceneDelegate &sd,
        HDParam &rparm,
        const SdfPath &id)
{
    SdfPath     mat = sd.GetMaterialId(id);

    if (HDOptions::material()) UTformat("Assign material {} to {}\n", mat, id);
    if (mat.IsEmpty() || rparm.hasMaterial(mat))
        return;

    UTformat(stderr, "Missing material {} for object {}\n", mat, id);
}

UT_StringHolder
systemUsage()
{
    UT_WorkBuffer	msg;
    SYS_TimeVal		usr, sys;
    SYSrusage(usr, sys);
    msg.      format("System Memory: {}\n", printMem(UT_ArenaInfo::arenaSize()));
    msg.appendFormat("Memory In Use: {}\n", printMem(UT_ArenaInfo::totalInUse()));
    msg.appendFormat("     CPU Time: {}\n", printTime(SYStime(usr)));
    msg.appendFormat("  System Time: {}\n", printTime(SYStime(sys)));
    return msg;
}

} // end namespace HDUtil


PXR_NAMESPACE_CLOSE_SCOPE       // ]
