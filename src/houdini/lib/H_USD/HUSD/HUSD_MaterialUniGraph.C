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

#include "HUSD_MaterialUniGraph.h"
#include "HUSD_Constants.h"
#include "HUSD_PathSet.h"
#include "XUSD_Data.h"
#include "XUSD_Utils.h"
#include "XUSD_AttributeUtils.h"
#include "UsdHoudini/houdiniNodeGraphContainerAPI.h"
#include <gusd/UT_Gf.h>
#include <PI/PI_EditScriptedParms.h>
#include <PRM/PRM_Type.h>
#include <UNI/UNI_Node.h>
#include <UNI/UNI_Port.h>
#include <UNI/UNI_Wire.h>
#include <UN/UN_UniCategory.h>
#include <UN/UN_GraphConfig.h>
#include <UN/UN_NodeType.h>
#include <UNI/UNI_Manager.h>
#include <UT/UT_Debug.h>
#include <UT/UT_StdUtil.h>
#include <UT/UT_StringStream.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdUI/backdrop.h>
#include <pxr/usd/usdUI/nodeGraphNodeAPI.h>
#include <functional>

PXR_NAMESPACE_USING_DIRECTIVE

namespace ph = std::placeholders;
namespace
{
    constexpr UT_StringLit theNewPortName("Next");

    void
    husdPrintSdrNode(const SdrShaderNode *sdrnode)
    {
        // Print the sdrnode members to gauge what we can use as heuristics
        // to construct the Tab menu submenu.
        UT_WorkBuffer meta;
        for( auto &&it : sdrnode->GetMetadata())
        {
            meta.append(it.first.GetString());
            meta.append(':');
            meta.append(it.second);
            meta.append(' ');
        }
        UT_WorkBuffer deps;
        for( auto &&it : sdrnode->GetDepartments())
        {
            deps.append(it.GetString());
            deps.append(' ');
        }
#if PXR_MINOR_VERSION < 26
        std::string role = sdrnode->GetRole();
#else
        std::string role = sdrnode->GetRole().GetString();
#endif

        UTdebugPrintCd(none, "ID =",sdrnode->GetIdentifier().GetString(),
                "\n    ", "Src =", sdrnode->GetSourceType().GetString(),
                          "N =", sdrnode->GetName(),
                          "L =", sdrnode->GetLabel().GetString(),
                "\n    ", "Ctx =", sdrnode->GetContext().GetString(),
                          "F =", sdrnode->GetFamily().GetString(),
                          "Cat =", sdrnode->GetCategory().GetString(),
                          "R =", role,
                "\n    ", "I =", sdrnode->GetInfoString(),
                "\n    ", "D = ", deps,
                "\n    ", "M = ", meta,
                "\n");
    }

    UT_StringArray
    husdToolSubmenus(const SdrShaderNode *sdrnode)
    {
        UT_StringHolder srctype = sdrnode->GetSourceType().GetString();
        UT_WorkBuffer submenu_path(srctype);
        submenu_path.append('/');

        // TODO: FIXME: Remove it after the submenus stabilize
        //husdPrintSdrNode(sdrnode);

        // TODO: FIXME: some special casing for now; once Sdr standardizes
        //      a bit more, make it more generic.
        // Note, each parser plugin (srctype) has own interpretation
        // for the meaning of Family, Category, Role.
        // Eg, Role for kma is the shader ID, but for mtlx it is the nodeGroup
        // that should be used for submenu.
        constexpr UT_StringLit HUSD_KMA("kma");
        if (srctype == HUSD_KMA.asRef())
        {
            // Karma parser provides nothing useful, so
            // use a substring between the first two underscores.
            const auto &name = sdrnode->GetName();
            auto first = name.find('_');
            if (first != std::string::npos)
            {
                auto second = name.find('_', first + 1);
                if (second != std::string::npos)
                {
                    auto length = second - first - 1;
                    submenu_path.append(name.substr(first + 1, length));
                }
            }
        }
        else
        {
#if PXR_MINOR_VERSION < 26
            if(UT_StringHolder submenu = sdrnode->GetRole(); submenu)
                submenu_path.append(submenu);
#else
            if(UT_StringHolder submenu = sdrnode->GetRole().GetString(); submenu)
                submenu_path.append(submenu);
#endif
            else if (UT_StringHolder submenu = sdrnode->GetCategory().GetString(); submenu)
                submenu_path.append(submenu);
        }

        return UT_StringArray({submenu_path});
    }

    SdrShaderNodeConstPtr
    husdGetShaderNode(SdrRegistry &sdrregistry, const SdrIdentifier& nodeid)
    {
        SdrShaderNodeConstPtr sdrnode = nullptr;

        // Both Karma and Mtlx use the same shader IDs (eg, ND_add_vector3).
        // Choose MaterialX because it has richer information for submenus.
        // TODO: FIXME: revisit how we add node types to UNI category.
        //      Currently we put all types under HUSD_SHADER_GRAPH_CATEGORY
        //      but we should be putting them under the Sdr node's sourceType.
        //      There is some new development in USD related to Sdr nodes:
        //      https://academysoftwarefdn.slack.com/archives/
        //      C02HJH53RN3/p1760552290376359
        //      which provides better set of Sdr node categorization and queries
        //      so use it to determine the UNI category for the shader.
        auto sdrnodes = sdrregistry.GetShaderNodesByIdentifier(nodeid);
        UT_ASSERT(sdrnodes.size() > 0);
        if (sdrnodes.size() == 1)
        {
            sdrnode = sdrnodes.front();
        }
        else
        {
            auto mtlx_it = std::find_if(sdrnodes.begin(), sdrnodes.end(),
                    [](const SdrShaderNodeConstPtr& ptr)
                    {
                        return ptr->GetSourceType() == "mtlx";
                    });
            auto mtlx = mtlx_it != sdrnodes.end() ? *mtlx_it : nullptr;
            auto kma_it = std::find_if(sdrnodes.begin(), sdrnodes.end(),
                    [](const SdrShaderNodeConstPtr& ptr)
                    {
                        return ptr->GetSourceType() == "kma";
                    });
            auto kma = kma_it != sdrnodes.end() ? *kma_it : nullptr;
            if(sdrnodes.size() != 2 || !kma)
            {
                // TODO: FIXME: Remove the assertion and the printout
                //              after the submenus stabilize.
                // We are aware of karma reusing IDs, but not other sources.
                UTdebugPrintCd(none, "SdrNode count:", sdrnodes.size());
                for( auto &n : sdrnodes)
                    husdPrintSdrNode(n);
                UT_ASSERT(!"Unhandled Sdr node sources");
            }
            sdrnode = mtlx ? mtlx : kma ? kma : sdrnodes.front();
        }

        return sdrnode;
    }

    class husd_UniCategory : public UN_UniCategory
    {
    public:
        husd_UniCategory() : UN_UniCategory(HUSD_SHADER_GRAPH_CATEGORY)
        {}

        UT_StringHolder nodeTypeIcon(const UT_StringRef &type_name) const override
        { 
            constexpr UT_StringLit NODE_TYPE_ICON("COMMON_usd");
            return NODE_TYPE_ICON.asRef(); 
        }
    };

    void
    husdAddUniPortType(UN_PortTypeRegistry &reg, const UT_StringRef &name)
    {
        // Colors for some types.
        static const UT_ArrayStringMap<UT_StringHolder> theMap({

                { "int",	    "VopInOutIntColor" },
                { "int[]",	    "VopInOutIntArrayColor" },
                { "float",	    "VopInOutFloatColor" },
                { "float[]",	    "VopInOutFloatArrayColor" },
                { "double",	    "VopInOutFloatColor" },
                { "double[]",	    "VopInOutFloatArrayColor" },
                { "string",	    "VopInOutStringColor" },
                { "string[]",       "VopInOutStringArrayColor" },
                { "token",	    "ShopInOutShaderColor" },
                { "token[]",        "ShopInOutShaderArrayColor" },
                { "asset",	    "VopInOutStructColor" },
                { "asset[]",	    "VopInOutStructArrayColor" },

                { "float2",	    "VopInOutVector2Color" },
                { "float2[]",       "VopInOutVector2ArrayColor" },
                { "double2",	    "VopInOutVector2Color" },
                { "double2[]",      "VopInOutVector2ArrayColor" },

                { "float3",	    "VopInOutVectorColor" },
                { "float3[]",       "VopInOutVectorArrayColor" },
                { "double3",	    "VopInOutVectorColor" },
                { "double3[]",      "VopInOutVectorArrayColor" },
                { "point3",	    "VopInOutVectorColor" },
                { "point3[]",       "VopInOutVectorArrayColor" },
                { "vector3f",	    "VopInOutVectorColor" },
                { "vector3f[]",     "VopInOutVectorArrayColor" },
                { "vector3d",	    "VopInOutVectorColor" },
                { "vector3d[]",     "VopInOutVectorArrayColor" },
                { "normal3f",	    "VopInOutVectorColor" },
                { "normal3f[]",     "VopInOutVectorArrayColor" },
                { "normal3d",	    "VopInOutVectorColor" },
                { "normal3d[]",     "VopInOutVectorArrayColor" },

                { "float4",	    "VopInOutVectorColor" },
                { "float4[]",       "VopInOutVectorArrayColor" },
                { "double4",	    "VopInOutVectorColor" },
                { "double4[]",      "VopInOutVectorArrayColor" },

                { "color3f",	    "VopInOutColorColor" },
                { "color3f[]",	    "VopInOutColorColor" },
                { "color3d",	    "VopInOutColorColor" },
                { "color3d[]",	    "VopInOutColorColor" },
                { "color4f",	    "VopInOutColorColor" },
                { "color4f[]",	    "VopInOutColorColor" },
                { "color4d",	    "VopInOutColorColor" },
                { "color4d[]",	    "VopInOutColorColor" },

                { "matrix2d",       "VopInOutMatrix2Color" },
                { "matrix2d[]",     "VopInOutMatrix2ArrayColor" },
                { "matrix3d",	    "VopInOutMatrix3Color" },
                { "matrix3d[]",     "VopInOutMatrix3ArrayColor" },
                { "matrix4d",	    "VopInOutMatrix4Color" },
                { "matrix4d[]",     "VopInOutMatrix4ArrayColor" },
        });

        auto port_type = UTmakeUnique<UN_PortType>(name);
        if (auto it = theMap.find(name); it != theMap.end())
            port_type->setColorName(it->second);
        //else UTdebugPrintCd(none, "Missing color for port type:", name);
        
        reg.addType(std::move(port_type));
    }

