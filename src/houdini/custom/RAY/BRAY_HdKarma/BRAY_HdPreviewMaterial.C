/*
 * PROPRIETARY INFORMATION.  This software is proprietary to
 * Side Effects Software Inc., and is not to be reproduced,
 * transmitted, or disclosed in any way without written permission.
 *
 * NAME:	BRAY_HdPreviewMaterial.h
 *
 * COMMENTS: Conversion of USD Preview Material to VEX
 */

#include "BRAY_HdPreviewMaterial.h"
#include "BRAY_HdUtil.h"

#include <HUSD/XUSD_Format.h>
#include <HUSD/XUSD_Tokens.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

#include <UT/UT_Debug.h>
#include <UT/UT_Lock.h>
#include <UT/UT_Set.h>
#include <UT/UT_VarScan.h>
#include <UT/UT_StringMap.h>
#include <UT/UT_StringSet.h>
#include <UT/UT_VarEncode.h>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{
    static UT_Lock		theLock;

    struct USDtoVEXType
    {
	const char	*myUSD;
	const char	*myVEX;
	const char	*myDefault;
    };

    static USDtoVEXType	thePrimvarTypeMap[] =
    {
	{ "int",	"int",		"0" },
	{ "float",	"float",	"0" },
	{ "float2",	"vector2",	"0" },
	{ "float3",	"vector",	"0" },
	{ "float4",	"vector4",	"0" },
	{ "string",	"string",	"''" },
	{ "normal",	"vector",	"0" },
	{ "point",	"vector",	"0" },
	{ "matrix",	"matrix",	"1" },
    };
}

#if 1
    static constexpr UT_StringLit	theSpecularBSDF("ggx");
#else
    static constexpr UT_StringLit	theSpecularBSDF("phong");
#endif

static constexpr UT_StringLit	theNodeIdString("NODE_ID");
static constexpr UT_StringLit	theNodePathString("NODE_PATH");
static constexpr UT_StringLit	theBSDFVar("SPECULAR_BSDF");
static constexpr UT_StringLit	theSTString("st");
static constexpr UT_StringLit	theSTDefault("set(s,t)");
static constexpr UT_StringLit	thePrimVarReaderPrefix("UsdPrimvarReader_");
static const TfToken		theFallbackToken("fallback", TfToken::Immortal);
static const TfToken		theDefaultToken("default", TfToken::Immortal);
static const TfToken		theSTToken("st", TfToken::Immortal);
static const TfToken		theUVTextureToken("UsdUVTexture",
					TfToken::Immortal);
static const TfToken		thePreviewSurfaceToken("UsdPreviewSurface",
					TfToken::Immortal);
static const TfToken		theTransform2dToken("UsdTransform2d",
					TfToken::Immortal);

static constexpr UT_StringLit	theTextureDefs[] = {
    UT_StringLit("file"),	UT_StringLit("''"),
    UT_StringLit("st"),		UT_StringLit("set(s,t)"),
    UT_StringLit("wrapS"),	UT_StringLit("'repeat'"),
    UT_StringLit("wrapT"),	UT_StringLit("'repeat'"),
    UT_StringLit("fallback"),	UT_StringLit("{0,0,0,1}"),
    UT_StringLit("scale"),	UT_StringLit("1"),
    UT_StringLit("bias"),	UT_StringLit("0"),
};
static constexpr UT_StringLit	theSurfaceDefs[] = {
    UT_StringLit("diffuseColor"),	UT_StringLit("{0.18,0.18,0.18}"),
    UT_StringLit("specularColor"),	UT_StringLit("{0,0,0}"),
    UT_StringLit("emissiveColor"),	UT_StringLit("{0,0,0}"),
    UT_StringLit("useSpecularWorkflow"),	UT_StringLit("0"),
    UT_StringLit("roughness"),		UT_StringLit("0.5"),
    UT_StringLit("clearcoat"),		UT_StringLit("0"),
    UT_StringLit("clearcoatRoughness"),	UT_StringLit("0.01"),
    UT_StringLit("opacity"),		UT_StringLit("1"),
    UT_StringLit("ior"),		UT_StringLit("1.5"),
    UT_StringLit("normal"),		UT_StringLit("{0,0,1}"),
    UT_StringLit("displacement"),	UT_StringLit("0"),
    UT_StringLit("occlusion"),		UT_StringLit("1"),
    UT_StringLit("metallic"),		UT_StringLit("0"),
};
static constexpr UT_StringLit	theTransform2dDefs[] = {
    UT_StringLit("in"),			UT_StringLit("{0,0}"),
    UT_StringLit("pivot"),		UT_StringLit("{.5,.5}"),
    UT_StringLit("rotation"),		UT_StringLit("0"),
    UT_StringLit("scale"),		UT_StringLit("{1,1}"),
    UT_StringLit("translation"),	UT_StringLit("{0,0}"),
};

