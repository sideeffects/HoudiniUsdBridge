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

#include "HUSD_ExpansionState.h"
#include <UT/UT_IStream.h>
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONValue.h>
#include <UT/UT_JSONWriter.h>

static constexpr UT_StringLit	 theExpandedKey("expanded");
static constexpr UT_StringLit	 theChildrenKey("children");

HUSD_ExpansionState::HUSD_ExpansionState()
    : myExpanded(false)
{
    // Always start with the root node expanded.
    setExpanded("/", true);
}

HUSD_ExpansionState::~HUSD_ExpansionState()
{
}

bool
HUSD_ExpansionState::isExpanded(const char *path) const
{
    // Skip past the initial slash if there is one.
    if (*path == '/')
	path++;

    UT_StringView		 pathview(path);
    UT_StringViewArray		 pathtokens(pathview.split("/"));
    const HUSD_ExpansionState	*expstate = this;
    int				 depth = 0;

    // If the first path token is an empty string, that token refers to "this",
    // and we're already pointing there. So advance to the next token.
    if (pathtokens.entries() > 0 && pathtokens(0).isEmpty())
	depth++;

    // Loop through each part of the path.
    while (expstate && pathtokens.entries() > depth)
    {
	const HUSD_ExpansionStateMap &childmap = expstate->myChildren;
	UT_StringHolder childname(pathtokens(depth));
	auto childit = childmap.find(childname);

	if (childit != childmap.end())
	    expstate = childit->second.get();
	else
	    expstate = nullptr;
	depth++;
    }

    // If the branch doesn't even exist, it must not be expanded.
    return expstate ? expstate->myExpanded : false;
}

void
HUSD_ExpansionState::setExpanded(const char *path, bool expanded)
{
    // Skip past the initial slash if there is one.
    if (*path == '/')
	path++;

    UT_StringView		 pathview(path);
    UT_StringViewArray		 pathtokens(pathview.split("/"));
    HUSD_ExpansionState		*expstate = this;
    int				 depth = 0;

    // If the first path token is an empty string, that token refers to "this",
    // and we're already pointing there. So advance to the next token.
    if (pathtokens.entries() > 0 && pathtokens(0).isEmpty())
	depth++;

    // Loop through each part of the path.
    while (expstate && pathtokens.entries() > depth)
    {
	HUSD_ExpansionStateMap &childmap = expstate->myChildren;
	UT_StringHolder childname(pathtokens(depth));
	auto childit = childmap.find(childname);

	if (childit == childmap.end())
	{
	    // We have more path, but no more hierarhcy. If we are
	    // expanding, expand the hierarchy. Otherwise, we are
	    // collapsing something inside an already-collapsed part
	    // of the hierarchy. Just exit.
	    if (expanded)
	    {
		expstate = new HUSD_ExpansionState();
		expstate->myExpanded = true;
		childmap.emplace(childname, expstate);
	    }
	    else
		expstate = nullptr;
	}
	else
	    expstate = childit->second.get();
	depth++;
    }

    // Record the new expansion state for this branch.
    if (expstate)
	expstate->myExpanded = expanded;
}

const HUSD_ExpansionState *
HUSD_ExpansionState::getChild(const UT_StringRef &name) const
{
    auto childit = myChildren.find(name);

    if (childit != myChildren.end())
	return childit->second.get();

    return nullptr;
}

bool
HUSD_ExpansionState::getExpanded() const
{
    return myExpanded;
}

exint
HUSD_ExpansionState::getMemoryUsage() const
{
    exint	 length = sizeof(*this) + myChildren.getMemoryUsage(false);

    for (auto &&child : myChildren)
	length += child.second->getMemoryUsage();

    return length;
}

void
HUSD_ExpansionState::clear()
{
    myChildren.clear();
    myExpanded = false;
}

void
HUSD_ExpansionState::copy(const HUSD_ExpansionState &src)
{
    myChildren.clear();
    myExpanded = src.myExpanded;
    for (auto &&child : src.myChildren)
    {
	HUSD_ExpansionStateHandle	 childcopy(new HUSD_ExpansionState());

	childcopy->copy(*child.second);
	myChildren.emplace(child.first, childcopy);
    }
}

bool
HUSD_ExpansionState::save(UT_JSONWriter &writer) const
{
    bool	 success = true;

    success &= writer.jsonBeginMap();
    success &= writer.jsonKeyToken(theExpandedKey.asRef());
    success &= writer.jsonBool(myExpanded);
    if (!myChildren.empty())
    {
	UT_StringArray	 childnames;

	success &= writer.jsonKeyToken(theChildrenKey.asRef());
	success &= writer.jsonBeginMap();
	for (auto &&child : myChildren)
	    childnames.append(child.first);
	childnames.sort();
	for (auto &&childname : childnames)
	{
	    auto child = myChildren.find(childname);

	    if (child != myChildren.end())
	    {
		success &= writer.jsonKeyToken(childname);
		success &= child->second->save(writer);
	    }
	}
	success &= writer.jsonEndMap();
    }
    success &= writer.jsonEndMap();

    return success;
}

bool
HUSD_ExpansionState::save(std::ostream &os, bool binary) const
{
    UT_AutoJSONWriter	 writer(os, binary);

    return save(*writer);
}

bool
HUSD_ExpansionState::load(const UT_JSONValue &value)
{
    const UT_JSONValueMap	*map = value.getMap();

    if (!map)
	return false;

    const UT_JSONValue	*expanded_value = map->get(theExpandedKey.asRef());
    const UT_JSONValue	*children_value = map->get(theChildrenKey.asRef());

    if (expanded_value && expanded_value->getB())
	myExpanded = true;
    else
	myExpanded = false;

    if (children_value)
    {
	const UT_JSONValueMap	*children_map = children_value->getMap();
	UT_StringArray		 childnames;

	if (!children_map)
	    return false;

	children_map->getKeyReferences(childnames);
	for (auto &&childname : childnames)
	{
	    const UT_JSONValue	*child_value = children_map->get(childname);

	    if (!child_value)
		return false;

	    HUSD_ExpansionStateHandle	 child_state(new HUSD_ExpansionState());

	    if (!child_state->load(*child_value))
		return false;

	    myChildren.emplace(childname, child_state);
	}
    }

    return true;
}

bool
HUSD_ExpansionState::load(UT_IStream &is)
{
    UT_AutoJSONParser	 parser(is);
    UT_JSONValue	 rootvalue;

    if (!rootvalue.parseValue(parser))
	return false;

    return load(rootvalue);
}