    UNI_CategoryHandle
    husdLoadUniCategory()
    {
        auto category = UTmakeUnique<husd_UniCategory>();
        auto &port_type_reg = category->portTypeRegistry();

        // Register known attribute types (for shader inputs/outputs).
        port_type_reg.setPortTypeNameSafeChars("[]"); // arrays have brackets
        const SdfSchema& schema = SdfSchema::GetInstance();
        for (auto &&t : schema.GetAllTypes()) 
        {  
            UT_StringHolder name(t.GetAsToken().GetText());
            husdAddUniPortType(port_type_reg, name);
        }

        // Create a UNI node type of each SdrShader node type.
        SdrRegistry &sdrregistry = SdrRegistry::GetInstance();
        for (auto &&nodeid : sdrregistry.GetShaderNodeIdentifiers())
        {
            auto sdrnode = husdGetShaderNode(sdrregistry, nodeid);
            UT_StringHolder shaderid(nodeid.GetText());
            auto unnodetype = UTmakeUnique<UN_NodeType>(shaderid);
                    // TODO: FIXME: use label too
                    //sdrnode->GetLabel().GetText());
            // Create a single signature for this node type.
            UN_NodeSignatureHandle sig = UTmakeUnique<UN_NodeSignature>(shaderid);
            for (auto &&input : sdrnode->GetShaderInputNames())
                sig->addInput(input.GetText(),
                    sdrnode->GetShaderInput(input)->GetTypeAsSdfType().
                        GetSdfType().GetAsToken().GetString());
            for (auto &&output : sdrnode->GetShaderOutputNames())
                sig->addOutput(output.GetText(),
                    sdrnode->GetShaderOutput(output)->GetTypeAsSdfType().
                        GetSdfType().GetAsToken().GetString());
            unnodetype->addSignature(std::move(sig));
            unnodetype->setToolSubmenus(husdToolSubmenus(sdrnode));
            // Add the node type to the registry.
            category->nodeTypeRegistry().addType(std::move(unnodetype));
        }

        // TODO: FIXME: Fake type for testing and debuging multi-signature types
        //              Remove it once tests are complted.
        for (auto nodetypename : {"foo", "bar"})
        {
            auto foonodetype = UTmakeUnique<UN_NodeType>(nodetypename);
            UT_WorkBuffer signame;
            UN_NodeSignatureHandle sig;

            signame.format("{}(i,f3)", nodetypename);
            sig = UTmakeUnique<UN_NodeSignature>(signame, "Integer");
            sig->addInput("in1", "int");
            sig->addInput("in2", "float3");
            sig->addOutput("out1", "int");
            foonodetype->addSignature(std::move(sig));

            signame.format("{}(f,f3)", nodetypename);
            sig = UTmakeUnique<UN_NodeSignature>(signame, "Float");
            sig->addInput("in1", "float");
            sig->addInput("in2", "float3");
            sig->addOutput("out1", "float");
            foonodetype->addSignature(std::move(sig));

            signame.format("{}(f3,f)", nodetypename);
            sig = UTmakeUnique<UN_NodeSignature>(signame, "Float Reordered");
            sig->addInput("in2", "float3");
            sig->addInput("in1", "float");
            sig->addOutput("out1", "float");
            foonodetype->addSignature(std::move(sig));

            signame.format("{}(s,s)", nodetypename);
            sig = UTmakeUnique<UN_NodeSignature>(signame, "String");
            sig->addInput("in1", "string");
            sig->addInput("in2", "string");
            sig->addOutput("out1", "string");
            foonodetype->addSignature(std::move(sig));

            category->nodeTypeRegistry().addType(std::move(foonodetype));
        }

        // Add the "node graph" node type to the category.
        auto ngtype = UTmakeUnique<UN_NodeType>(HUSD_NODEGRAPH_NODE_TYPE);
        ngtype->setChildCategoryNames(
            UT_StringArray{ HUSD_SHADER_GRAPH_CATEGORY });
        category->nodeTypeRegistry().addType(std::move(ngtype));

        return category;
    }

    UN_GraphContext
    husdShaderGraphContext()
    {
        // Basic configuration for UsdShade node graphs.
        UN_GraphConfig config;
        config.setForceValidGraphItemNames(true);
        config.useRegistryNodeTypes(true);

        // Build a context that ties the SdrShader node type category to the
        // UN manager and the graph config.
        return UN_GraphContext(std::move(config),
            HUSD_SHADER_GRAPH_CATEGORY,
            &UNImanager());
    }

    bool
    husdGetPortInfo(const TfToken &prefixedportname,
            std::string *portname,
            UN_PortKind *portkind)
    {
        std::pair<std::string, bool> stripinfo;

        stripinfo = SdfPath::StripPrefixNamespace(
            prefixedportname, UsdShadeTokens->inputs);
        if (stripinfo.second)
        {
            *portname = stripinfo.first;
            *portkind = UN_PortKind::Input;
        }
        else
        {
            stripinfo = SdfPath::StripPrefixNamespace(
                prefixedportname, UsdShadeTokens->outputs);
            if (stripinfo.second)
            {
                *portname = stripinfo.first;
                *portkind = UN_PortKind::Output;
            }
            else
                *portkind = UN_PortKind::Invalid;
        }

        return stripinfo.second;
    }

    void
    husdCreateAndConnectPorts(UN_GraphData &graphdata,
        UN_NodeID srcnodeid,
        UN_PortKind srcportkind,
        const UT_StringHolder &srcportname,
        const UT_StringHolder &srcporttype,
        UN_NodeID destnodeid,
        UN_PortKind destportkind,
        const UT_StringHolder &destportname,
        const UT_StringHolder &destporttype)
    {

        // For connections to the root prim, we need to connect
        // to the _input_ on the material, creating this input if
        // it doesn't already exist.
        if (srcnodeid == graphdata.rootNode() || graphdata.isSubnet(srcnodeid))
        {
            if (!graphdata.port(srcnodeid, srcportkind, srcportname))
                graphdata.createPort(srcnodeid, srcportkind,
                    srcportname, srcporttype);
        }
        if (destnodeid == graphdata.rootNode() || graphdata.isSubnet(destnodeid))
        {
            if (!graphdata.port(destnodeid, destportkind, destportname))
                graphdata.createPort(destnodeid, destportkind,
                    destportname, destporttype);
        }

        // Create the connection in the UN_GraphData.
        graphdata.connectPorts(
            graphdata.port(srcnodeid, srcportkind, srcportname),
            graphdata.port(destnodeid, destportkind, destportname));
    }

    SdfPrimSpecHandle
    husdCreatePrim(const SdfLayerRefPtr &layer,
            const SdfPath &path,
            SdfSpecifier specifier = SdfSpecifierOver,
            const TfToken &primtype = TfToken())
    {
        // Create the primspec if it doesn't already exist.
        SdfPrimSpecHandle primspec = layer->GetPrimAtPath(path);
        if (!primspec)
        {
            primspec = SdfCreatePrimInLayer(layer, path);
            primspec->SetSpecifier(specifier);
        }
        // If the specifier is "Over" and we've been asked to make it
        // something other than "Over", then do so. But we don't want to
        // turn a "Def" into an "Over".
        if (primspec->GetSpecifier() == SdfSpecifierOver &&
            specifier != SdfSpecifierOver)
            primspec->SetSpecifier(specifier);
        // Set the prim type if requested, even if the prim already existed.
        if (!primtype.IsEmpty())
            primspec->SetTypeName(primtype);

        return primspec;
    }

    void
    husdAddApiSchema(const SdfPrimSpecHandle &primspec, const TfToken &schema)
    {
        VtValue listopval = primspec->GetInfo(UsdTokens->apiSchemas);
        SdfTokenListOp listop = listopval.Get<SdfTokenListOp>();
        auto items = listop.GetPrependedItems();
        items.insert(items.begin(), schema);
        listop.SetPrependedItems(items);
        primspec->SetInfo(UsdTokens->apiSchemas, VtValue::Take(listop));
    }

    void
    husdAddCustomData(const SdfPrimSpecHandle &primspec,
            const UT_StringHolder &key, const UT_StringHolder &value)
    {
        primspec->SetCustomData(
            key.toStdString(), VtValue(value.toStdString()));
    }

    void
    husdAddCustomData(const SdfPrimSpecHandle &primspec,
            const UT_StringHolder &key, const UT_StringArray &value)
    {
        VtArray<std::string> vtvalue;
        for (auto &&str : value)
            vtvalue.push_back(str.toStdString());
        primspec->SetCustomData(
            key.toStdString(), VtValue(vtvalue));
    }

    SdfAttributeSpecHandle
    husdCreateAttribute(const SdfPrimSpecHandle &primspec,
            const TfToken &attributename,
            const SdfValueTypeName &attributetype)
    {
        SdfAttributeSpecHandle attributespec =
            primspec->GetAttributeAtPath(
                SdfPath::ReflexiveRelativePath().AppendProperty(
                    attributename));
        if (!attributespec)
            attributespec = SdfAttributeSpec::New(primspec,
                attributename, attributetype);

        return attributespec;
    }

    UT_StringHolder
    husdCreateUniquePortName(const UNI_Node &node,
            UNI_PortKind portkind,
            const UT_StringHolder &desired_name)
    {
        UT_StringArray used_names;
        auto ports = portkind == UNI_PortKind::Input
            ? node.inputPorts()
            : node.outputPorts();
        bool found = false;
        for (auto &&port : ports)
        {
            auto port_name = node.graph()->name(port);
            if (port_name == desired_name)
                found = true;
            used_names.append(node.graph()->name(port));
        }
        if (!found)
            return desired_name;

        UT_String unique_name = desired_name.c_str();
        used_names.sort();
        while (used_names.sortedFind(unique_name) >= 0)
            unique_name.incrementNumberedName();
        return unique_name;
    }

    TfToken
    husdMakePortAttributeName(const UNI_Port &port)
    {
        if (port.portKind() == UNI_PortKind::Input)
            return TfToken(UsdShadeTokens->inputs.GetString() +
                port.name().toStdString());

        return TfToken(UsdShadeTokens->outputs.GetString() +
            port.name().toStdString());
    }

    SdfAttributeSpecHandle
    husdCreatePortAttribute(const SdfPrimSpecHandle &primspec,
            const UNI_Port &port)
    {
        TfToken tfportname(port.name().toStdString());
        TfToken tfporttype(port.typeName().toStdString());
        SdfValueTypeName attributetype =
            SdfSchema::GetInstance().FindType(tfporttype);
        if (attributetype)
        {
            TfToken tfattrname = husdMakePortAttributeName(port);
            return husdCreateAttribute(primspec, tfattrname, attributetype);
        }
        return SdfAttributeSpecHandle();
    }

