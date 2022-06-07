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

#include "BRAY_SdrKarmaDiscovery.h"
#include <UT/UT_Debug.h>
#include <UT/UT_IStream.h>
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONWriter.h>
#include <UT/UT_Map.h>
#include <UT/UT_PathSearch.h>
#include <UT/UT_StringStream.h>
#include <UT/UT_UniquePtr.h>
#include <UT/UT_ZString.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/matrix2d.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/ndr/debugCodes.h>
#include <pxr/usd/ndr/nodeDiscoveryResult.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>
#include <pxr/usd/usdLux/tokens.h>

namespace
{
    static constexpr UT_StringLit       theName("name");
    static constexpr UT_StringLit       theType("type");
    static constexpr UT_StringLit       theDefault("default");
    static constexpr UT_StringLit       theInputs("inputs");
    static constexpr UT_StringLit       theOutputs("outputs");

    class KarmaInput
    {
    public:
        KarmaInput() = default;
        bool    load(const UT_JSONValue &v)
        {
            const UT_JSONValueMap       *map = v.getMap();
            UT_ASSERT(map);
            if (!map)
                return false;
            const UT_JSONValue  *name = map->get(theName.asRef());
            const UT_JSONValue  *type = map->get(theType.asRef());
            const UT_JSONValue  *def = map->get(theDefault.asRef());
            UT_ASSERT(name && type && def);
            if (!name || !type || !def)
                return false;
            UT_StringHolder     typestr;
            UT_VERIFY(name->import(myName));
            UT_VERIFY(type->import(typestr));
            if (!myName || !typestr)
                return false;

            return loadDefault(typestr, *def);
        }
        void    save(UT_JSONWriter &w) const
        {
            w.jsonBeginArray();
            w.jsonValue(myName);
            if (myF)
            {
                w.jsonValue("F");
                if (mySize == 1)
                    w.jsonValue(myF[0]);
                else
                    w.jsonUniformArray(mySize, myF.get());
            }
            else if (myI)
            {
                w.jsonValue("I");
                if (mySize == 1)
                    w.jsonValue(myI[0]);
                else
                    w.jsonUniformArray(mySize, myI.get());
            }
            else if (myB)
            {
                w.jsonValue("B");
                if (mySize > 1)
                    w.jsonBeginArray();
                for (int i = 0; i < mySize; ++i)
                    w.jsonValue(myB[i]);
                if (mySize > 1)
                    w.jsonEndArray();
            }
            else
            {
                UT_ASSERT(myS);
                w.jsonValue("S");
                if (mySize > 1)
                    w.jsonBeginArray();
                for (int i = 0; i < mySize; ++i)
                    w.jsonValue(myS[i]);
                if (mySize > 1)
                    w.jsonEndArray();
            }
            w.jsonEndArray();
        }

        bool    loadDefault(const UT_StringRef &type, const UT_JSONValue &def)
        {
            int                 bracket = type.findCharIndex('[');
            UT_StringView       base(type);
            mySize = 1;
            if (bracket >= 0)
            {
                base = UT_StringView(type.c_str(), type.c_str()+bracket);
                mySize = SYSatoi(type.c_str()+bracket+1);
            }
            if (base == "float")
                return loadFloat(def);
            if (base == "int")
                return loadInt(def);
            if (base == "bool")
                return loadBool(def);
            if (base == "string")
                return loadString(def);
            UTdebugFormat("Bad type: {}", base);
            UT_ASSERT(0);
            return false;
        }
        bool    loadFloat(const UT_JSONValue &def)
        {
            myF = UTmakeUnique<fpreal64[]>(mySize);
            if (mySize > 1)
                return loadArray(myF.get(), def);
            return def.import(*myF.get());
        }
        bool    loadInt(const UT_JSONValue &def)
        {
            myI = UTmakeUnique<int64[]>(mySize);
            if (mySize > 1)
                return loadArray(myI.get(), def);
            return def.import(*myI.get());
        }
        bool    loadBool(const UT_JSONValue &def)
        {
            myB = UTmakeUnique<bool[]>(mySize);
            if (mySize > 1)
                return loadArray(myB.get(), def);
            return def.import(*myB.get());
        }
        bool    loadString(const UT_JSONValue &def)
        {
            myS = UTmakeUnique<UT_StringHolder[]>(mySize);
            if (mySize > 1)
                return loadArray(myS.get(), def);
            return def.import(*myS.get());
        }

