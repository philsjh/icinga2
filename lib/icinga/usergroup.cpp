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

#include "icinga/usergroup.h"
#include "base/dynamictype.h"
#include "base/objectlock.h"
#include "base/logger_fwd.h"
#include "base/timer.h"
#include "base/utility.h"
#include "base/context.h"
#include <boost/foreach.hpp>

using namespace icinga;

REGISTER_TYPE(UserGroup);

INITIALIZE_ONCE(&UserGroup::RegisterObjectRuleHandler);

void UserGroup::RegisterObjectRuleHandler(void)
{
        ObjectRule::RegisterType("UserGroup", &UserGroup::EvaluateObjectRules);
}

bool UserGroup::EvaluateObjectRule(const User::Ptr user, const ObjectRule& rule)
{
	DebugInfo di = rule.GetDebugInfo();

	std::ostringstream msgbuf;
	msgbuf << "Evaluating 'object' rule (" << di << ")";
	CONTEXT(msgbuf.str());

	Dictionary::Ptr locals = make_shared<Dictionary>();
	locals->Set("user", user);

	if (!rule.EvaluateFilter(locals))
		return false;

	std::ostringstream msgbuf2;
	msgbuf2 << "Assigning membership for group '" << rule.GetName() << "' to user '" << user->GetName() << "' for rule " << di;
	Log(LogDebug, "icinga", msgbuf2.str());

	String group_name = rule.GetName();
	UserGroup::Ptr group = UserGroup::GetByName(group_name);

	if (!group) {
		Log(LogCritical, "icinga", "Invalid membership assignment. Group '" + group_name + "' does not exist.");
		return false;
	}

	/* assign user group membership */
	group->ResolveGroupMembership(user, true);

	return true;
}

void UserGroup::EvaluateObjectRules(const std::vector<ObjectRule>& rules)
{
	BOOST_FOREACH(const ObjectRule& rule, rules) {
		BOOST_FOREACH(const User::Ptr& user, DynamicType::GetObjects<User>()) {
			CONTEXT("Evaluating group membership in '" + rule.GetName() + "' for user '" + user->GetName() + "'");

			EvaluateObjectRule(user, rule);
		}
	}
}

std::set<User::Ptr> UserGroup::GetMembers(void) const
{
	boost::mutex::scoped_lock lock(m_UserGroupMutex);
	return m_Members;
}

void UserGroup::AddMember(const User::Ptr& user)
{
	boost::mutex::scoped_lock lock(m_UserGroupMutex);
	m_Members.insert(user);
}

void UserGroup::RemoveMember(const User::Ptr& user)
{
	boost::mutex::scoped_lock lock(m_UserGroupMutex);
	m_Members.erase(user);
}

bool UserGroup::ResolveGroupMembership(User::Ptr const& user, bool add, int rstack) {

	if (add && rstack > 20) {
		Log(LogWarning, "icinga", "Too many nested groups for group '" + GetName() + "': User '" +
		    user->GetName() + "' membership assignment failed.");

		return false;
	}

	Array::Ptr groups = GetGroups();

	if (groups && groups->GetLength() > 0) {
		ObjectLock olock(groups);

		BOOST_FOREACH(const String& name, groups) {
			UserGroup::Ptr group = UserGroup::GetByName(name);

			if (group && !group->ResolveGroupMembership(user, add, rstack + 1))
				return false;
		}
	}

	if (add)
		AddMember(user);
	else
		RemoveMember(user);

	return true;
}