    std::pair<UT_StringHolder, int>
    husdParmTypeAndSizeFromSdfType(const SdfValueTypeName &sdftype)
    {
        if (sdftype == SdfValueTypeNames->Bool)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_TOGGLE,
                PRM_TypeExtended(), PRM_MultiType(), 1), 1);
        if (sdftype == SdfValueTypeNames->Double ||
            sdftype == SdfValueTypeNames->Float ||
            sdftype == SdfValueTypeNames->Half ||
            sdftype == SdfValueTypeNames->TimeCode)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_FLT,
                PRM_TypeExtended(), PRM_MultiType(), 1), 1);
        if (sdftype == SdfValueTypeNames->Int ||
            sdftype == SdfValueTypeNames->Int64 ||
            sdftype == SdfValueTypeNames->UInt ||
            sdftype == SdfValueTypeNames->UInt64 ||
            sdftype == SdfValueTypeNames->UChar)
                return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_INT,
                PRM_TypeExtended(), PRM_MultiType(), 1), 1);

        if (sdftype == SdfValueTypeNames->Double2 ||
            sdftype == SdfValueTypeNames->Float2 ||
            sdftype == SdfValueTypeNames->Half2)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_FLT,
                PRM_TypeExtended(), PRM_MultiType(), 2), 2);
        if (sdftype == SdfValueTypeNames->TexCoord2d ||
            sdftype == SdfValueTypeNames->TexCoord2f ||
            sdftype == SdfValueTypeNames->TexCoord2h)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_UVW,
                PRM_TypeExtended(), PRM_MultiType(), 2), 2);
        if (sdftype == SdfValueTypeNames->Int2)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_INT,
                PRM_TypeExtended(), PRM_MultiType(), 2), 2);

        if (sdftype == SdfValueTypeNames->Double3 ||
            sdftype == SdfValueTypeNames->Float3 ||
            sdftype == SdfValueTypeNames->Half3)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_FLT,
                PRM_TypeExtended(), PRM_MultiType(), 3), 3);
        if (sdftype == SdfValueTypeNames->Vector3d ||
            sdftype == SdfValueTypeNames->Vector3f ||
            sdftype == SdfValueTypeNames->Vector3h ||
            sdftype == SdfValueTypeNames->Point3d ||
            sdftype == SdfValueTypeNames->Point3f ||
            sdftype == SdfValueTypeNames->Point3h ||
            sdftype == SdfValueTypeNames->Normal3d ||
            sdftype == SdfValueTypeNames->Normal3f ||
            sdftype == SdfValueTypeNames->Normal3h)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_XYZ,
                PRM_TypeExtended(), PRM_MultiType(), 3), 3);
        if (sdftype == SdfValueTypeNames->Color3d ||
            sdftype == SdfValueTypeNames->Color3f ||
            sdftype == SdfValueTypeNames->Color3h)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_RGB,
                PRM_TypeExtended(), PRM_MultiType(), 3), 3);
        if (sdftype == SdfValueTypeNames->Int3)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_INT,
                PRM_TypeExtended(), PRM_MultiType(), 3), 3);

        if (sdftype == SdfValueTypeNames->Double4 ||
            sdftype == SdfValueTypeNames->Float4 ||
            sdftype == SdfValueTypeNames->Half4 ||
            sdftype == SdfValueTypeNames->Matrix2d)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_FLT,
                PRM_TypeExtended(), PRM_MultiType(), 4), 4);
        if (sdftype == SdfValueTypeNames->Quatd ||
            sdftype == SdfValueTypeNames->Quatf ||
            sdftype == SdfValueTypeNames->Quath)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_XYZ,
                PRM_TypeExtended(), PRM_MultiType(), 4), 4);
        if (sdftype == SdfValueTypeNames->Color4d ||
            sdftype == SdfValueTypeNames->Color4f ||
            sdftype == SdfValueTypeNames->Color4h)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_RGBA,
                PRM_TypeExtended(), PRM_MultiType(), 4), 4);
        if (sdftype == SdfValueTypeNames->Int4)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_INT,
                PRM_TypeExtended(), PRM_MultiType(), 4), 4);

        if (sdftype == SdfValueTypeNames->Matrix3d)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_FLT,
                PRM_TypeExtended(), PRM_MultiType(), 9), 9);

        if (sdftype == SdfValueTypeNames->Matrix4d)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_FLT,
                PRM_TypeExtended(), PRM_MultiType(), 16), 16);

        if (sdftype == SdfValueTypeNames->Asset)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_FILE,
                PRM_TypeExtended(), PRM_MultiType(), 1), 1);
        if (sdftype == SdfValueTypeNames->String ||
            sdftype == SdfValueTypeNames->Token ||
            sdftype == SdfValueTypeNames->PathExpression)
            return std::make_pair(PI_EditScriptedParm::getScriptType(PRM_STRING,
                PRM_TypeExtended(), PRM_MultiType(), 1), 1);

        return std::make_pair(UT_StringHolder(), 0);
    }

    UNI_ParmValue
    husdConvertVtValueToParmValue(const VtValue &vtvalue)
    {
        SdfValueTypeName sdftype = SdfGetValueTypeNameForValue(vtvalue);
        if (sdftype == SdfValueTypeNames->Bool)
        {
            bool boolvalue;
            if (HUSDgetValue(vtvalue, boolvalue))
                return boolvalue;
        }
        if (sdftype == SdfValueTypeNames->Int ||
            sdftype == SdfValueTypeNames->Int64 ||
            sdftype == SdfValueTypeNames->UInt ||
            sdftype == SdfValueTypeNames->UInt64 ||
            sdftype == SdfValueTypeNames->UChar)
        {
            int64 intvalue;
            if (HUSDgetValue(vtvalue, intvalue))
                return intvalue;
        }
        if (sdftype == SdfValueTypeNames->Double ||
            sdftype == SdfValueTypeNames->Float ||
            sdftype == SdfValueTypeNames->Half ||
            sdftype == SdfValueTypeNames->TimeCode)
        {
            fpreal fprealvalue;
            if (HUSDgetValue(vtvalue, fprealvalue))
                return fprealvalue;
        }
        if (sdftype == SdfValueTypeNames->Double2 ||
            sdftype == SdfValueTypeNames->Float2 ||
            sdftype == SdfValueTypeNames->Half2 ||
            sdftype == SdfValueTypeNames->TexCoord2d ||
            sdftype == SdfValueTypeNames->TexCoord2f ||
            sdftype == SdfValueTypeNames->TexCoord2h ||
            sdftype == SdfValueTypeNames->Int2)
        {
            UT_Vector2D v2value;
            if (HUSDgetValue(vtvalue, v2value))
                return v2value;
        }
        if (sdftype == SdfValueTypeNames->Double3 ||
            sdftype == SdfValueTypeNames->Float3 ||
            sdftype == SdfValueTypeNames->Half3 ||
            sdftype == SdfValueTypeNames->Vector3d ||
            sdftype == SdfValueTypeNames->Vector3f ||
            sdftype == SdfValueTypeNames->Vector3h ||
            sdftype == SdfValueTypeNames->Point3d ||
            sdftype == SdfValueTypeNames->Point3f ||
            sdftype == SdfValueTypeNames->Point3h ||
            sdftype == SdfValueTypeNames->Normal3d ||
            sdftype == SdfValueTypeNames->Normal3f ||
            sdftype == SdfValueTypeNames->Normal3h ||
            sdftype == SdfValueTypeNames->Color3d ||
            sdftype == SdfValueTypeNames->Color3f ||
            sdftype == SdfValueTypeNames->Color3h ||
            sdftype == SdfValueTypeNames->TexCoord3d ||
            sdftype == SdfValueTypeNames->TexCoord3f ||
            sdftype == SdfValueTypeNames->TexCoord3h ||
            sdftype == SdfValueTypeNames->Int3)
        {
            UT_Vector3D v3value;
            if (HUSDgetValue(vtvalue, v3value))
                return v3value;
        }
        if (sdftype == SdfValueTypeNames->Double4 ||
            sdftype == SdfValueTypeNames->Float4 ||
            sdftype == SdfValueTypeNames->Half4 ||
            sdftype == SdfValueTypeNames->Matrix2d ||
            sdftype == SdfValueTypeNames->Quatd ||
            sdftype == SdfValueTypeNames->Quatf ||
            sdftype == SdfValueTypeNames->Quath ||
            sdftype == SdfValueTypeNames->Color4d ||
            sdftype == SdfValueTypeNames->Color4f ||
            sdftype == SdfValueTypeNames->Color4h ||
            sdftype == SdfValueTypeNames->Int4)
        {
            UT_Vector4D v4value;
            if (HUSDgetValue(vtvalue, v4value))
                return v4value;
        }
        if (sdftype == SdfValueTypeNames->Matrix3d)
        {
            UT_Matrix3D m3value;
            if (HUSDgetValue(vtvalue, m3value))
                return m3value;
        }
        if (sdftype == SdfValueTypeNames->Matrix4d)
        {
            UT_Matrix4D m4value;
            if (HUSDgetValue(vtvalue, m4value))
                return m4value;
        }
        if (sdftype == SdfValueTypeNames->Asset ||
            sdftype == SdfValueTypeNames->String ||
            sdftype == SdfValueTypeNames->Token ||
            sdftype == SdfValueTypeNames->PathExpression)
        {
            UT_StringHolder strvalue;
            if (HUSDgetValue(vtvalue, strvalue))
                return strvalue;
        }

        return UNI_ParmValue();
    }

    VtValue
    husdConvertParmValueToVtValue(const UNI_ParmValue &value,
            const SdfValueTypeName &sdftype)
    {
        VtValue vtvalue;

        if (std::holds_alternative<bool>(value))
        {
            vtvalue = HUSDgetVtValue(std::get<bool>(value));
        }
        else if (std::holds_alternative<int64>(value))
        {
            int64 i = std::get<int64>(value);
            if (sdftype == SdfValueTypeNames->Int)
                vtvalue = VtValue((int32)i);
            if (sdftype == SdfValueTypeNames->Int64)
                vtvalue = VtValue((int64)i);
            if (sdftype == SdfValueTypeNames->UInt)
                vtvalue = VtValue((uint32)i);
            if (sdftype == SdfValueTypeNames->UInt64)
                vtvalue = VtValue((uint64)i);
            if (sdftype == SdfValueTypeNames->UChar)
                vtvalue = VtValue((uchar)i);
        }
        else if (std::holds_alternative<fpreal64>(value))
        {
            fpreal64 f = std::get<fpreal64>(value);
            if (sdftype == SdfValueTypeNames->Double)
                vtvalue = VtValue(f);
            if (sdftype == SdfValueTypeNames->Float)
                vtvalue = VtValue((fpreal32)f);
            if (sdftype == SdfValueTypeNames->Half)
                vtvalue = VtValue((GfHalf)f);
            if (sdftype == SdfValueTypeNames->TimeCode)
                vtvalue = VtValue(UsdTimeCode(f));
        }
        else if (std::holds_alternative<UT_Vector2D>(value))
        {
            UT_Vector2D v2 = std::get<UT_Vector2D>(value);
            if (sdftype == SdfValueTypeNames->Double2 ||
                sdftype == SdfValueTypeNames->TexCoord2d)
                vtvalue = VtValue(GfVec2d(v2[0], v2[1]));
            if (sdftype == SdfValueTypeNames->Float2 ||
                sdftype == SdfValueTypeNames->TexCoord2f)
                vtvalue = VtValue(GfVec2f(v2[0], v2[1]));
            if (sdftype == SdfValueTypeNames->Half2 ||
                sdftype == SdfValueTypeNames->TexCoord2h)
                vtvalue = VtValue(GfVec2h(v2[0], v2[1]));
            if (sdftype == SdfValueTypeNames->Int2)
                vtvalue = VtValue(GfVec2i(v2[0], v2[1]));
        }
        else if (std::holds_alternative<UT_Vector3D>(value))
        {
            UT_Vector3D v3 = std::get<UT_Vector3D>(value);
            if (sdftype == SdfValueTypeNames->Double3 ||
                sdftype == SdfValueTypeNames->Vector3d ||
                sdftype == SdfValueTypeNames->Point3d ||
                sdftype == SdfValueTypeNames->Normal3d ||
                sdftype == SdfValueTypeNames->Color3d)
                vtvalue = VtValue(GfVec3d(v3[0], v3[1], v3[2]));
            if (sdftype == SdfValueTypeNames->Float3 ||
                sdftype == SdfValueTypeNames->Vector3f ||
                sdftype == SdfValueTypeNames->Point3f ||
                sdftype == SdfValueTypeNames->Normal3f ||
                sdftype == SdfValueTypeNames->Color3f)
                vtvalue = VtValue(GfVec3f(v3[0], v3[1], v3[2]));
            if (sdftype == SdfValueTypeNames->Half3 ||
                sdftype == SdfValueTypeNames->Vector3h ||
                sdftype == SdfValueTypeNames->Point3h ||
                sdftype == SdfValueTypeNames->Normal3h ||
                sdftype == SdfValueTypeNames->Color3h)
                vtvalue = VtValue(GfVec3h(v3[0], v3[1], v3[2]));
            if (sdftype == SdfValueTypeNames->Int3)
                vtvalue = VtValue(GfVec3i(v3[0], v3[1], v3[2]));
        }
        else if (std::holds_alternative<UT_Vector4D>(value))
        {
            UT_Vector4D v4 = std::get<UT_Vector4D>(value);
            if (sdftype == SdfValueTypeNames->Double4 ||
                sdftype == SdfValueTypeNames->Color4d)
                vtvalue = VtValue(GfVec4d(v4[0], v4[1], v4[2], v4[3]));
            if (sdftype == SdfValueTypeNames->Float4 ||
                sdftype == SdfValueTypeNames->Color4f)
                vtvalue = VtValue(GfVec4f(v4[0], v4[1], v4[2], v4[3]));
            if (sdftype == SdfValueTypeNames->Half4 ||
                sdftype == SdfValueTypeNames->Color4h)
                vtvalue = VtValue(GfVec4h(v4[0], v4[1], v4[2], v4[3]));
            if (sdftype == SdfValueTypeNames->Int4)
                vtvalue = VtValue(GfVec4i(v4[0], v4[1], v4[2], v4[3]));
            if (sdftype == SdfValueTypeNames->Matrix2d)
                vtvalue = VtValue(GfMatrix2d(v4[0], v4[1], v4[2], v4[3]));
            if (sdftype == SdfValueTypeNames->Quatd)
                vtvalue = VtValue(GfQuatd(v4[0], v4[1], v4[2], v4[3]));
            if (sdftype == SdfValueTypeNames->Quatf)
                vtvalue = VtValue(GfQuatf(v4[0], v4[1], v4[2], v4[3]));
            if (sdftype == SdfValueTypeNames->Quath)
                vtvalue = VtValue(GfQuath(v4[0], v4[1], v4[2], v4[3]));
        }
        else if (std::holds_alternative<UT_Matrix3D>(value))
        {
            UT_Matrix3D m3 = std::get<UT_Matrix3D>(value);
            if (sdftype == SdfValueTypeNames->Matrix3d)
                vtvalue = VtValue(GfMatrix3d(
                    m3[0][0], m3[0][1], m3[0][2],
                    m3[1][0], m3[1][1], m3[1][2],
                    m3[2][0], m3[2][1], m3[2][2]));
        }
        else if (std::holds_alternative<UT_Matrix4D>(value))
        {
            UT_Matrix4D m4 = std::get<UT_Matrix4D>(value);
            if (sdftype == SdfValueTypeNames->Matrix4d)
                vtvalue = VtValue(GfMatrix4d(
                    m4[0][0], m4[0][1], m4[0][2], m4[0][3],
                    m4[1][0], m4[1][1], m4[1][2], m4[1][3],
                    m4[2][0], m4[2][1], m4[2][2], m4[2][3],
                    m4[3][0], m4[3][1], m4[3][2], m4[3][3]));
        }
        else if (std::holds_alternative<UT_StringHolder>(value))
        {
            std::string str = std::get<UT_StringHolder>(value).toStdString();
            if (sdftype == SdfValueTypeNames->Asset)
                vtvalue = VtValue(SdfAssetPath(str));
            if (sdftype == SdfValueTypeNames->String)
                vtvalue = VtValue(str);
            if (sdftype == SdfValueTypeNames->Token)
                vtvalue = VtValue(TfToken(str));
            if (sdftype == SdfValueTypeNames->PathExpression)
                vtvalue = VtValue(SdfPathExpression(str));
        }

        return vtvalue;
    }

    UT_StringHolder
    husdConvertParmValueToDefaultString(const UNI_ParmValue &value, int idx)
    {
        UT_StringHolder strvalue;

        if (std::holds_alternative<bool>(value))
            strvalue = std::get<bool>(value) ? "1" : "0";
        else if (std::holds_alternative<int64>(value))
            strvalue.sprintf("%lld", std::get<int64>(value));
        else if (std::holds_alternative<fpreal64>(value))
            strvalue.sprintf("%g", std::get<fpreal64>(value));
        else if (std::holds_alternative<UT_Vector2D>(value))
            strvalue.sprintf("%g", std::get<UT_Vector2D>(value).data()[idx]);
        else if (std::holds_alternative<UT_Vector3D>(value))
            strvalue.sprintf("%g", std::get<UT_Vector3D>(value).data()[idx]);
        else if (std::holds_alternative<UT_Vector4D>(value))
            strvalue.sprintf("%g", std::get<UT_Vector4D>(value).data()[idx]);
        else if (std::holds_alternative<UT_Matrix3D>(value))
            strvalue.sprintf("%g", std::get<UT_Matrix3D>(value).data()[idx]);
        else if (std::holds_alternative<UT_Matrix4D>(value))
            strvalue.sprintf("%g", std::get<UT_Matrix4D>(value).data()[idx]);
        else if (std::holds_alternative<UT_StringHolder>(value))
            strvalue = std::get<UT_StringHolder>(value);

        return strvalue;
    }

    PI_EditScriptedParm *
    husdCreateParmFromSdrInput(const SdrShaderPropertyConstPtr &input)
    {
        PI_EditScriptedParm *parm = nullptr;
        auto name = input->GetName();
        SdrSdfTypeIndicator sdrtype = input->GetTypeAsSdfType();
        std::pair<UT_StringHolder, int> typeandsize;
        if (sdrtype.HasSdfType())
            typeandsize = husdParmTypeAndSizeFromSdfType(sdrtype.GetSdfType());
        if (typeandsize.first.isstring())
        {
            parm = new PI_EditScriptedParm();
            parm->myName = input->GetName().GetText();
            parm->myLabel = input->GetLabel().GetText();
            parm->setType(typeandsize.first);
            parm->setSize(typeandsize.second);

            VtValue defvtvalue = input->GetDefaultValueAsSdfType();
            UNI_ParmValue defvalue = husdConvertVtValueToParmValue(defvtvalue);
            if (!std::holds_alternative<UNI_InvalidParmValue>(defvalue))
            {
                for (auto i = 0; i < typeandsize.second; ++i)
                    parm->myDefaults[i] =
                        husdConvertParmValueToDefaultString(defvalue, i);
            }
        }
        else
        {
            UT_ASSERT(!"Unsupported Sdr property type.");
        }

        return parm;
    }

    PI_EditScriptedParm *
    husdCreateParmFromUsdShadeInput(const UsdShadeInput &input)
    {
        PI_EditScriptedParm *parm = nullptr;
        auto name = input.GetBaseName();
        std::pair<UT_StringHolder, int> typeandsize;
        typeandsize = husdParmTypeAndSizeFromSdfType(input.GetTypeName());
        if (typeandsize.first.isstring())
        {
            parm = new PI_EditScriptedParm();
            parm->myName = input.GetBaseName().GetText();
            parm->myLabel = input.GetBaseName().GetText();
            parm->setType(typeandsize.first);
            parm->setSize(typeandsize.second);

            VtValue defvtvalue;
            input.Get(&defvtvalue);
            UNI_ParmValue defvalue = husdConvertVtValueToParmValue(defvtvalue);
            // An attribute may have no authored default value, in which case
            // we can get the implicit default value from the attribute type.
            if (std::holds_alternative<UNI_InvalidParmValue>(defvalue))
                defvalue = husdConvertVtValueToParmValue(
                    input.GetTypeName().GetDefaultValue());
            if (!std::holds_alternative<UNI_InvalidParmValue>(defvalue))
            {
                for (auto i = 0; i < typeandsize.second; ++i)
                    parm->myDefaults[i] =
                        husdConvertParmValueToDefaultString(defvalue, i);
            }
        }
        else
        {
            UT_ASSERT(!"Unsupported UsdShadeInput type.");
        }

        return parm;
    }

    const auto theDefaultWireStyle = TfToken("inherited");
    const auto theWireStyles = std::vector<TfToken>{
        TfToken("straight"),
        TfToken("rounded")
    };

    UT_Optional<UNI_WireStyle>
    husdConvertVtValueToWireStyle(const TfToken &token)
    {
        UT_Optional<UNI_WireStyle> result;

        auto it = std::find(theWireStyles.begin(), theWireStyles.end(), token);
        if (it != theWireStyles.end()) 
            result = static_cast<UNI_WireStyle>(
                    std::distance(theWireStyles.begin(), it));

        return result;
    }

    VtValue
    husdConvertWireStyleToVtValue(UNI_WireStyle wire_style)
    {
        TfToken token(theDefaultWireStyle);
        auto index = static_cast<int>(wire_style);
        if (0 <= index && index < theWireStyles.size())
            token = theWireStyles[index];
        else
            UT_ASSERT(!"Unknown wire style");

        return VtValue(token);
    }

    UT_StringHolder 
    husdGetShaderID(const UN_GraphData &graphdata, UN_NodeID nodeid,
            const UT_StringRef &signature)
    {
        // USD shader ID is the UN node's signature, and falls back on nodetype.
        // Note, see husdGetNodeTypeAndSignatureFromShaderID() 
        // how shader ID is parsed back into signature/typename.
        return signature ? signature : graphdata.nodeData().typeName(nodeid);
    }

    UT_StringHolder 
    husdGetShaderID(const UN_GraphData &graphdata, UN_NodeID nodeid)
    {
        return husdGetShaderID(graphdata, nodeid, 
            graphdata.nodeData().signatureName(nodeid));
    }

    std::pair<UT_StringHolder, UT_StringHolder>
    husdGetNodeTypeAndSignatureFromShaderID(const TfToken &shaderid)
    {
        UT_StringHolder signature(shaderid.GetText());
        auto &node_type_reg = UTverify_cast<husd_UniCategory*>(
                UNImanager().findCategory(HUSD_SHADER_GRAPH_CATEGORY))
            ->nodeTypeRegistry();
        auto &type_names = node_type_reg.typeNamesFromSignature(signature);

        // Note, see husdGetShaderID() how shader ID was constructed.
        UT_StringHolder node_type_name;
        UT_ASSERT(type_names.size() <= 1); // otherwise, ambiguous
        if (type_names.size() > 0)
        {
            node_type_name = type_names[0];
        }
        else 
        {
            node_type_name = signature;
            signature.clear();
        }

        return std::make_pair(node_type_name, signature);
    }

    /// RAII object that registers USD shade UNI category.
    struct husd_UniCategoryRegistrar
    {
        husd_UniCategoryRegistrar()
        {
            auto &mgr = UNImanager();
            UT_ASSERT(!mgr.hasCategoryFactory(HUSD_SHADER_GRAPH_CATEGORY));
            mgr.addCategoryFactory(HUSD_SHADER_GRAPH_CATEGORY, []()
            {
                return husdLoadUniCategory();
            });
            mgr.addRootType(HUSD_SHADER_GRAPH_CATEGORY,
                    UT_StringArray{ HUSD_SHADER_GRAPH_CATEGORY });
        }
    };

    husd_UniCategoryRegistrar theHusdUniCategoryRegistrar;
};