        bool    isFloat() const { return myF.get() != nullptr; }
        bool    isInt() const { return myI.get() != nullptr; }
        bool    isBool() const { return myB.get() != nullptr; }
        bool    isString() const { return myS.get() != nullptr; }

        template <typename T>
        bool    loadArray(T *result, const UT_JSONValue &val)
        {
            const UT_JSONValueArray     *arr = val.getArray();
            UT_ASSERT(arr && arr->size() == mySize);
            if (!arr)
                return false;
            if (arr->size() != mySize)
                return false;
            for (int i = 0; i < mySize; ++i)
            {
                if (!arr->get(i)->import(result[i]))
                {
                    UT_ASSERT(0);
                    return false;
                }
            }
            return true;
        }

        UT_StringHolder                 myName;
        UT_UniquePtr<fpreal64[]>        myF;
        UT_UniquePtr<int64[]>           myI;
        UT_UniquePtr<bool[]>            myB;
        UT_UniquePtr<UT_StringHolder[]> myS;
        int                             mySize;
    };
    using KarmaOutput = KarmaInput;
    struct KarmaNode
    {
        bool    load(const UT_JSONValue &val)
        {
            const UT_JSONValueMap       *map = val.getMap();
            UT_ASSERT(map);
            if (!map)
                return false;
            const UT_JSONValue  *name = map->get(theName.asRef());
            const UT_JSONValue  *inputs = map->get(theInputs.asRef());
            const UT_JSONValue  *outputs = map->get(theOutputs.asRef());
            UT_ASSERT(name && inputs && outputs);
            if (!name || !inputs || !outputs)
                return false;
            UT_VERIFY(name->import(myName));
            const UT_JSONValueArray     *iarr = inputs->getArray();
            const UT_JSONValueArray     *oarr = outputs->getArray();
            UT_ASSERT(iarr && oarr);
            if (!iarr || !oarr)
                return false;
            myInputSize = iarr->size();
            myOutputSize = oarr->size();
            myInputs = UTmakeUnique<KarmaInput[]>(myInputSize);
            myOutputs = UTmakeUnique<KarmaInput[]>(myOutputSize);
            if (!loadInputs(myInputs.get(), *iarr))
                return false;
            if (!loadInputs(myOutputs.get(), *oarr))
                return false;
            return true;
        }
        bool    loadInputs(KarmaInput *inputs, const UT_JSONValueArray &arr)
        {
            for (int i = 0, n = arr.size(); i < n; ++i)
            {
                if (!inputs[i].load(*arr.get(i)))
                {
                    UT_ASSERT(0);
                    return false;
                }
            }
            return true;
        }
        void    save(UT_JSONWriter &w) const
        {
            w.jsonBeginArray();
            w.jsonValue(myName);
            saveInputs(w, myInputSize, myInputs.get());
            saveInputs(w, myOutputSize, myOutputs.get());
            w.jsonEndArray();
        }
        void    saveInputs(UT_JSONWriter &w, int n, const KarmaInput *arr) const
        {
            w.jsonBeginArray();
            for (int i = 0; i < n; ++i)
                arr[i].save(w);
            w.jsonEndArray();
        }
        UT_StringHolder                 myName;
        UT_UniquePtr<KarmaInput[]>      myInputs;
        UT_UniquePtr<KarmaOutput[]>     myOutputs;
        int                             myInputSize;
        int                             myOutputSize;
    };

    bool
    loadKarmaNodes(UT_Array<KarmaNode> &nodes)
    {
        const char      *filename = "karmaShaderNodes.json";
        UT_WorkBuffer   path;
        if (!HoudiniFindFile(filename, path))
        {
            UTdebugFormat("Karma - Can't find {}", filename);
            return false;
        }
        UT_IFStream     is;
        if (!is.open(path))
        {
            UTdebugFormat("Error opening file: {}", filename);
            return false;
        }
        UT_AutoJSONParser       j(is);
        UT_JSONValue            contents;
        if (!contents.parseValue(*j, &is))
        {
            UTdebugFormat("Error loading JSON {} {}", filename, j->getErrors());
            return false;
        }
        const UT_JSONValueArray *arr = contents.getArray();
        UT_ASSERT(arr);
        if (!arr)
            return false;
        nodes.setSize(arr->size());
        for (int i = 0, n = arr->size(); i < n; ++i)
        {
            if (!nodes[i].load(*arr->get(i)))
                return false;
        }
        return true;
    }
}

PXR_NAMESPACE_OPEN_SCOPE