// The NodeInfo class stores the node's offset in the network's node vector.
// The processed boolean is to ensure the node is only processed one time
// during traversal.
struct NodeInfo
{
    NodeInfo(int idx = -1)
	: myIndex(idx)
	, myProcessed(false)
    {
    }
    int		myIndex;
    bool	myProcessed;
};

// The usd_NodeMap stores a mapping from the node's path as key to its index in
// the networks node vector.  This allows us to look up a node in the vector
// given its path.
using usd_NodeMap = UT_Map<SdfPath, NodeInfo>;

// The usd_RelationMap stores mapping from a node's path as key to a set of all
// the relationship nodes that provide input wires to any of its parameters.
// From the relationship node, we can find the input node (using the
// usd_NodeMap).
using usd_RelationMap = UT_Map<SdfPath, UT_Set<int>>;
using usd_VariableMap = UT_StringMap<UT_StringHolder>; 

static const char	*theCommonHeader = "#include <usd_preview.h>\n";

static const char	*theUVTextureCode = R"VEX_CODE(
    // Generator: $NODE_PATH
    vector4 rgba_$NODE_ID = usd_texture($file, $st,
				$wrapS, $wrapT,
				$fallback, $bias, $scale);
    vector rgb_$NODE_ID = vector(rgba_$NODE_ID);
    float r_$NODE_ID = rgba_$NODE_ID.r;
    float g_$NODE_ID = rgba_$NODE_ID.g;
    float b_$NODE_ID = rgba_$NODE_ID.b;
    float a_$NODE_ID = rgba_$NODE_ID.a;
)VEX_CODE";

static const char	*theDisplaceCode = R"VEX_CODE(
    vector	nn = normalize(N);	// Normal in tangent space
    P += $displacement * nn;
    N = computenormal(P, nn, Ng);
)VEX_CODE";

static const char	*theTransform2dCode = R"VEX_CODE(
    // Generator: $NODE_PATH
    vector2 result_$NODE_ID = usd_rotate(($in - $pivot) * $scale, $rotation)
				+ $pivot + $translation;
)VEX_CODE";

static const char	*theSurfaceCode = R"VEX_CODE(
    // Preview Surface: $NODE_PATH
    {
	// Initialize the variables required to compute the BSDF
	vector	tN = normalize(N);
	vector	tNg = normalize(Ng);
	vector	tI = normalize(I);
	vector	tanu = normalize(dPds);
	vector	tanv = normalize(dPdt);

	float	energy = 0;
	vector	F0 = $specularColor;
	vector	F90 = 1;

	if (!$useSpecularWorkflow)
	{
	    float R = (1 - $ior) / (1 + $ior);
	    vector spec = lerp({1,1,1}, $diffuseColor, $metallic);
	    F0 = R * R * spec;
	    F90 = spec;
	}

	F = bsdf();

	// Specular lobe
	if (max(F0) > 0)
	{
	    F += get_bsdf("$SPECULAR_BSDF",
		    tNg, tN, tI, tanu, tanv,
		    F0,
		    F90,
		    0.67,	// eta
		    $roughness,	// roughness
		    0,		// aniso
		    0,		// anisodir
		    1,		// masking
		    0,		// thinfilm
		    1,		// fresblend
		    1,		// reflect
		    0,		// refract
		    0,		// dispersion
		    energy,
		    "reflect",
		    "refract");
	}

	// Clearcoat
	if ($clearcoat > 0)
	{
	    // We don't want to take clearcoat into energy conservation
	    float	tmp_energy;
	    F += $clearcoat * get_bsdf("$SPECULAR_BSDF",
		    tNg, tN, tI, tanu, tanv,
		    1.0,	// F0
		    1.0,	// F90
		    0.67,	// eta
		    $clearcoatRoughness,	// roughness
		    0,		// aniso
		    0,		// anisodir
		    1,		// masking
		    0,		// thinfilm
		    1,		// fresblend
		    1,		// reflect
		    0,		// refract
		    0,		// dispersion
		    tmp_energy,
		    "reflect",
		    "refract");
	}
	F += (1 - energy) * (1 - $metallic) * $diffuseColor * diffuse() * 2.0;
	Of = $opacity;
	Ce = $emissiveColor;
    }
)VEX_CODE";

