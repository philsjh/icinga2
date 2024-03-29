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

#ifndef MACROPROCESSOR_H
#define MACROPROCESSOR_H

#include "icinga/i2-icinga.h"
#include "icinga/macroresolver.h"
#include "base/dictionary.h"
#include "base/array.h"
#include <boost/function.hpp>
#include <map>

namespace icinga
{

/**
 * Resolves macros.
 *
 * @ingroup icinga
 */
class I2_ICINGA_API MacroProcessor
{
public:
	typedef boost::function<String (const String&)> EscapeCallback;
	typedef std::pair<String, Object::Ptr> ResolverSpec;
	typedef std::vector<ResolverSpec> ResolverList;

	static Value ResolveMacros(const Value& str, const ResolverList& resolvers,
	    const CheckResult::Ptr& cr = CheckResult::Ptr(), String *missingMacro = NULL,
	    const EscapeCallback& escapeFn = EscapeCallback());

private:
	MacroProcessor(void);

	static bool ResolveMacro(const String& macro, const ResolverList& resolvers,
		const CheckResult::Ptr& cr, String *result, bool *recursive_macro);
	static String InternalResolveMacros(const String& str,
	    const ResolverList& resolvers, const CheckResult::Ptr& cr,
	    String *missingMacro, const EscapeCallback& escapeFn,
	    int recursionLevel = 0);
};

}

#endif /* MACROPROCESSOR_H */
