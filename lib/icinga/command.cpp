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

#include "icinga/command.h"

using namespace icinga;

REGISTER_TYPE(Command);

bool Command::ResolveMacro(const String& macro, const CheckResult::Ptr&, String *result) const
{
	Dictionary::Ptr vars = GetVars();

	if (vars && vars->Contains(macro)) {
		*result = vars->Get(macro);
		return true;
	}

	return false;
}

int Command::GetModifiedAttributes(void) const
{
	int attrs = 0;

	if (!GetOverrideVars().IsEmpty())
		attrs |= ModAttrCustomVariable;

	return attrs;
}

void Command::SetModifiedAttributes(int flags)
{
	if ((flags & ModAttrCustomVariable) == 0) {
		SetOverrideVars(Empty);
		OnVarsChanged(GetSelf());
	}
}