NDR_REGISTER_DISCOVERY_PLUGIN(BRAY_SdrKarmaDiscovery)

// Help for UT_Format()
#define FORMAT_TYPE(TYPE) \
    static SYS_FORCE_INLINE size_t \
    format(char *buffer, size_t bufsize, const TYPE &v) \
    { \
        UT::Format::Writer      writer(buffer, bufsize); \
        UT::Format::Formatter<> f; \
        UT_OStringStream        os; \
        os << v << std::ends; \
        return f.format(writer, "{}", {os.str()}); \
    } \
    /* end macro */
FORMAT_TYPE(TfToken)

static SYS_FORCE_INLINE size_t
format(char *buffer, size_t bufsize, const NdrTokenVec &v)
{
    UT_WorkBuffer       tmp;
    if (v.size())
    {
        tmp.format("{}", v[0]);
        for (size_t i = 1, n = v.size(); i < n; ++i)
            tmp.appendFormat(", {}", v[i]);
    }
    UT::Format::Writer      writer(buffer, bufsize);
    UT::Format::Formatter<> f;
    return f.format(writer, "[{}]", {tmp});
}

TF_DEFINE_PRIVATE_TOKENS(
    theTokens,
    ((karmaToken, "karma"))        // Built-in Karma shader node
);

BRAY_SdrKarmaDiscovery::BRAY_SdrKarmaDiscovery()
{
    TF_DEBUG(NDR_DISCOVERY).Msg("SdrKarmaDiscovery c-tor");
}

BRAY_SdrKarmaDiscovery::~BRAY_SdrKarmaDiscovery()
{
}

static void
makeShaderNode(NdrNodeDiscoveryResultVec &nodes, const KarmaNode &node)
{
    static const std::string    uri("karma");   // Token for built-in node

    std::string          name = node.myName.toStdString();
    TfToken              family;
    TfToken              discovery_type = theTokens->karmaToken;
    NdrTokenMap          metadata;

    // Encode the JSON representation for the node into some metadata
    {
        static const TfToken    theKarmaRep("karma_rep", TfToken::Immortal);
        static const TfToken    theKarmaRepOLen("karma_rep_olen",
                                        TfToken::Immortal);
        UT_WorkBuffer           noderep;
        UT_WorkBuffer           noderep_len;
        UT_AutoJSONWriter       w(noderep);
        w->setPrettyPrint(false);
        node.save(*w);
        UT_ZString              zs(noderep);
        noderep_len.sprintf("%d", int(noderep.length()));
        metadata[theKarmaRepOLen] = noderep_len.toStdString();
        metadata[theKarmaRep] = zs.compressedString().toStdString();
    }
    nodes.emplace_back(
            NdrIdentifier(name),
            NdrVersion().GetAsDefault(),
            name,
            family,
            discovery_type,         // discovery type
            theTokens->karmaToken,  // source type
            uri,                    // uri
            uri,                    // resolvedUri -Identify as a built-in node
            std::string(),          // sourceCode
            metadata,               // metadata
            std::string(),          // blindData
            TfToken()               // subIdentifier
    );
}

static void
makeShaderNode(NdrNodeDiscoveryResultVec &nodes, const UT_StringRef &name_ref)
{
    static const std::string    uri("karma");   // Token for built-in node

    std::string          name = name_ref.toStdString();
    TfToken              family; // Empty token
    TfToken              discovery_type = theTokens->karmaToken;

    nodes.emplace_back(
            NdrIdentifier(name),
            NdrVersion().GetAsDefault(),
            name,
            family,
            discovery_type,         // discovery type
            theTokens->karmaToken,  // source type
            uri,                    // uri
            uri,                    // resolvedUri -Identify as a built-in node
            std::string(),          // sourceCode
            NdrTokenMap(),          // metadata
            std::string(),          // blindData
            TfToken()               // subIdentifier
    );
}

NdrNodeDiscoveryResultVec
BRAY_SdrKarmaDiscovery::DiscoverNodes(const Context &)
{
    NdrNodeDiscoveryResultVec   result;

    // Add the built-in Karma nodes
    UT_Array<KarmaNode> karma_nodes;
    loadKarmaNodes(karma_nodes);

    for (auto &&n : karma_nodes)
        makeShaderNode(result, n);

    return result;
}

const NdrStringVec &
BRAY_SdrKarmaDiscovery::GetSearchURIs() const
{
    static NdrStringVec theURIs;
    return theURIs;
}

PXR_NAMESPACE_CLOSE_SCOPE