class HUSD_MaterialUniGraph::husd_MaterialUniGraphPrivate
{
public:
    SdfLayerRefPtr   myLayer;
};

HUSD_MaterialUniGraph::HUSD_MaterialUniGraph(
        HUSD_MaterialUniGraphDataHandleProvider data_handle_provider,
        const HUSD_Path &material_path,
        const std::string &in_layer,
        int material_index)
    : UN_BasicUniGraph(husdShaderGraphContext()),
      myDataHandleProviderFn(data_handle_provider),
      myMaterialPath(material_path),
      myMaterialIndex(material_index),
      myPrivate(UTmakeUnique<husd_MaterialUniGraphPrivate>())
{
    HUSD_AutoReadLock lock(myDataHandleProviderFn(),
        HUSD_AutoReadLock::OVERRIDES_UNCHANGED);
    buildGraphFromMaterial(lock);
    myPrivate->myLayer = SdfLayer::CreateAnonymous();
    if (!in_layer.empty())
        myPrivate->myLayer->ImportFromString(in_layer);
}

HUSD_MaterialUniGraph::~HUSD_MaterialUniGraph()
{
}

UNI_GraphHandle
HUSD_MaterialUniGraph::createGraph(
        HUSD_MaterialUniGraphDataHandleProvider data_handle_provider,
        const HUSD_Path &material_path,
        const std::string &in_layer,
        int material_index)
{
    auto graph = UT_UniquePtr<HUSD_MaterialUniGraph>(new HUSD_MaterialUniGraph(
        data_handle_provider, material_path, in_layer, material_index));
    return UNI_GraphHandle(std::move(graph));
}

