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

#include "SOP_CustomTraversal.h"

#include "gusd/GU_USD.h"
#include "gusd/GU_PackedUSD.h"
#include "gusd/USD_ThreadedTraverse.h"
#include "gusd/USD_Utils.h"
#include "gusd/UT_Assert.h"
#include "gusd/UT_StaticInit.h"

#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/fileUtils.h>
#include "pxr/base/plug/registry.h"
#include "pxr/usd/kind/registry.h"
#include "pxr/usd/usd/modelAPI.h"

#include <HUSD/HUSD_API.h>
#include <OP/OP_Parameters.h>
#include <PRM/PRM_AutoDeleter.h>
#include <PRM/PRM_ChoiceList.h>
#include <PRM/PRM_Conditional.h>
#include <PRM/PRM_Default.h>
#include <PRM/PRM_Shared.h>
#include <PRM/PRM_Template.h>
#include <UT/UT_WorkArgs.h>
#include <UT/UT_StringMMPattern.h>
#include <UT/UT_UniquePtr.h>
#include <PY/PY_Python.h>

#include BOOST_HEADER(function.hpp)

PXR_NAMESPACE_OPEN_SCOPE

namespace {

/** A traversal implementation offering users full configuration
    over many aspects of traversal.*/
class GusdUSD_CustomTraverse : public GusdUSD_Traverse
{
public:
    enum TriState
    {
        TRUE_STATE,
        FALSE_STATE,
        ANY_STATE
    };

    struct Opts : public GusdUSD_Traverse::Opts
    {
        Opts() : GusdUSD_Traverse::Opts() {}
        ~Opts() override {}

        void                    Reset() override;
        bool                    Configure(
            OP_Parameters& parms, fpreal t) override;

        /** Methods for matching components by wildcard pattern.
            Note that for all methods, an empty pattern is treated
            as equivalent to '*'. I.e., an empty pattern matches everything.
            @{ */
        bool                    SetKindsByPattern(const char* pattern,
            bool caseSensitive=true,
            std::string* err=nullptr);

        bool                    SetPurposesByPattern(const char* pattern,
            bool caseSensitive=true,
            std::string* err=nullptr);

        bool                    SetTypesByPattern(const char* pattern,
            bool caseSensitive=true,
            std::string* err=nullptr);

        void                    SetNamePattern(const char* pattern,
            bool caseSensitive=true);

        void                    SetPathPattern(const char* pattern,
            bool caseSensitive=true);
        /** @} */

        /** Create a predicate matching all of the configurable
            options that refer to prim flags.*/
        Usd_PrimFlagsPredicate  MakePredicate() const;


        TriState            active, visible, imageable, defined, abstract,
            model, group, instance, prototype, clips;
        bool                traverseMatched;
        UT_Array<TfToken>   purposes, kinds;
        UT_Array<TfType>    types;
        UT_StringMMPattern  namePattern, pathPattern;
    };

    Opts*           CreateOpts() const override  { return new Opts; }

    bool            FindPrims(const UsdPrim& root,
        UsdTimeCode time,
        GusdPurposeSet purposes,
        UT_Array<UsdPrim>& prims,
        bool skipRoot=true,
        const GusdUSD_Traverse::Opts* opts=nullptr
    ) const override;

    bool            FindPrims(const UT_Array<UsdPrim>& roots,
        const GusdDefaultArray<UsdTimeCode>& times,
        const GusdDefaultArray<GusdPurposeSet>& purposes,
        UT_Array<PrimIndexPair>& prims,
        bool skipRoot=true,
        const GusdUSD_Traverse::Opts* opts=nullptr
    ) const override;