static UT_StringHolder
defineParameter(const HdMaterialNode &node, int node_id, UT_WorkBuffer &code,
	const char *identifier)
{
    auto &&nparm = node.parameters.find(HusdHdMaterialTokens()->varname);
    auto &&fparm = node.parameters.find(theFallbackToken);
    auto &&dparm = node.parameters.find(theDefaultToken);

    UT_ASSERT(nparm != node.parameters.end());

    UT_WorkBuffer	 valbuf;
    UT_WorkBuffer	 decl;
    const VtValue	&name = nparm->second;
    const char		*vextype = nullptr;

    if (fparm != node.parameters.end())
	vextype = BRAY_HdUtil::valueToVex(valbuf, fparm->second);
    else if (dparm != node.parameters.end())
	vextype = BRAY_HdUtil::valueToVex(valbuf, dparm->second);
    else
    {
	// No default or fallback value was specified, so we assume that it's a
	// simple default (like 0 or "")
	// Grab the type from the identifier
	UT_ASSERT(strlen(identifier) > thePrimVarReaderPrefix.length());
	const char *type = identifier + thePrimVarReaderPrefix.length();
	for (auto &&item : thePrimvarTypeMap)
	{
	    if (!strcmp(type, item.myUSD))
	    {
		vextype = item.myVEX;
		valbuf.strcpy(item.myDefault);
	    }
	}
	if (!vextype)
	{
	    UT_ASSERT(0 && "Unknown type");
	    return UT_StringHolder::theEmptyString;
	}
    }

    decl.sprintf("%s %s = ", vextype, name.Get<TfToken>().GetText());
    decl.append(valbuf);
    code.sprintf("    %s\tresult_%d = %s;\n",
	    vextype,
	    node_id,
	    name.Get<TfToken>().GetText());
    //code.appendSprintf("printf('%%g\\n', result_%d);", node_id);

    return UT_StringHolder(decl);
}

static const char *
expandVariable(const char *name, void *userdata)
{
    const usd_VariableMap *map = (const usd_VariableMap *)(userdata);
    auto it = map->find(name);
    UT_ASSERT(it != map->end());
    return it->second.c_str();
}

static bool
displaceHasWires(const HdMaterialNode &node, const usd_VariableMap &map)
{
    // If there's a wire, then the parameter map on the node won't have the
    // corresponding input.
    auto &&dname = node.parameters.find(HusdHdMaterialTokens()->displacement);

    // If the parameters don't show up in the parameters map, there's a wire
    if (dname == node.parameters.end())
	return true;

    // We know there's a parameter for displacement - but the value associated
    // with the parameter might not be 0, so there might be a fixed amount of
    // displacement.
    const VtValue	&dval = dname->second;
    UT_ASSERT(dval.IsHolding<float>());
    if (dval.IsHolding<float>() && dval.Get<float>() != 0)
	return true;

    return false;
}

static void
addMissingVars(usd_VariableMap &vars, const UT_StringLit *defs, size_t n)
{
    // Fix up missing variables
    for (size_t i = 0; i < n; i += 2)
    {
	if (vars.count(defs[i].asRef()) == 0)
	    vars[defs[i].asHolder()] = defs[i+1].asHolder();
    }
}

static void
fixST(const HdMaterialNode &node, usd_VariableMap &vars)
{
    // If the st parameter doesn't exist in the list, then there's an input wire
    if (node.parameters.count(theSTToken) == 0)
	return;
    // Otherwise, we should default "st" to the parametric coordinates
    vars[theSTString.asHolder()] = theSTDefault.asHolder();
}