void
HUSD_MaterialUniGraph::setWireStyle(UNI_WireStyle wire_style) 
{
    auto it = myNodeIdToPathMap.find(unGraphData().rootNode());
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        husdAddApiSchema(primspec, UsdHoudiniTokens->HoudiniNodeGraphContainerAPI);
        SdfAttributeSpecHandle attrspec = 
            husdCreateAttribute(primspec,
                UsdHoudiniTokens->houdiniContainerWireStyle,
                SdfValueTypeNames->Token);
        attrspec->SetDefaultValue(husdConvertWireStyleToVtValue(wire_style));
    }

    // Base class sends a change notification. Also keep base class in-sync.
    UN_UniGraph::setWireStyle(wire_style);
}

UNI_NodeID
HUSD_MaterialUniGraph::createNode( UNI_NodeID parent_id,
        const UT_StringHolder &node_name,
        const UT_StringHolder &type_name,
        const UT_StringHolder &signature_name ) 
{
    auto it = myNodeIdToPathMap.find(parent_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfLayerRefPtr layer = myPrivate->myLayer;
        HUSD_Path path = it->second.appendChild(node_name);
        SdfPrimSpecHandle primspec;

        // Make the new UsdShadeShader prim.
        primspec = husdCreatePrim(layer, path.sdfPath(), SdfSpecifierDef,
            TfToken(HUSD_Constants::getShaderPrimTypeName().toStdString()));

        // Update the name of the node in our internal map lookup.
        if (primspec)
        {
            UNI_NodeID id = UN_BasicUniGraph::createNode(
                parent_id, node_name, type_name, signature_name);
            myNodeIdToPathMap[id] = path;

            if( type_name == HUSD_NODEGRAPH_NODE_TYPE )
            {
                unGraphData().createPort(id, UNI_PortKind::Input,
                        theNewPortName.asRef(), UT_StringHolder());
                unGraphData().createPort(id, UNI_PortKind::Output,
                        theNewPortName.asRef(), UT_StringHolder());
            }

            auto shaderid = husdGetShaderID(unGraphData(), id);
            if (type_name != HUSD_NODEGRAPH_NODE_TYPE)
                husdAddApiSchema(primspec, UsdShadeTokens->NodeDefAPI);
            SdfAttributeSpecHandle attrspec = husdCreateAttribute(primspec,
                UsdShadeTokens->infoId, SdfValueTypeNames->Token);
            attrspec->SetDefaultValue(VtValue(TfToken(shaderid.toStdString())));

            return id;
        }
    }
    return UNI_NodeID();
}


void
HUSD_MaterialUniGraph::setName( UNI_NodeID node_id,
        const UT_StringHolder &name)
{
    auto it = myNodeIdToPathMap.find(node_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfLayerRefPtr layer = myPrivate->myLayer;
        SdfPath oldpath(it->second.sdfPath());
        SdfPath newpath = oldpath.GetParentPath().
            AppendChild(TfToken(name.toStdString()));
        SdfPrimSpecHandle oldprimspec = layer->GetPrimAtPath(oldpath);
        SdfPrimSpecHandle newprimspec;
        bool is_def = false;

        // Make a copy from the old prim location to the new prim location,
        // and delete anything at the old prim location.
        if (oldprimspec)
        {
            is_def = oldprimspec->GetSpecifier() == SdfSpecifierDef;
            SdfCopySpec(layer, oldpath, layer, newpath);
            if (oldprimspec->GetNameParent())
                oldprimspec->GetNameParent()->RemoveNameChild(oldprimspec);
            else
                layer->RemoveRootPrim(oldprimspec);
            newprimspec = layer->GetPrimAtPath(newpath);
            UT_ASSERT(newprimspec);
        }
        if (!newprimspec)
            newprimspec = husdCreatePrim(layer, newpath);

        // If the prim is defined in this layer, that means everything
        // there is to know about it is in this layer, so we don't need
        // to do anything else to rename it. If the spec already has
        // references, that means it's already getting most of its
        // definition from somewhere else, and that's fine, we don't
        // need to do anything else.
        if (!is_def && newprimspec && !newprimspec->HasReferences())
        {
            // In here, the old prim didn't exist or was a pure over with
            // no references. In this case, we want to add a reference back
            // to the old prim location. We also want to deactivate that old
            // location and explicitly activate the new location.
            oldprimspec = husdCreatePrim(layer, oldpath);
            oldprimspec->SetActive(false);
            newprimspec->SetActive(true);
            newprimspec->GetReferenceList().GetExplicitItems().
                push_back(SdfReference(std::string(), oldpath));
        }

        // TODO: We now have the newprimspec set up. Now we need to reconnect
        // any wires with the oldprimspec as their "src" to the newprimspec.
        // TODO: If isDef is false, we need to make a UN_Graph node for the
        // deactivated old prim location.

        // Update the name of the node in our internal map lookup.
        myNodeIdToPathMap[node_id] = newpath;
    }
    UN_BasicUniGraph::setName(node_id, name);
}

void
HUSD_MaterialUniGraph::setSignature( UNI_NodeID node_id,
        const UT_StringHolder &signature_name ) 
{
    auto it = myNodeIdToPathMap.find(node_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        if (unGraphData().nodeData().typeName(node_id) 
                != HUSD_NODEGRAPH_NODE_TYPE)
            husdAddApiSchema(primspec, UsdShadeTokens->NodeDefAPI);
        SdfAttributeSpecHandle attrspec = husdCreateAttribute(primspec,
            UsdShadeTokens->infoId, SdfValueTypeNames->Token);

        auto shaderid = husdGetShaderID(unGraphData(), node_id, signature_name);
        attrspec->SetDefaultValue(VtValue(TfToken(shaderid.toStdString())));
    }
    UN_BasicUniGraph::setSignature(node_id, signature_name);

}

void
HUSD_MaterialUniGraph::setPosition(UNI_NodeID node_id, const UT_Vector2D &pos)
{
    auto it = myNodeIdToPathMap.find(node_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        husdAddApiSchema(primspec, UsdUITokens->NodeGraphNodeAPI);
        SdfAttributeSpecHandle attrspec =
            husdCreateAttribute(primspec, UsdUITokens->uiNodegraphNodePos,
                SdfValueTypeNames->Float2);
        attrspec->SetDefaultValue(VtValue(GusdUT_Gf::Cast(UT_Vector2F(pos))));
    }
    UN_BasicUniGraph::setPosition(node_id, pos);
}

void
HUSD_MaterialUniGraph::setColor(UNI_NodeID node_id, const UT_Color &clr)
{
    auto it = myNodeIdToPathMap.find(node_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        husdAddApiSchema(primspec, UsdUITokens->NodeGraphNodeAPI);
        SdfAttributeSpecHandle attrspec =
            husdCreateAttribute(primspec, UsdUITokens->uiNodegraphNodeDisplayColor,
                SdfValueTypeNames->Color3f);
        GfVec3f gfclr;
        clr.getRGB(gfclr.data(), gfclr.data()+1, gfclr.data()+2);
        attrspec->SetDefaultValue(VtValue(gfclr));
    }
    UN_BasicUniGraph::setColor(node_id, clr);
}

void
HUSD_MaterialUniGraph::setComment(UNI_NodeID node_id,
        const UT_StringHolder &comment)
{
    auto it = myNodeIdToPathMap.find(node_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        husdAddCustomData(primspec,
            HUSD_Constants::getCommentCustomDataName(), comment);
    }
    UN_BasicUniGraph::setComment(node_id, comment);
}

void
HUSD_MaterialUniGraph::setTags(UNI_NodeID node_id,
        const UT_StringArray &tags)
{
    auto it = myNodeIdToPathMap.find(node_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        husdAddCustomData(primspec,
            HUSD_Constants::getTagsCustomDataName(), tags);
    }
    UN_BasicUniGraph::setTags(node_id, tags);
}

UT_StringHolder
HUSD_MaterialUniGraph::parmDialogScript( UNI_NodeID node_id ) const
{
    UT_OStringStream os;
    auto it = myNodeIdToPathMap.find(node_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        PI_EditScriptedParms piparms;
        SdrRegistry &sdrregistry = SdrRegistry::GetInstance();
        TfToken type_name = TfToken(typeName(node_id).toStdString());
        auto sdrnode = sdrregistry.GetShaderNodeByIdentifier(type_name);
        if (sdrnode)
        {
            for (auto &&input_name : sdrnode->GetShaderInputNames())
            {
                auto input = sdrnode->GetShaderInput(input_name);
                auto *piparm = husdCreateParmFromSdrInput(input);
                if (piparm)
                    piparms.addParm(piparm);
            }
        }
        else
        {
            HUSD_AutoReadLock lock(myDataHandleProviderFn(),
                HUSD_AutoReadLock::OVERRIDES_UNCHANGED);

            if (lock.isStageValid())
            {
                UsdPrim prim(lock.constData()->stage()->
                    GetPrimAtPath(it->second.sdfPath()));
                UsdShadeConnectableAPI child_connectable(prim);
                if (child_connectable)
                {
                    for (auto &&input : child_connectable.GetInputs())
                    {
                        auto *piparm = husdCreateParmFromUsdShadeInput(input);
                        if (piparm)
                            piparms.addParm(piparm);
                    }
                }
            }
        }

        UT_String warnings;
        os << "{\n    name parameters\n\n";
        piparms.save(os, warnings, false);
        os << "}\n";
    }
    return os.str();
}

UNI_ParmValue
HUSD_MaterialUniGraph::parmValue( UNI_NodeID node_id,
        const UT_StringRef &parm_name ) const
{
    auto it = myNodeIdToPathMap.find(node_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        HUSD_AutoReadLock lock(myDataHandleProviderFn(),
            HUSD_AutoReadLock::OVERRIDES_UNCHANGED);
        std::string parm_name_str = parm_name.toStdString();
        SdfPath attrpath = it->second.sdfPath().AppendProperty(TfToken(
            UsdShadeTokens->inputs.GetString() + parm_name_str));

        // Get the parm value from the attribute authored on the stage.
        if (lock.isStageValid())
        {
            auto attr = lock.constData()->stage()->GetAttributeAtPath(attrpath);
            if (attr && attr.HasAuthoredValue())
            {
                VtValue vtvalue;
                if (attr.Get(&vtvalue, UsdTimeCode::EarliestTime()))
                {
                    UNI_ParmValue value =
                        husdConvertVtValueToParmValue(vtvalue);
                    if (!std::holds_alternative<UNI_InvalidParmValue>(value))
                        return value;
                }
            }
        }

        // Get the default value from the shader registry.
        SdrRegistry &sdrregistry = SdrRegistry::GetInstance();
        TfToken type_name = TfToken(typeName(node_id).toStdString());
        auto sdrnode = sdrregistry.GetShaderNodeByIdentifier(type_name);
        if (sdrnode)
        {
            auto input = sdrnode->GetShaderInput(TfToken(parm_name_str));
            if (input)
            {
                VtValue defvtvalue = input->GetDefaultValueAsSdfType();
                return husdConvertVtValueToParmValue(defvtvalue);
            }
        }
    }

    return UNI_ParmValue();
}

