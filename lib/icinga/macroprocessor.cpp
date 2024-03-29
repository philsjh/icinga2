/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2014 Icinga Development Team (http://www.icinga.org)    *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "icinga/macroprocessor.h"
#include "icinga/macroresolver.h"
#include "base/utility.h"
#include "base/array.h"
#include "base/objectlock.h"
#include "base/logger_fwd.h"
#include "base/context.h"
#include "base/dynamicobject.h"
#include <boost/foreach.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/classification.hpp>

using namespace icinga;

Value MacroProcessor::ResolveMacros(const Value& str, const ResolverList& resolvers,
    const CheckResult::Ptr& cr, String *missingMacro,
    const MacroProcessor::EscapeCallback& escapeFn)
{
	Value result;

	if (str.IsEmpty())
		return Empty;

	if (str.IsScalar()) {
		result = InternalResolveMacros(str, resolvers, cr, missingMacro, escapeFn);
	} else if (str.IsObjectType<Array>()) {
		Array::Ptr resultArr = make_shared<Array>();
		Array::Ptr arr = str;

		ObjectLock olock(arr);

		BOOST_FOREACH(const Value& arg, arr) {
			/* Note: don't escape macros here. */
			resultArr->Add(InternalResolveMacros(arg, resolvers, cr, missingMacro, EscapeCallback()));
		}

		result = resultArr;
	} else {
		BOOST_THROW_EXCEPTION(std::invalid_argument("Command is not a string or array."));
	}

	return result;
}

bool MacroProcessor::ResolveMacro(const String& macro, const ResolverList& resolvers,
    const CheckResult::Ptr& cr, String *result, bool *recursive_macro)
{
	CONTEXT("Resolving macro '" + macro + "'");

	*recursive_macro = false;

	std::vector<String> tokens;
	boost::algorithm::split(tokens, macro, boost::is_any_of("."));

	String objName;
	if (tokens.size() > 1) {
		objName = tokens[0];
		tokens.erase(tokens.begin());
	}

	BOOST_FOREACH(const ResolverSpec& resolver, resolvers) {
		if (!objName.IsEmpty() && objName != resolver.first)
			continue;

		if (objName.IsEmpty()) {
			DynamicObject::Ptr dobj = dynamic_pointer_cast<DynamicObject>(resolver.second);

			if (dobj) {
				Dictionary::Ptr vars = dobj->GetVars();

				if (vars && vars->Contains(macro)) {
					*result = vars->Get(macro);
					*recursive_macro = true;
					return true;
				}
			}
		}

		MacroResolver::Ptr mresolver = dynamic_pointer_cast<MacroResolver>(resolver.second);

		if (mresolver && mresolver->ResolveMacro(boost::algorithm::join(tokens, "."), cr, result))
			return true;

		Value ref = resolver.second;
		bool valid = true;

		BOOST_FOREACH(const String& token, tokens) {
			if (ref.IsObjectType<Dictionary>()) {
				Dictionary::Ptr dict = ref;
				if (dict->Contains(token)) {
					ref = dict->Get(token);
					continue;
				} else {
					valid = false;
					break;
				}
			} else if (ref.IsObject()) {
				Object::Ptr object = ref;

				const Type *type = object->GetReflectionType();

				if (!type) {
					valid = false;
					break;
				}

				int field = type->GetFieldId(token);

				if (field == -1) {
					valid = false;
					break;
				}

				ref = object->GetField(field);
			}
		}

		if (valid) {
			if (tokens[0] == "vars" ||
			    tokens[0] == "action_url" ||
			    tokens[0] == "notes_url" ||
			    tokens[0] == "notes")
				*recursive_macro = true;

			*result = ref;
			return true;
		}
	}

	return false;
}

String MacroProcessor::InternalResolveMacros(const String& str, const ResolverList& resolvers,
    const CheckResult::Ptr& cr, String *missingMacro,
    const MacroProcessor::EscapeCallback& escapeFn, int recursionLevel)
{
	CONTEXT("Resolving macros for string '" + str + "'");

	if (recursionLevel > 15)
		BOOST_THROW_EXCEPTION(std::runtime_error("Infinite recursion detected while resolving macros"));

	size_t offset, pos_first, pos_second;
	offset = 0;

	String result = str;
	while ((pos_first = result.FindFirstOf("$", offset)) != String::NPos) {
		pos_second = result.FindFirstOf("$", pos_first + 1);

		if (pos_second == String::NPos)
			BOOST_THROW_EXCEPTION(std::runtime_error("Closing $ not found in macro format string."));

		String name = result.SubStr(pos_first + 1, pos_second - pos_first - 1);

		String resolved_macro;
		bool recursive_macro;
		bool found = ResolveMacro(name, resolvers, cr, &resolved_macro, &recursive_macro);

		/* $$ is an escape sequence for $. */
		if (name.IsEmpty()) {
			resolved_macro = "$";
			found = true;
		}

		if (!found) {
			if (!missingMacro)
				Log(LogWarning, "icinga", "Macro '" + name + "' is not defined.");
			else
				*missingMacro = name;
		}

		/* recursively resolve macros in the macro if it was a user macro */
		if (recursive_macro)
			resolved_macro = InternalResolveMacros(resolved_macro,
			    resolvers, cr, missingMacro, EscapeCallback(), recursionLevel + 1);

		if (escapeFn)
			resolved_macro = escapeFn(resolved_macro);

		result.Replace(pos_first, pos_second - pos_first + 1, resolved_macro);
		offset = pos_first + resolved_macro.GetLength() + 1;
	}

	return result;
}