static void
fixFilePaths(usd_VariableMap& vars)
{
    UT_Lock::Scope  lock(theLock);
    auto pathIt = vars.find("file");
    if (pathIt != vars.end())
    {
	// Search for @assetname.usdz[texturefilename.png/jpg] path
	if (pathIt->second.contains(".usdz"))
	{
	    // in case of windows style paths having backward slashes
	    // fix them to have forward slashes so that when vex tries
	    // to look up for them in its cache, the paths are proper
	    auto& val = pathIt->second;
	    val.substitute("\\", "/");
	}
    }
    else
    {
	UT_ASSERT(0 && "variable map doesn't have the file field");
    }
}

static bool
process(UT_WorkBuffer &code,
	UT_StringSet &vexparms,
	const HdMaterialNetwork &net,
	int curr,
	usd_NodeMap &nodes,
	const usd_RelationMap &wires,
	bool for_displace)
{
    const HdMaterialNode	&node = net.nodes[curr];
    NodeInfo			&info = nodes[node.path];
    if (info.myProcessed)
	return true;
    info.myProcessed = true;

    usd_VariableMap	vars;
    UT_WorkBuffer	tmp;

    // Process the input wires
    auto	inputs = wires.find(node.path);
    if (inputs != wires.end())
    {
	for (int ridx : (*inputs).second)
	{
	    auto &&r = net.relationships[ridx];
	    auto &&inputnode = nodes.find(r.inputId);
	    UT_ASSERT(inputnode != nodes.end());
	    int	inputidx = inputnode->second.myIndex;

	    if (!process(code, vexparms, net, inputidx,
			nodes, wires, for_displace))
	    {
		return false;
	    }

	    // Create the name of the output wire and store the input name as a
	    // variable for expansion.
	    tmp.sprintf("%s_%d", r.inputName.GetText(), inputidx);
	    vars[r.outputName.GetText()] = UT_StringHolder(tmp);
	}
    }

    // Create variables for all the parameters, storing their value
    for (auto &&p : node.parameters)
    {
	tmp.clear();
	BRAY_HdUtil::valueToVex(tmp, p.second);
	vars[p.first.GetText()] = UT_StringHolder(tmp);
    }

    // Stash the node index and path as variables in the map too
    tmp.sprintf("%d", curr);
    vars[theNodeIdString.asHolder()] = UT_StringHolder(tmp);
    vars[theNodePathString.asHolder()] = UT_StringHolder(node.path.GetText());
    vars[theBSDFVar.asHolder()] = theSpecularBSDF.asHolder();

#if 0
    UTdebugFormat("PROCESS: {} {}", node.path, node.identifier);
    UTdebugFormat("Variables:");
    for (auto &&item : vars)
	UTdebugFormat("   '{}' : '{}'", item.first, item.second);
#endif

    UT_String		ident(node.identifier.GetText());
    UT_WorkBuffer	expanded;
    if (ident.startsWith(thePrimVarReaderPrefix.c_str(), true,
		thePrimVarReaderPrefix.length()))
    {
	UT_StringHolder	parm = defineParameter(node, curr, expanded, ident);
	if (parm)
	{
	    vexparms.insert(parm);
	    code.append(expanded);
	}
    }
    else if (node.identifier == theUVTextureToken)
    {
	// Expand the texture code
	addMissingVars(vars, theTextureDefs, SYScountof(theTextureDefs));
	fixST(node, vars);
	fixFilePaths(vars);
	UTVariableScan(expanded, theUVTextureCode, expandVariable, &vars);
	code.append(expanded);
    }
    else if (node.identifier == thePreviewSurfaceToken)
    {
	// Expand the surface shader code
	addMissingVars(vars, theSurfaceDefs, SYScountof(theSurfaceDefs));
	if (for_displace)
	{
	    // Check if we're building the displacement shader, but neither the
	    // displace nor the normal values have any effect on the shader.
	    if (!displaceHasWires(node, vars))
		return false;
	    UTVariableScan(expanded, theDisplaceCode, expandVariable,&vars);
	}
	else
	{
	    // TODO: Need to process tangent space normal maps in surface
	    UTVariableScan(expanded, theSurfaceCode, expandVariable, &vars);
	}
	code.append(expanded);
    }
    else if (node.identifier == theTransform2dToken)
    {
	addMissingVars(vars, theTransform2dDefs,
		SYScountof(theTransform2dDefs));
	UTVariableScan(expanded, theTransform2dCode, expandVariable, &vars);
	code.append(expanded);
    }
    else
    {
	UTdebugFormat("Unhandled Node Type: {}", node.identifier);
	UT_ASSERT(0 && "Unhandled node type");
	return false;
    }
    return true;
}