void
HUSD_MaterialUniGraph::setParmValue( UNI_NodeID node_id,
        const UT_StringRef &parm_name,
        const UNI_ParmValue &value )
{
    auto it = myNodeIdToPathMap.find(node_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        std::string parm_name_str = parm_name.toStdString();
        SdfPath attrpath = it->second.sdfPath().AppendProperty(TfToken(
            UsdShadeTokens->inputs.GetString() + parm_name_str));

        // Get the default value from the shader registry.
        SdrRegistry &sdrregistry = SdrRegistry::GetInstance();
        TfToken type_name = TfToken(typeName(node_id).toStdString());
        auto sdrnode = sdrregistry.GetShaderNodeByIdentifier(type_name);
        if (sdrnode)
        {
            auto input = sdrnode->GetShaderInput(TfToken(parm_name_str));
            if (input)
            {
                SdfValueTypeName sdftype = input->GetTypeAsSdfType().GetSdfType();
                VtValue defvtvalue = input->GetDefaultValueAsSdfType();
                VtValue vtvalue = husdConvertParmValueToVtValue(value, sdftype);
                SdfPrimSpecHandle primspec =
                    husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
                SdfAttributeSpecHandle attrspec = husdCreateAttribute(
                    primspec, attrpath.GetNameToken(), sdftype);
                // To set a value to its default value, block the value.
                if (vtvalue == defvtvalue)
                    vtvalue = VtValue(SdfValueBlock());
                attrspec->SetDefaultValue(vtvalue);
            }
        }
    }
}

void
HUSD_MaterialUniGraph::destroy( UNI_NodeID node_id )
{
    auto it = myNodeIdToPathMap.find(node_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfLayerRefPtr layer = myPrivate->myLayer;
        SdfPrimSpecHandle primspec = layer->GetPrimAtPath(it->second.sdfPath());

        // We can only delete nodes defined in this layer.
        if (primspec && primspec->GetSpecifier() == SdfSpecifierDef)
        {
            // Explicitly destroy all "output" wires associated with this node
            // so that the required USD updates will be made on the destination
            // prims to eliminate the connections to this node.
            for (auto &&port_id : outputPorts(node_id))
            {
                for (auto &&wire_id : dstWires(port_id))
                {
                    destroy(wire_id);
                }
            }
            // Remove the defined prim and erase it from our map.
            primspec->GetNameParent()->RemoveNameChild(primspec);
            myNodeIdToPathMap.erase(it);
            UN_BasicUniGraph::destroy(node_id);
        }
    }
}

void
HUSD_MaterialUniGraph::setContainerConnectPosition( UNI_NodeID parent_id,
        UNI_ContainerConnectType contype,
        const UT_Vector2D &pos )
{
    auto it = myNodeIdToPathMap.find(parent_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        husdAddApiSchema(primspec, UsdHoudiniTokens->HoudiniNodeGraphContainerAPI);
        SdfAttributeSpecHandle attrspec =
            husdCreateAttribute(primspec,
                contype == UNI_ContainerConnectType::INPUTS
                    ? UsdHoudiniTokens->houdiniContainerInputPos
                    : UsdHoudiniTokens->houdiniContainerOutputPos,
                SdfValueTypeNames->Float2);
        attrspec->SetDefaultValue(VtValue(GusdUT_Gf::Cast(UT_Vector2F(pos))));
    }
    UN_UniGraph::setContainerConnectPosition(parent_id, contype, pos);
}

void
HUSD_MaterialUniGraph::setContainerConnectColor( UNI_NodeID parent_id,
        UNI_ContainerConnectType contype,
        const UT_Color &clr )
{
    auto it = myNodeIdToPathMap.find(parent_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        husdAddApiSchema(primspec, UsdHoudiniTokens->HoudiniNodeGraphContainerAPI);
        SdfAttributeSpecHandle attrspec =
            husdCreateAttribute(primspec,
                contype == UNI_ContainerConnectType::INPUTS
                    ? UsdHoudiniTokens->houdiniContainerInputDisplayColor
                    : UsdHoudiniTokens->houdiniContainerOutputDisplayColor,
                SdfValueTypeNames->Color3f);
        attrspec->SetDefaultValue(VtValue(GusdUT_Gf::Cast(clr.rgb())));
    }
    UN_UniGraph::setContainerConnectColor(parent_id, contype, clr);
}

UNI_WireID
HUSD_MaterialUniGraph::createWire(UNI_PortID src, UNI_PortID dst)
{
    UNI_Port srcport(graphID(), src);
    UNI_Node srcnode(graphID(), srcport.node());
    UNI_Port dstport(graphID(), dst);
    UNI_Node dstnode(graphID(), dstport.node());

    auto srcit = myNodeIdToPathMap.find(srcnode.id());
    auto dstit = myNodeIdToPathMap.find(dstnode.id());
    UT_ASSERT(srcit != myNodeIdToPathMap.end());
    UT_ASSERT(dstit != myNodeIdToPathMap.end());
    if (srcit != myNodeIdToPathMap.end() && dstit != myNodeIdToPathMap.end())
    {
        if (dstport.name() == theNewPortName.asRef() &&
            srcport.name() == theNewPortName.asRef())
        {
            UT_StringHolder dstportname = husdCreateUniquePortName(dstnode,
                dstport.portKind(), UT_StringHolder());
            dst = unGraphData().createPort(dstnode.id(), dstport.portKind(),
                dstportname, UT_StringRef(), dstportname, -1);
            if (!dst.isValid())
                return UNI_WireID();
            dstport = UNI_Port(graphID(), dst);
            UT_StringHolder srcportname = husdCreateUniquePortName(srcnode,
                srcport.portKind(), UT_StringHolder());
            src = unGraphData().createPort(srcnode.id(), srcport.portKind(),
                srcportname, UT_StringRef(), srcportname, -1);
            if (!src.isValid())
                return UNI_WireID();
            srcport = UNI_Port(graphID(), src);
        }
        else if (dstport.name() == theNewPortName.asRef())
        {
            UT_StringHolder dstportname = husdCreateUniquePortName(dstnode,
                dstport.portKind(), srcport.name());
            dst = unGraphData().createPort(dstnode.id(), dstport.portKind(),
                dstportname, srcport.typeName(), srcport.label(), -1);
            if (!dst.isValid())
                return UNI_WireID();
            dstport = UNI_Port(graphID(), dst);
        }
        else if (srcport.name() == theNewPortName.asRef())
        {
            UT_StringHolder srcportname = husdCreateUniquePortName(srcnode,
                srcport.portKind(), dstport.name());
            src = unGraphData().createPort(srcnode.id(), srcport.portKind(),
                srcportname, dstport.typeName(), dstport.label(), -1);
            if (!src.isValid())
                return UNI_WireID();
            srcport = UNI_Port(graphID(), src);
        }
        SdfPrimSpecHandle dstprimspec =
            husdCreatePrim(myPrivate->myLayer, dstit->second.sdfPath());
        SdfAttributeSpecHandle dstattrspec =
            husdCreatePortAttribute(dstprimspec, dstport);
        SdfPrimSpecHandle srcprimspec =
            husdCreatePrim(myPrivate->myLayer, srcit->second.sdfPath());
        SdfAttributeSpecHandle srcattrspec =
            husdCreatePortAttribute(srcprimspec, srcport);
        if (srcattrspec && dstattrspec)
        {
            auto pathlist = dstattrspec->GetConnectionPathList();
            pathlist.Add(srcattrspec->GetPath());
            return UN_BasicUniGraph::createWire(src, dst);
        }
    }
    return UNI_WireID();
}

void
HUSD_MaterialUniGraph::destroy(UNI_WireID wire_id)
{
    UNI_Wire wire(graphID(), wire_id);
    UNI_Port srcport(graphID(), wire.src());
    UNI_Node srcnode(graphID(), srcport.node());
    UNI_Port dstport(graphID(), wire.dst());
    UNI_Node dstnode(graphID(), dstport.node());

    auto srcit = myNodeIdToPathMap.find(srcnode.id());
    auto dstit = myNodeIdToPathMap.find(dstnode.id());
    UT_ASSERT(srcit != myNodeIdToPathMap.end());
    UT_ASSERT(dstit != myNodeIdToPathMap.end());
    if (srcit != myNodeIdToPathMap.end() && dstit != myNodeIdToPathMap.end())
    {
        TfToken dstattrname = TfToken(dstport.name().toStdString());
        SdfPrimSpecHandle dstprimspec =
            husdCreatePrim(myPrivate->myLayer, dstit->second.sdfPath());
        SdfAttributeSpecHandle dstattrspec =
            husdCreatePortAttribute(dstprimspec, dstport);
        if (dstattrspec)
        {
            TfToken srcattrname = husdMakePortAttributeName(srcport);
            SdfPath srcattrpath = srcit->second.sdfPath().AppendProperty(srcattrname);
            auto pathlist = dstattrspec->GetConnectionPathList();
            pathlist.Remove(srcattrpath);
        }
    }
    UN_BasicUniGraph::destroy(wire_id);
}

UNI_StickyNoteID
HUSD_MaterialUniGraph::createStickyNote( UNI_NodeID parent_id,
        const UT_StringHolder &name )
{
    auto it = myNodeIdToPathMap.find(parent_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it != myNodeIdToPathMap.end())
    {
        SdfPath backdrop_path =
            it->second.sdfPath().AppendChild(TfToken(name.toStdString()));
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, backdrop_path,
                SdfSpecifierDef, UsdUITokens->Backdrop);

        UNI_StickyNoteID id = UN_BasicUniGraph::createStickyNote(parent_id, name);
        myStickyNoteIdToPathMap.emplace(id, backdrop_path);
        return id;
    }

    return UNI_StickyNoteID();
}

void
HUSD_MaterialUniGraph::setName( UNI_StickyNoteID note_id,
        const UT_StringHolder &name )
{
    UN_BasicUniGraph::setName(note_id, name);
}

void
HUSD_MaterialUniGraph::setPosition( UNI_StickyNoteID note_id,
        const UT_Vector2D &pos )
{
    auto it = myStickyNoteIdToPathMap.find(note_id);
    UT_ASSERT(it != myStickyNoteIdToPathMap.end());
    if (it != myStickyNoteIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        husdAddApiSchema(primspec, UsdUITokens->NodeGraphNodeAPI);
        SdfAttributeSpecHandle attrspec =
            husdCreateAttribute(primspec, UsdUITokens->uiNodegraphNodePos,
                SdfValueTypeNames->Float2);
        attrspec->SetDefaultValue(VtValue(GusdUT_Gf::Cast(UT_Vector2F(pos))));
    }
    UN_BasicUniGraph::setPosition(note_id, pos);
}

void
HUSD_MaterialUniGraph::setSize( UNI_StickyNoteID note_id,
        const UT_Vector2D &size )
{
    auto it = myStickyNoteIdToPathMap.find(note_id);
    UT_ASSERT(it != myStickyNoteIdToPathMap.end());
    if (it != myStickyNoteIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
                husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        husdAddApiSchema(primspec, UsdUITokens->NodeGraphNodeAPI);
        SdfAttributeSpecHandle attrspec =
            husdCreateAttribute(primspec, UsdUITokens->uiNodegraphNodeSize,
                SdfValueTypeNames->Float2);
        attrspec->SetDefaultValue(VtValue(GusdUT_Gf::Cast(UT_Vector2F(size))));
    }
    UN_BasicUniGraph::setSize(note_id, size);
}

void
HUSD_MaterialUniGraph::setColor( UNI_StickyNoteID note_id,
        const UT_Color &clr )
{
    auto it = myStickyNoteIdToPathMap.find(note_id);
    UT_ASSERT(it != myStickyNoteIdToPathMap.end());
    if (it != myStickyNoteIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        husdAddApiSchema(primspec, UsdUITokens->NodeGraphNodeAPI);
        SdfAttributeSpecHandle attrspec =
            husdCreateAttribute(primspec, UsdUITokens->uiNodegraphNodeDisplayColor,
                SdfValueTypeNames->Color3f);
        GfVec3f gfclr;
        clr.getRGB(gfclr.data(), gfclr.data()+1, gfclr.data()+2);
        attrspec->SetDefaultValue(VtValue(gfclr));
    }
    UN_BasicUniGraph::setColor(note_id, clr);
}