    static void     Initialize();
};

void
GusdUSD_CustomTraverse::Opts::Reset()
{
    defined = TRUE_STATE;
    abstract = FALSE_STATE;
    active = TRUE_STATE;
    visible = TRUE_STATE;
    imageable = TRUE_STATE;
    model = group = instance = prototype = clips = ANY_STATE;

    traverseMatched = false;
    kinds.clear();
    purposes.clear();
    types.clear();
}

void _PredicateSwitch(Usd_PrimFlagsConjunction& p,
    GusdUSD_CustomTraverse::TriState state,
    Usd_PrimFlags flag)
{
    switch(state) {
    case GusdUSD_CustomTraverse::TRUE_STATE:  p &= flag; break;
    case GusdUSD_CustomTraverse::FALSE_STATE: p &= !flag; break;
    default: break;
    }
}

Usd_PrimFlagsPredicate
GusdUSD_CustomTraverse::Opts::MakePredicate() const
{
    // Build a predicate from the user-configured options.

    /* Note that we *intentionally* exclude load state from being
       user-configurable, since traversers are primarily intended to
       be used on pure, read-only caches, in which case users aren't meant
       to know about prim load states.

       We also don't default add UsdPrimIsLoaded at all to the predicate,
       as that prevents users from traversing to inactive prims, since
       if a prim carrying payloads has been deactivated, the prim will
       be considered both inactive and unloaded.*/

    Usd_PrimFlagsConjunction p;

    _PredicateSwitch(p, active,     UsdPrimIsActive);
    _PredicateSwitch(p, model,      UsdPrimIsModel);
    _PredicateSwitch(p, group,      UsdPrimIsGroup);
    _PredicateSwitch(p, defined,    UsdPrimIsDefined);
    _PredicateSwitch(p, abstract,   UsdPrimIsAbstract);
    _PredicateSwitch(p, instance,   UsdPrimIsInstance);
    _PredicateSwitch(p, prototype,  Usd_PrimPrototypeFlag);
    _PredicateSwitch(p, clips,      Usd_PrimClipsFlag);
    return p;
}


bool
GusdUSD_CustomTraverse::Opts::Configure(OP_Parameters& parms, fpreal t)
{
#define _EVALTRI(NAME) \
    NAME = static_cast<TriState>(parms.evalInt(#NAME, 0, t));

    _EVALTRI(active);
    _EVALTRI(visible);
    _EVALTRI(imageable);
    _EVALTRI(defined);
    _EVALTRI(abstract);
    _EVALTRI(model);
    _EVALTRI(group);
    _EVALTRI(instance);
    _EVALTRI(clips);

    // The parameter "master" was renamed to "prototype", but for backward
    // compatibility, we will accept either one ("prototype" takes priority
    // if they both exist).
    if (parms.getParmPtr("prototype"))
        prototype = static_cast<TriState>(parms.evalInt("prototype", 0, t));
    else
        prototype = static_cast<TriState>(parms.evalInt("master", 0, t));

    traverseMatched = parms.evalInt("traversematched", 0, t);

#define _EVALSTR(NAME,VAR) \
    parms.evalString(VAR, #NAME, 0, t);

    UT_String kindsStr, purposesStr, typesStr;
    _EVALSTR(kinds, kindsStr);
    _EVALSTR(purposes, purposesStr);
    _EVALSTR(types, typesStr);

    std::string err;
    if(!SetKindsByPattern(kindsStr, true, &err) ||
        !SetPurposesByPattern(purposesStr, true, &err) ||
        !SetTypesByPattern(typesStr, true, &err)) {

        parms.opLocalError(OP_ERR_ANYTHING, err.c_str());
        return false;
    }

    UT_String namePatternStr, pathPatternStr;
    _EVALSTR(namemask, namePatternStr);
    _EVALSTR(pathmask, pathPatternStr);

    SetNamePattern(namePatternStr);
    SetPathPattern(pathPatternStr);

    if(kinds.size() > 0 && model == FALSE_STATE) {
        parms.opLocalError(OP_ERR_ANYTHING,
            "Model kinds specified, but models "
            "are being excluded. Matches are impossible.");
        return false;
    }
    return true;
}

void
_BadPatternError(const char* type,
    const char* pattern,
    std::string* err=NULL)
{
    if(err) {
        UT_WorkBuffer buf;
        buf.sprintf("No %s matched pattern '%s'", type, pattern);
        *err = buf.toStdString();
    }
}

void
_SetPattern(UT_StringMMPattern& patternObj,
    const char* pattern,
    bool caseSensitive)
{
    if(!UTisstring(pattern) || UT_String(pattern) == "*") {
        patternObj.clear();
    } else {
        patternObj.compile(pattern, caseSensitive);
    }
}

bool
GusdUSD_CustomTraverse::Opts::SetKindsByPattern(
    const char* pattern, bool caseSensitive, std::string* err)
{
    if(!UTisstring(pattern) || UT_String(pattern) == "*") {
        kinds.clear();
        return true;
    }
    GusdUSD_Utils::GetBaseModelKindsMatchingPattern(
        pattern, kinds, caseSensitive);
    if(kinds.size() == 0) {
        _BadPatternError("model kinds", pattern, err);
        return false;
    }
    return true;
}


bool
GusdUSD_CustomTraverse::Opts::SetPurposesByPattern(
    const char* pattern, bool caseSensitive, std::string* err)
{
    if(!UTisstring(pattern) || UT_String(pattern) == "*") {
        // Empty pattern means 'match everything'
        purposes.clear();
        return true;
    }
    GusdUSD_Utils::GetPurposesMatchingPattern(pattern, purposes, caseSensitive);
    if(purposes.size() == 0) {
        _BadPatternError("purposes", pattern, err);
        return false;
    }
    return true;
}


bool
GusdUSD_CustomTraverse::Opts::SetTypesByPattern(
    const char* pattern, bool caseSensitive, std::string* err)
{
    if(!UTisstring(pattern) || UT_String(pattern) == "*") {
        // Empty pattern means 'match everything'
        types.clear();
        return true;
    }
    GusdUSD_Utils::GetBaseSchemaTypesMatchingPattern(pattern, types,
        caseSensitive);
    if(types.size() == 0) {
        _BadPatternError("prim schema types", pattern, err);
        return false;
    }
    return true;
}


void
GusdUSD_CustomTraverse::Opts::SetNamePattern(
    const char* pattern, bool caseSensitive)
{
    _SetPattern(namePattern, pattern, caseSensitive);
}


void
GusdUSD_CustomTraverse::Opts::SetPathPattern(
    const char* pattern, bool caseSensitive)
{
    _SetPattern(pathPattern, pattern, caseSensitive);
}

struct _Visitor
{
    _Visitor(const GusdUSD_CustomTraverse::Opts& opts)
        : _opts(opts),
        _predicate(opts.MakePredicate()) {}

    Usd_PrimFlagsPredicate  TraversalPredicate(bool allow_abstract) const
    {
        // Need a predicate matching all prims.
        return Usd_PrimFlagsPredicate::Tautology();
    }

    bool                    AcceptPrim(const UsdPrim& prim,
        UsdTimeCode time,
        GusdPurposeSet purposes,
        GusdUSD_TraverseControl& ctl);

    bool                    AcceptType(const UsdPrim& prim) const;

    bool                    AcceptPurpose(const UsdGeomImageable& prim,
        GusdUSD_TraverseControl& ctl);

    bool                    AcceptKind(const UsdPrim& prim) const;

    bool                    AcceptVis(const UsdGeomImageable& prim,
        UsdTimeCode time,
        GusdUSD_TraverseControl& ctl);

    bool                    AcceptNamePattern(const UsdPrim& prim) const;

    bool                    AcceptPathPattern(const UsdPrim& prim) const;

private:
    const GusdUSD_CustomTraverse::Opts& _opts;
    const Usd_PrimFlagsPredicate        _predicate;
    TfToken _vis, _purpose;
};


bool
_Visitor::AcceptPrim(const UsdPrim& prim,
    UsdTimeCode time,
    GusdPurposeSet purposes,
    GusdUSD_TraverseControl& ctl)
{
    UsdGeomImageable ip(prim);

    bool visit = true;

    if(ARCH_UNLIKELY(!(bool)ip)) {
        // Prim is not imageable
        if(_opts.imageable == GusdUSD_CustomTraverse::TRUE_STATE) {
            visit = false;
            // will be inherited.
            ctl.PruneChildren();
        } else if(_opts.purposes.size() > 0 ||
                 _opts.visible == GusdUSD_CustomTraverse::TRUE_STATE) {
            // Can only match prims that depend on imageable attributes
            // Since this prim is not imageable, it can't possibly
            // match our desired visibility or purpose.
            visit = false;
        }
    }
    // Always test purpose and visibility; that may allow us to prune traversal
    // early, and is also necessary for propagation of inherited state.
    visit = AcceptPurpose(ip, ctl) && visit;
    visit = AcceptVis(ip, time, ctl) && visit;

    /* These tests are based on cached data;
       check them before anything that requires attribute reads.*/
    visit = visit && _predicate(prim) && AcceptType(prim);

    visit = visit && AcceptKind(prim)
            && AcceptNamePattern(prim)
            && AcceptPathPattern(prim);

    if(visit && !_opts.traverseMatched)
        ctl.PruneChildren();

    return visit;
}


bool
_Visitor::AcceptType(const UsdPrim& prim) const
{
    if(_opts.types.size() == 0)
        return true;

    const std::string& typeName = prim.GetTypeName().GetString();
    if(!typeName.empty()) {
        /* TODO: profile this search.
                 It may be faster to fill an unordered set of type
                 names to do this test instead.*/
        TfType type =
            PlugRegistry::FindDerivedTypeByName<UsdSchemaBase>(typeName);
        for(auto& t : _opts.types) {
            if(type.IsA(t)) {
                return true;
            }
        }
    }
    return false;
}


bool
_Visitor::AcceptPurpose(const UsdGeomImageable& prim,
    GusdUSD_TraverseControl& ctl)
{
    if(_opts.purposes.size() == 0)
        return true;

    if (!_purpose.IsEmpty()) {
        // The root-most non-default purpose wins, so only query a new purpose
        // if we haven't already found a non-default purpose during traversal.
        if (_purpose == UsdGeomTokens->default_) {
            TfToken purpose;
            prim.GetPurposeAttr().Get(&purpose);
            if (!purpose.IsEmpty()) {
                _purpose = purpose;
            }
        }
    } else {
        _purpose = prim.ComputePurpose();
    }
    for(auto& p : _opts.purposes) {
        if(p == _purpose) {
            return true;
        }
    }
    if (_purpose != UsdGeomTokens->default_) {
        // Purpose is a pruning operation; if a non-default purpose
        // is found that doesn't match, we should not traverse further.
        ctl.PruneChildren();
    }
    return false;
}


bool
_Visitor::AcceptKind(const UsdPrim& prim) const
{
    if(_opts.kinds.size() == 0)
        return true;

    UsdModelAPI model(prim);
    TfToken kind;
    model.GetKind(&kind);
    for(auto& k : _opts.kinds) {
        if(KindRegistry::IsA(kind, k))
            return true;
    }
    return false;
}


bool
_Visitor::AcceptVis(const UsdGeomImageable& prim,
    UsdTimeCode time,
    GusdUSD_TraverseControl& ctl)
{
    if(_opts.visible == GusdUSD_CustomTraverse::ANY_STATE)
        return true;

    if (!_vis.IsEmpty()) {
        TfToken vis;
        prim.GetVisibilityAttr().Get(&vis, time);
        if (vis == UsdGeomTokens->invisible) {
            _vis = vis;
        }
    } else {
        _vis = prim.ComputeVisibility(time);
    }

    if(_opts.visible == GusdUSD_CustomTraverse::TRUE_STATE) {
        if (_vis == UsdGeomTokens->inherited) {
            return true;
        }
        // Not visible. None of the children will be either,
        // so no need to traverse any further.
        ctl.PruneChildren();
        return false;
    } else { // FALSE_STATE
        return _vis == UsdGeomTokens->invisible;
    }
}


bool
_Visitor::AcceptNamePattern(const UsdPrim& prim) const
{
    if(_opts.namePattern.isEmpty())
        return true;
    return UT_String(prim.GetName().GetText()).multiMatch(_opts.namePattern);
}


bool
_Visitor::AcceptPathPattern(const UsdPrim& prim) const
{
    if(_opts.pathPattern.isEmpty())
        return true;
    return UT_String(prim.GetPath().GetText()).multiMatch(_opts.pathPattern);
}


static GusdUSD_CustomTraverse::Opts _defaultOpts;


bool
GusdUSD_CustomTraverse::FindPrims(const UsdPrim& root,
    UsdTimeCode time,
    GusdPurposeSet purposes,
    UT_Array<UsdPrim>& prims,
    bool skipRoot,
    const GusdUSD_Traverse::Opts* opts) const
{
    const auto* customOpts = UTverify_cast<const Opts*>(opts);
    _Visitor visitor(customOpts ? *customOpts : _defaultOpts);

    return GusdUSD_ThreadedTraverse::ParallelFindPrims(
        root, time, purposes, prims, visitor, skipRoot);
}


bool
GusdUSD_CustomTraverse::FindPrims(
    const UT_Array<UsdPrim>& roots,
    const GusdDefaultArray<UsdTimeCode>& times,
    const GusdDefaultArray<GusdPurposeSet>& purposes,
    UT_Array<PrimIndexPair>& prims,
    bool skipRoot,
    const GusdUSD_Traverse::Opts* opts) const
{
    const auto* customOpts = UTverify_cast<const Opts*>(opts);
    _Visitor visitor(customOpts ? *customOpts : _defaultOpts);

    return GusdUSD_ThreadedTraverse::ParallelFindPrims(
        roots, times, purposes, prims, visitor, skipRoot);
}

void _MakePrefixedName(UT_String& str, const char* name,
    int prefixCount, const char* prefix)
{
    const int len = strlen(prefix);

    UT_WorkBuffer buf;
    for(int i = 0; i < prefixCount; ++i)
        buf.append(prefix, len);
    buf.append(name);
    buf.stealIntoString(str);
}

void _AppendTypes(const TfType& type, UT_Array<PRM_Name>& names,
    PRM_AutoDeleter& deleter, int depth=0)
{
    const auto& typeName = type.GetTypeName();
    // Add spacing at front, by depth, to indicate hierarchy.
    UT_String label;
    _MakePrefixedName(label, typeName.c_str(), depth, "|   ");

    // Only 16.5 (not 16.0 and not 17.0) needs to use BOOST_NS::function here.
    // In 16.0 BOOST_NS doesn't exist yet. In 17.0 the argument is templated.
    names.append(PRM_Name(typeName.c_str(), deleter.appendCallback(
        BOOST_NS::function<void (char *)>(free), label.steal())));

    for(const auto& derived : type.GetDirectlyDerivedTypes())
        _AppendTypes(derived, names, deleter, depth+1);
}

PRM_Name* _GetTypeNames()
{
    static UT_Array<PRM_Name> names;
    static PRM_AutoDeleter deleter;

    TfType type = TfType::Find<UsdSchemaBase>();
    _AppendTypes(type, names, deleter);

    names.append(PRM_Name());

    return &names(0);
}

void _AppendKinds(const GusdUSD_Utils::KindNode* kind,
    UT_Array<PRM_Name>& names,
    PRM_AutoDeleter& deleter, int depth=0)
{
    const auto& name = kind->kind.GetString();
    // Add spacing at front, by depth, to indicate hierarchy.
    UT_String label;
    _MakePrefixedName(label, name.c_str(), depth, "|   ");

    // Only 16.5 (not 16.0 and not 17.0) needs to use BOOST_NS::function here.
    // In 16.0 BOOST_NS doesn't exist yet. In 17.0 the argument is templated.
    names.append(PRM_Name(name.c_str(), deleter.appendCallback(
        BOOST_NS::function<void (char *)>(free), label.steal())));

    for(const auto& derived : kind->children)
        _AppendKinds(derived.get(), names, deleter, depth+1);
}

PRM_Name* _GetModelKindNames()
{
    static UT_Array<PRM_Name> names;
    static PRM_AutoDeleter deleter;

    for(const auto& kind : GusdUSD_Utils::GetModelKindHierarchy().children)
        _AppendKinds(kind.get(), names, deleter);

    names.append(PRM_Name());

    return &names(0);
}

PRM_Name* _GetPurposeNames()
{
    static UT_Array<PRM_Name> names;
    for(const auto& p : UsdGeomImageable::GetOrderedPurposeTokens())
        names.append(PRM_Name(p.GetText(), p.GetText()));
    names.append(PRM_Name());
    return &names(0);
}

const PRM_Template* _CreateTemplates()
{
    static PRM_Default trueDef(GusdUSD_CustomTraverse::TRUE_STATE, "");
    static PRM_Default falseDef(GusdUSD_CustomTraverse::FALSE_STATE, "");
    static PRM_Default anyDef(GusdUSD_CustomTraverse::ANY_STATE, "");

    static PRM_ChoiceList typesMenu(
        PRM_CHOICELIST_TOGGLE, _GetTypeNames());
    static PRM_ChoiceList modelKindsMenu(
        PRM_CHOICELIST_TOGGLE, _GetModelKindNames());
    static PRM_ChoiceList purposesMenu(
        PRM_CHOICELIST_TOGGLE, _GetPurposeNames());

    static PRM_Name activeName("active", "Is Active");
    static PRM_Name visibleName("visible", "Is Visible");
    static PRM_Name imageableName("imageable", "Is Imageable");
    static PRM_Name definedName("defined", "Is Defined");
    static PRM_Name abstractName("abstract", "Is Abstract");
    static PRM_Name groupName("group", "Is Group");
    static PRM_Name modelName("model", "Is Model");
    static PRM_Name instanceName("instance", "Is Instance");
    static PRM_Name prototypeName("prototype", "Is Instance Prototype");
    static PRM_Name clipsName("clips", "Has Clips");

    static PRM_Name stateNames[] = {
        PRM_Name("true", "True"),
        PRM_Name("false", "False"),
        PRM_Name("any", "Ignore"),
        PRM_Name()
    };
    static PRM_ChoiceList stateMenu(PRM_CHOICELIST_SINGLE, stateNames);

    static PRM_Name nameMaskName("namemask", "Name Mask");
    static PRM_Name pathMaskName("pathmask", "Path Mask");

    static PRM_Name traverseMatchedName("traversematched", "Traverse Matched");

    static PRM_Name typesName("types", "Prim Types");
    static PRM_Name purposesName("purposes", "Purposes");
    static PRM_Name kindsName("kinds", "Kinds");

#define _STATETEMPLATE(name,def)                        \
    PRM_Template(PRM_ORD, 1, &name, &def, &stateMenu)

    static PRM_Template templates[] = {
        PRM_Template(PRM_STRING, 1, &typesName,
            PRMzeroDefaults, &typesMenu),
        PRM_Template(PRM_STRING, 1, &purposesName,
            PRMzeroDefaults, &purposesMenu),
        PRM_Template(PRM_STRING, 1, &kindsName,
            PRMzeroDefaults, &modelKindsMenu),
        PRM_Template(PRM_STRING, 1, &nameMaskName, PRMzeroDefaults),
        PRM_Template(PRM_STRING, 1, &pathMaskName, PRMzeroDefaults),
        PRM_Template(PRM_TOGGLE, 1, &traverseMatchedName, PRMzeroDefaults),

        _STATETEMPLATE(activeName, trueDef),
        _STATETEMPLATE(visibleName, trueDef),
        _STATETEMPLATE(imageableName, trueDef),
        _STATETEMPLATE(definedName, trueDef),
        _STATETEMPLATE(abstractName, falseDef),
        _STATETEMPLATE(groupName, anyDef),
        _STATETEMPLATE(modelName, anyDef),
        _STATETEMPLATE(instanceName, anyDef),
        _STATETEMPLATE(prototypeName, anyDef),
        _STATETEMPLATE(clipsName, anyDef),
        PRM_Template()
    };

    return templates;
}

} /*namespace*/

void
SOP_CustomTraversal::RegisterCustomTraversal()
{
    // Simply creating this object will register it with our traversal
    // table. Do this before adding the operator type because the traversal
    // menu gets created statically by accessing the traversals table
    // modified by this static object.
    static GusdUSD_TraverseType _type(new GusdUSD_CustomTraverse, "std:custom",
        "Custom Traversal", _CreateTemplates(),
        "Configurable traversal, allowing complex "
        "discovery patterns.");
}

void
SOP_CustomTraversal::ConcatTemplates(UT_Array<PRM_Template>& array,
    const PRM_Template* templates)
{
    int count = PRM_Template::countTemplates(templates);
    if(count > 0) {
        exint idx = array.size();
        array.bumpSize(array.size() + count);
        UTconvertArray(&array(idx), templates, count);
    }
}

PRM_ChoiceList &
SOP_CustomTraversal::CreateTraversalMenu()
{
    static const PRM_Name theNoTraverseName(_NOTRAVERSE_NAME, "No Traversal");
    static UT_Array<PRM_Name> theNames = [&]() {
        UT_Array<PRM_Name> names;
        names.append(theNoTraverseName);
        const auto& table = GusdUSD_TraverseTable::GetInstance();
        for (const auto& pair : table)
            names.append(pair.second->GetName());

        names.stdsort([](const PRM_Name& a, const PRM_Name& b)
            { return UT_String(a.getLabel()) < UT_String(b.getLabel()); });
        names.append(PRM_Name());
        return names;
    }();
    static PRM_ChoiceList theMenu(PRM_CHOICELIST_SINGLE, &theNames(0));

    return theMenu;
}

PXR_NAMESPACE_CLOSE_SCOPE