static void
declareShader(UT_WorkBuffer &code, const char *vex_context,
	const std::string &path)
{
    UT_StringHolder	safepath;
    safepath = UT_VarEncode::encodeVar(UT_StringRef(path));
    code.appendSprintf("%s\n_%s_%s(",
	    vex_context,
	    safepath.buffer(),
	    vex_context);
}

static void
declareParameters(UT_WorkBuffer &code, const UT_StringSet &vexparms)
{
    for (auto &&p : vexparms)
	code.appendSprintf("\n\t%s;", p.c_str());
}

static UT_StringHolder
makeSurface(const HdMaterialNode &node,
	const UT_StringSet &vexparms,
	const char *statements)
{
    UT_WorkBuffer	code;

    code.strcpy(theCommonHeader);
    declareShader(code, "surface", node.path.GetString());
    code.append("\n\texport vector Ce=0;");
    declareParameters(code, vexparms);
    code.appendSprintf(")\n{\n%s\n}", statements);

    // dump code
    //UTdebugFormat("Generated Code: '''{}'''", code);
    return UT_StringHolder(code);
}

static UT_StringHolder
makeDisplace(const HdMaterialNode &node,
	const UT_StringSet &vexparms,
	const char *statements)
{
    UT_WorkBuffer	code;

    code.strcpy(theCommonHeader);
    declareShader(code, "displacement", node.path.GetString());
    declareParameters(code, vexparms);

    code.appendSprintf(")\n{\n%s\n}", statements);

    //UTdebugFormat("Generated Displacement: '''{}'''", code);
    return UT_StringHolder(code);
}

UT_StringHolder
BRAY_HdPreviewMaterial::convert(const HdMaterialNetwork &net, ShaderType type)
{
    usd_NodeMap		  nodes;
    usd_RelationMap	  wires;
    int			  output = -1;
    auto		&&mtokens = HusdHdMaterialTokens();

    // Build a map so we can look up the nodes based on their path
    for (int i = 0, n = net.nodes.size(); i < n; ++i)
    {
	const HdMaterialNode &node = net.nodes[i];
	UT_ASSERT(nodes.find(node.path) == nodes.end());
	nodes[node.path] = NodeInfo(i);
	if (output < 0)
	{
	    if (node.identifier == mtokens->usdPreviewMaterial)
		output = i;
	}
	else
	{
	    // Can't have more than one preview material node
	    UT_ASSERT(node.identifier != mtokens->usdPreviewMaterial);
	}
    }
    if (output < 0)
    {
	// In this case, there's no preview material (but there may be other
	// representations).
	return UT_StringHolder::theEmptyString;
    }

    // For each node, create a list of all the nodes needed for input
    // This allows us to look up nodes (and relationship nodes) while parsing
    for (int i = 0, n = net.relationships.size(); i < n; ++i)
    {
	const HdMaterialRelationship	&r = net.relationships[i];
	wires[r.outputId].insert(i);
    }

    UT_WorkBuffer	code;
    UT_StringSet	vexparms;

    if (!process(code, vexparms, net, output, nodes, wires, type == DISPLACE))
	return UT_StringHolder::theEmptyString;

    switch (type)
    {
	case SURFACE:
	    return makeSurface(net.nodes[output], vexparms, code.buffer());
	case DISPLACE:
	    return makeDisplace(net.nodes[output], vexparms, code.buffer());
	    break;
    }
    return UT_StringHolder::theEmptyString;
}

PXR_NAMESPACE_CLOSE_SCOPE