void
HUSD_MaterialUniGraph::setText( UNI_StickyNoteID note_id,
        const UT_StringHolder &text )
{
    auto it = myStickyNoteIdToPathMap.find(note_id);
    UT_ASSERT(it != myStickyNoteIdToPathMap.end());
    if (it != myStickyNoteIdToPathMap.end())
    {
        SdfPrimSpecHandle primspec =
            husdCreatePrim(myPrivate->myLayer, it->second.sdfPath());
        husdAddApiSchema(primspec, UsdUITokens->NodeGraphNodeAPI);
        SdfAttributeSpecHandle attrspec =
            husdCreateAttribute(primspec, UsdUITokens->uiDescription,
                SdfValueTypeNames->String);
        attrspec->SetDefaultValue(VtValue(text.toStdString()));
    }
    UN_BasicUniGraph::setText(note_id, text);
}

void
HUSD_MaterialUniGraph::setTextColor( UNI_StickyNoteID note_id,
        const UT_Color &clr )
{
    UN_BasicUniGraph::setTextColor(note_id, clr);
}

void
HUSD_MaterialUniGraph::setTextSize( UNI_StickyNoteID note_id,
        const float size )
{
    UN_BasicUniGraph::setTextSize(note_id, size);
}

void
HUSD_MaterialUniGraph::setCollapsed( UNI_StickyNoteID note_id,
        bool collapsed )
{
    UN_BasicUniGraph::setCollapsed(note_id, collapsed);
}

void
HUSD_MaterialUniGraph::setBackgroundHidden( UNI_StickyNoteID note_id,
        bool hidden )
{
    UN_BasicUniGraph::setBackgroundHidden(note_id, hidden);
}

void
HUSD_MaterialUniGraph::destroy( UNI_StickyNoteID note_id )
{
    auto it = myStickyNoteIdToPathMap.find(note_id);
    UT_ASSERT(it != myStickyNoteIdToPathMap.end());
    if (it != myStickyNoteIdToPathMap.end())
    {
        SdfLayerRefPtr layer = myPrivate->myLayer;
        SdfPrimSpecHandle primspec = layer->GetPrimAtPath(it->second.sdfPath());

        // We can only delete sticky notes defined in this layer.
        if (primspec && primspec->GetSpecifier() == SdfSpecifierDef)
        {
            // Remove the defined prim and erase it from our map.
            primspec->GetNameParent()->RemoveNameChild(primspec);
            myStickyNoteIdToPathMap.erase(it);
            UN_BasicUniGraph::destroy(note_id);
        }
    }
}

static bool
_ShouldCopyValue(
    const UT_Map<SdfPath, SdfPath> &pathmap,
    SdfSpecType specType,
    const TfToken& field,
    const SdfLayerHandle& srcLayer,
    const SdfPath& srcPath,
    bool fieldInSrc,
    const SdfLayerHandle& dstLayer,
    const SdfPath& dstPath,
    bool fieldInDst,
    std::optional<VtValue>* valueToCopy)
{
    if (fieldInSrc) {
        if (field == SdfFieldKeys->ConnectionPaths ||
            field == SdfFieldKeys->TargetPaths) {
	    SdfPathListOp srcListOp;
            if (srcLayer->HasField(srcPath, field, &srcListOp)) {
                srcListOp.ModifyOperations(
                    [&pathmap](const SdfPath& path) {
                        // TODO: Deal with paths to container subnet inputs and
                        //       outputs. Either we need to create connections
                        //       on the destination subnet, or delete these
                        //       connections. We should also delete any
                        //       connections to nodes that don't exist in the
                        //       destination subnet. Need more data passed into
                        //       this function about the destination subnet...
                        for (auto &&it : pathmap)
                        {
                            if (path.HasPrefix(it.first))
                                return path.ReplacePrefix(it.first, it.second);
                        }
                        return path;
                    });

                *valueToCopy = VtValue::Take(srcListOp);
            }
        }
    }
    return true;
}

bool
_ShouldCopyChildren(
    const TfToken& childrenField,
    const SdfLayerHandle& srcLayer,
    const SdfPath& srcPath,
    bool fieldInSrc,
    const SdfLayerHandle& dstLayer,
    const SdfPath& dstPath,
    bool fieldInDst,
    std::optional<VtValue>* srcChildren,
    std::optional<VtValue>* dstChildren)
{
    return true;
}

bool
HUSD_MaterialUniGraph::copyItems(UNI_NodeID parent_id,
        const UNI_NodeIDList &node_ids,
        const UNI_StickyNoteIDList &note_ids,
        std::ostream &os) const
{
    // Create a population mask that includes only the prims we want.
    UsdStagePopulationMask mask;
    for (auto &&node_id : node_ids)
    {
        auto it = myNodeIdToPathMap.find(node_id);
        if (parentNode(node_id) == parent_id &&
            it != myNodeIdToPathMap.end())
            mask.Add(it->second.sdfPath());
    }
    for (auto &&note_id : note_ids)
    {
        auto it = myStickyNoteIdToPathMap.find(note_id);
        if (ownerNode(note_id) == parent_id &&
            it != myStickyNoteIdToPathMap.end())
            mask.Add(it->second.sdfPath());
    }
    if (mask.IsEmpty())
        return false;

    HUSD_AutoReadLock lock(myDataHandleProviderFn(),
        HUSD_AutoReadLock::OVERRIDES_UNCHANGED);
    if (!lock.isStageValid())
        return false;
    auto stage = lock.constData()->stage();

    // Create a new stage with the same root layer but limited by the population mask
    // Use UsdStage::LoadNone to avoid loading payloads
    UsdStageRefPtr maskedStage = UsdStage::OpenMasked(
        stage->GetRootLayer(),
        nullptr,
        stage->GetPathResolverContext(),
        mask,
        UsdStage::LoadNone
    );

    if (!maskedStage)
        return false;

    // TODO: Strip of any "references" (and other composition arcs?) authored
    //       on the prims on the stage being copied. Then do the flatten, then
    //       restore the composition arcs? We want node graphs with referenced
    //       contents to remain node graphs with referenced contents. Internal
    //       references, inherits, and specializes are probably to nodes
    //       outside the material, so it's probably best to preserve those
    //       without attempting any remapping?

    // Flatten the masked stage's layer stack
    // This only flattens the subtree specified by the population mask
    SdfLayerRefPtr flattened_layer = maskedStage->Flatten(false);
    if (!flattened_layer)
        return false;
    SdfLayerRefPtr copy_layer = SdfLayer::CreateAnonymous();
    UT_Map<SdfPath, SdfPath> path_remapping;

    // Move all the prims to be direct children of the pseudo root prim.
    for (auto &&primpath : mask.GetPaths())
    {
        SdfPath dstpath = SdfPath::AbsoluteRootPath().
            AppendChild(primpath.GetNameToken());
        path_remapping.emplace(primpath, dstpath);
    }

    // Move all the prims to be direct children of the pseudo root prim,
    // updating all connections and relationships as we go.
    for (auto &&primpath : mask.GetPaths())
    {
        SdfPath dstpath = SdfPath::AbsoluteRootPath().
            AppendChild(primpath.GetNameToken());
        SdfCopySpec(flattened_layer, primpath, copy_layer, dstpath,
            std::bind(_ShouldCopyValue,
                std::cref(path_remapping),
                ph::_1, ph::_2, ph::_3, ph::_4, ph::_5,
                ph::_6, ph::_7, ph::_8, ph::_9),
            std::bind(_ShouldCopyChildren,
                ph::_1, ph::_2, ph::_3, ph::_4, ph::_5,
                ph::_6, ph::_7, ph::_8, ph::_9));
    }

    std::string layerstr;
    copy_layer->ExportToString(&layerstr);
    os.write(layerstr.c_str(), layerstr.size());
    return true;
}

bool
HUSD_MaterialUniGraph::pasteItems(UNI_NodeID parent_id,
        UNI_NodeIDList &node_ids,
        UNI_StickyNoteIDList &note_ids,
        std::istream &is,
        UT_StringMap<UT_StringHolder> *rename_map)
{
    auto it = myNodeIdToPathMap.find(parent_id);
    UT_ASSERT(it != myNodeIdToPathMap.end());
    if (it == myNodeIdToPathMap.end())
        return false;
    SdfPath dest_parent_path = it->second.sdfPath();

    SdfLayerRefPtr load_layer = SdfLayer::CreateAnonymous();
    std::ostringstream ss;
    ss << is.rdbuf();
    if (!load_layer->ImportFromString(ss.str()))
        return false;

    UT_StringSet existing_names;
    {
        // Get the current stage to find all prims and avoid conflicst.
        HUSD_AutoReadLock lock(myDataHandleProviderFn(),
            HUSD_AutoReadLock::OVERRIDES_UNCHANGED);
        if (!lock.isStageValid())
            return false;
        auto stage = lock.constData()->stage();
        UsdPrim dest_parent_prim = stage->GetPrimAtPath(dest_parent_path);
        if (!dest_parent_prim)
            return false;

        // Generate the set of paths that need to be unique-ified because
        // they already exist in this parent prim.
        for (auto &&child_prim : dest_parent_prim.GetAllChildren())
            existing_names.insert(child_prim.GetPath().GetName());
    }

    // Gather the names of all root prims we are going to paste. We keep this
    // separate from the existing prim names on the stage because we want to
    // handle conflicts differently.
    SdfPrimSpecHandle root_spec = load_layer->GetPseudoRoot();
    UT_StringSet root_names;
    for (auto &&child_spec : root_spec->GetNameChildren())
        existing_names.insert(child_spec->GetPath().GetName());

    // Figure out any name remapping we need to do before copying the prims
    // from the loaded layer into our parent prim.
    UT_Map<SdfPath, SdfPath> path_remapping;
    HUSD_PathSet pasted_paths;
    for (auto &&child_spec : root_spec->GetNameChildren())
    {
        UT_String new_name = child_spec->GetName().c_str();
        if (existing_names.contains(new_name))
        {
            // When we are changing a root prim name, we want to make sure
            // that the new name doesn't match an existing stage prim or any
            // other root prim on the loaded layer.
            while (existing_names.contains(new_name) ||
                   root_names.contains(new_name))
                new_name.incrementNumberedName();
            existing_names.insert(new_name);
        }
        SdfPath srcpath = SdfPath::AbsoluteRootPath().
            AppendChild(child_spec->GetNameToken());
        SdfPath destpath = dest_parent_path.
            AppendChild(TfToken(new_name.toStdString()));
        path_remapping.emplace(srcpath, destpath);
        pasted_paths.insert(destpath);
    }

    // TODO: Collect more information about the destination subnet to allow
    //       _ShouldCopyValue to remap or delete connections based on the
    //       names of nodes/ports available in the destination subnet.

    // Copy the root prim specs into our editable layer,
    // updating all connections and relationships as we go.
    for (auto &&child_spec : root_spec->GetNameChildren())
    {
        auto it = path_remapping.find(child_spec->GetPath());
        if (it == path_remapping.end())
        {
            UT_ASSERT(!"Child prim path wasn't remapped");
            continue;
        }
        SdfCopySpec(load_layer, it->first,
            myPrivate->myLayer, it->second,
            std::bind(_ShouldCopyValue,
                std::cref(path_remapping),
                ph::_1, ph::_2, ph::_3, ph::_4, ph::_5,
                ph::_6, ph::_7, ph::_8, ph::_9),
            std::bind(_ShouldCopyChildren,
                ph::_1, ph::_2, ph::_3, ph::_4, ph::_5,
                ph::_6, ph::_7, ph::_8, ph::_9));
    }

    // Rather than trying to turn the new Usd prims into UN graph data, just
    // retranslate the entire graph. This requires "publishing" or updated
    // layer back to the node before rebuilding the graph.
    forceTriggerChangeCallbacks();
    HUSD_AutoReadLock lock(myDataHandleProviderFn(),
        HUSD_AutoReadLock::OVERRIDES_UNCHANGED);
    buildGraphFromMaterial(lock);

    // Select all the pasted nodes and notes.
    for (auto &&it : myNodeIdToPathMap)
        if (pasted_paths.contains(it.second))
            node_ids.append(it.first);
    for (auto &&it : myStickyNoteIdToPathMap)
        if (pasted_paths.contains(it.second))
            note_ids.append(it.first);

    return true;
}

