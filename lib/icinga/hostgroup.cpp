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

#include "icinga/hostgroup.h"
#include "base/dynamictype.h"
#include "base/logger_fwd.h"
#include "base/objectlock.h"
#include "base/utility.h"
#include "base/timer.h"
#include "base/context.h"
#include <boost/foreach.hpp>

using namespace icinga;

REGISTER_TYPE(HostGroup);

INITIALIZE_ONCE(&HostGroup::RegisterObjectRuleHandler);

void HostGroup::RegisterObjectRuleHandler(void)
{
        ObjectRule::RegisterType("HostGroup", &HostGroup::EvaluateObjectRules);
}

bool HostGroup::EvaluateObjectRule(const Host::Ptr host, const ObjectRule& rule)
{
	DebugInfo di = rule.GetDebugInfo();

	std::ostringstream msgbuf;
	msgbuf << "Evaluating 'object' rule (" << di << ")";
	CONTEXT(msgbuf.str());

	Dictionary::Ptr locals = make_shared<Dictionary>();
	locals->Set("host", host);

	if (!rule.EvaluateFilter(locals))
		return false;

	std::ostringstream msgbuf2;
	msgbuf2 << "Assigning membership for group '" << rule.GetName() << "' to host '" << host->GetName() << "' for rule " << di;
	Log(LogDebug, "icinga", msgbuf2.str());

	String group_name = rule.GetName();
	HostGroup::Ptr group = HostGroup::GetByName(group_name);

	if (!group) {
		Log(LogCritical, "icinga", "Invalid membership assignment. Group '" + group_name + "' does not exist.");
		return false;
	}

	/* assign host group membership */
	group->ResolveGroupMembership(host, true);

	return true;
}

void HostGroup::EvaluateObjectRules(const std::vector<ObjectRule>& rules)
{
	BOOST_FOREACH(const ObjectRule& rule, rules) {
		BOOST_FOREACH(const Host::Ptr& host, DynamicType::GetObjects<Host>()) {
			CONTEXT("Evaluating group membership in '" + rule.GetName() + "' for host '" + host->GetName() + "'");

			EvaluateObjectRule(host, rule);
		}
	}
}

std::set<Host::Ptr> HostGroup::GetMembers(void) const
{
	boost::mutex::scoped_lock lock(m_HostGroupMutex);
	return m_Members;
}

void HostGroup::AddMember(const Host::Ptr& host)
{
	boost::mutex::scoped_lock lock(m_HostGroupMutex);
	m_Members.insert(host);
}

void HostGroup::RemoveMember(const Host::Ptr& host)
{
	boost::mutex::scoped_lock lock(m_HostGroupMutex);
	m_Members.erase(host);
}

bool HostGroup::ResolveGroupMembership(Host::Ptr const& host, bool add, int rstack) {

	if (add && rstack > 20) {
		Log(LogWarning, "icinga", "Too many nested groups for group '" + GetName() + "': Host '" +
		    host->GetName() + "' membership assignment failed.");

		return false;
	}

	Array::Ptr groups = GetGroups();

	if (groups && groups->GetLength() > 0) {
		ObjectLock olock(groups);

		BOOST_FOREACH(const String& name, groups) {
			HostGroup::Ptr group = HostGroup::GetByName(name);

			if (group && !group->ResolveGroupMembership(host, add, rstack + 1))
				return false;
		}
	}

	if (add)
		AddMember(host);
	else
		RemoveMember(host);

	return true;
}