bool
HUSD_MaterialUniGraph::getEditLayerContent(std::string *out_layer) const
{
    return myPrivate->myLayer->ExportToString(out_layer);
}

int
HUSD_MaterialUniGraph::getMaterialIndex() const
{
    return myMaterialIndex;
}

void
HUSD_MaterialUniGraph::buildGraphFromMaterial(const HUSD_AutoAnyLock &lock)
{
    auto create_ports_fn = [&](const UsdPrim &child, UNI_NodeID node_id)
    {
        UsdShadeConnectableAPI child_connectable(child);
        if (child_connectable)
        {
            for (auto &&input : child_connectable.GetInputs())
            {
                unGraphData().createPort(node_id, UNI_PortKind::Input,
                    input.GetBaseName().GetString(),
                    input.GetTypeName().GetAsToken().GetString());
            }
            unGraphData().createPort(node_id, UNI_PortKind::Input,
                theNewPortName.asRef(), UT_StringHolder());
            for (auto &&output : child_connectable.GetOutputs())
            {
                unGraphData().createPort(node_id, UNI_PortKind::Output,
                    output.GetBaseName().GetString(),
                    output.GetTypeName().GetAsToken().GetString());
            }
            unGraphData().createPort(node_id, UNI_PortKind::Output,
                theNewPortName.asRef(), UT_StringHolder());
        }
    };

    // Start by clearing whatever we used to have.
    unGraphData().clearData(true);

    // Get the UsdShadeMaterial prim.
    if (!lock.isStageValid())
        return;
    auto stage = lock.constData()->stage();
    if (!stage)
        return;
    if (myMaterialPath.isEmpty())
        return;
    UsdShadeMaterial material(stage->GetPrimAtPath(myMaterialPath.sdfPath()));
    if (!material)
        return;
    UT_Map<SdfPath, UN_NodeID> node_map;
    UT_Map<SdfPath, UN_StickyNoteID> backdrop_map;
    node_map.emplace(myMaterialPath.sdfPath(), unGraphData().rootNode());
    create_ports_fn(material.GetPrim(), unGraphData().rootNode());

    // Create all the child shader nodes, including instance proxies.
    Usd_PrimFlagsPredicate pred = UsdTraverseInstanceProxies();
    for (auto &&child : material.GetPrim().GetFilteredDescendants(pred))
    {
        auto parentit = node_map.find(child.GetParent().GetPath());

        if (parentit == node_map.end())
        {
            UT_ASSERT(!"Prim with unrecognized parent");
            continue;
        }

        UsdShadeShader childshader(child);
        if (childshader)
        {
            TfToken shaderid;
            childshader.GetShaderId(&shaderid);
            auto [type_name, signature] = 
                husdGetNodeTypeAndSignatureFromShaderID(shaderid);

            node_map.emplace(child.GetPath(),
                unGraphData().createNode(parentit->second,
                    child.GetName().GetText(),
                    type_name,
                    HUSD_SHADER_GRAPH_CATEGORY,
                    signature));
            continue;
        }

        UsdShadeNodeGraph childnodegraph(child);
        if (childnodegraph)
        {
            auto node_id = unGraphData().createNode(parentit->second,
                child.GetName().GetText(),
                HUSD_NODEGRAPH_NODE_TYPE,
                HUSD_SHADER_GRAPH_CATEGORY);
            node_map.emplace(child.GetPath(), node_id);
            create_ports_fn(child, node_id);
            continue;
        }

        UsdUIBackdrop backdrop(child);
        if (backdrop)
        {
            backdrop_map.emplace(child.GetPath(),
                unGraphData().createStickyNote(parentit->second,
                    child.GetName().GetText()));
            continue;
        }
    }

    // Configure shaders nodes and connect them together.
    for (auto &&it : node_map)
    {
        // Record this node in our reverse lookup map.
        myNodeIdToPathMap.emplace(it.second, it.first);
        UsdPrim prim(stage->GetPrimAtPath(it.first));
        UT_Vector2D pos;
        GfVec2f gfpos{0.0f, 0.0f};
        UT_Color clr;
        GfVec3f gfclr{0.0f, 0.0f, 0.0f};
        TfToken token;

        UsdUINodeGraphNodeAPI nodeapi(prim);
        if (nodeapi)
        {
            if (nodeapi.GetPosAttr().HasAuthoredValue())
            {
                nodeapi.GetPosAttr().Get(&gfpos);
                pos = GusdUT_Gf::Cast(gfpos);
                unGraphData().nodeData().setPosition(it.second, pos);
            }
            if (nodeapi.GetDisplayColorAttr().HasAuthoredValue())
            {
                nodeapi.GetDisplayColorAttr().Get(&gfclr);
                clr.setRGB(gfclr[0], gfclr[1], gfclr[2]);
                unGraphData().nodeData().setColor(it.second, clr);
            }
        }

        VtValue comment = prim.GetCustomDataByKey(TfToken(
            HUSD_Constants::getCommentCustomDataName().toStdString()));
        if (comment.IsHolding<std::string>())
        {
            unGraphData().nodeData().setComment(it.second,
                comment.Get<std::string>());
        }

        VtValue tags = prim.GetCustomDataByKey(
            TfToken(HUSD_Constants::getTagsCustomDataName().toStdString()));
        if (tags.IsHolding<VtArray<std::string>>())
        {
            VtArray<std::string> vttags = tags.Get<VtArray<std::string>>();
            UT_StringArray uttags;
            for (auto &&tag : vttags)
                uttags.append(tag);
            unGraphData().nodeData().setTags(it.second, uttags);
        }

        // Copy container connection data onto the UN_Graph.
        UsdHoudiniHoudiniNodeGraphContainerAPI containerapi(prim);
        if (containerapi)
        {
            if (it.second == unGraphData().rootNode() &&
                containerapi.GetContainerWireStyleAttr().HasAuthoredValue())
            {
                containerapi.GetContainerWireStyleAttr().Get(&token);
                auto wirestyle = husdConvertVtValueToWireStyle(token);
                if (wirestyle)
                    UN_UniGraph::setWireStyle(*wirestyle);
            }
            if (containerapi.GetContainerInputPosAttr().HasAuthoredValue())
            {
                containerapi.GetContainerInputPosAttr().Get(&gfpos);
                pos = GusdUT_Gf::Cast(gfpos);
                UN_UniGraph::setContainerConnectPosition(it.second,
                    UNI_ContainerConnectType::INPUTS, pos);
            }
            if (containerapi.GetContainerInputDisplayColorAttr().HasAuthoredValue())
            {
                containerapi.GetContainerInputDisplayColorAttr().Get(&gfclr);
                clr.setRGB(gfclr[0], gfclr[1], gfclr[2]);
                UN_UniGraph::setContainerConnectColor(it.second,
                    UNI_ContainerConnectType::INPUTS, clr);
            }
            if (containerapi.GetContainerOutputPosAttr().HasAuthoredValue())
            {
                containerapi.GetContainerOutputPosAttr().Get(&gfpos);
                pos = GusdUT_Gf::Cast(gfpos);
                UN_UniGraph::setContainerConnectPosition(it.second,
                    UNI_ContainerConnectType::OUTPUTS, pos);
            }
            if (containerapi.GetContainerOutputDisplayColorAttr().HasAuthoredValue())
            {
                containerapi.GetContainerOutputDisplayColorAttr().Get(&gfclr);
                clr.setRGB(gfclr[0], gfclr[1], gfclr[2]);
                UN_UniGraph::setContainerConnectColor(it.second,
                    UNI_ContainerConnectType::OUTPUTS, clr);
            }
        }

        for (auto &&attrib : prim.GetAttributes())
        {
            SdfPathVector sources;

            attrib.GetConnections(&sources);
            for (auto &&source : sources)
            {
                SdfPath sourceprim = source.GetPrimPath();
                UsdAttribute sourceattr = stage->GetAttributeAtPath(source);
                if (!sourceattr)
                {
                    UT_ASSERT(!"Connection to non-existent attribute");
                    continue;
                }
                auto sourceit = node_map.find(sourceprim);
                if (sourceit == node_map.end())
                {
                    UT_ASSERT(!"Connection to node not in the material");
                    continue;
                }

                // Get the source port name, removing any directional prefix.
                std::string sourceportname;
                UN_PortKind sourceportkind;
                husdGetPortInfo(source.GetNameToken(),
                    &sourceportname, &sourceportkind);
                // Get the dest port name, removing any directional prefix.
                std::string destportname;
                UN_PortKind destportkind;
                husdGetPortInfo(attrib.GetName(),
                    &destportname, &destportkind);

                // If we can't get port info from the source or dest string,
                // don't try to connect the port.
                if (sourceportkind == UN_PortKind::Invalid ||
                    destportkind == UN_PortKind::Invalid)
                {
                    UT_ASSERT(!"Connection to bad port name");
                    continue;
                }

                husdCreateAndConnectPorts(unGraphData(),
                    sourceit->second, sourceportkind, sourceportname,
                    sourceattr.GetTypeName().GetAsToken().GetString(),
                    it.second, destportkind, destportname,
                    attrib.GetTypeName().GetAsToken().GetString());
            }
        }
    }

    // Configure sticky notes.
    for (auto &&it : backdrop_map)
    {
        // Record this node in our reverse lookup map.
        myStickyNoteIdToPathMap.emplace(it.second, it.first);
        UsdPrim prim(stage->GetPrimAtPath(it.first));

        UsdUIBackdrop backdrop(prim);
        if (backdrop)
        {
            if (backdrop.GetDescriptionAttr().HasAuthoredValue())
            {
                std::string description;
                backdrop.GetDescriptionAttr().Get(&description);
                unGraphData().stickyNoteData().setText(it.second, description);
            }
        }

        UsdUINodeGraphNodeAPI nodeapi(prim);
        if (nodeapi)
        {
            if (nodeapi.GetPosAttr().HasAuthoredValue())
            {
                UT_Vector2D pos;
                GfVec2f gfpos{0.0f, 0.0f};
                nodeapi.GetPosAttr().Get(&gfpos);
                pos = GusdUT_Gf::Cast(gfpos);
                unGraphData().stickyNoteData().setPosition(it.second, pos);
            }
            if (nodeapi.GetSizeAttr().HasAuthoredValue())
            {
                UT_Vector2D size;
                GfVec2f gfsize{0.0f, 0.0f};
                nodeapi.GetSizeAttr().Get(&gfsize);
                size = GusdUT_Gf::Cast(gfsize);
                unGraphData().stickyNoteData().setDimensions(it.second, size);
            }
            if (nodeapi.GetDisplayColorAttr().HasAuthoredValue())
            {
                UT_Color clr;
                GfVec3f gfclr{0.0f, 0.0f, 0.0f};
                nodeapi.GetDisplayColorAttr().Get(&gfclr);
                clr.setRGB(gfclr[0], gfclr[1], gfclr[2]);
                unGraphData().stickyNoteData().setColor(it.second, clr);
            }
        }
    }
}
