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

#include "icinga/dependency.h"
#include "icinga/service.h"
#include "config/configcompilercontext.h"
#include "base/dynamictype.h"
#include "base/objectlock.h"
#include "base/logger_fwd.h"
#include "base/scriptfunction.h"
#include <boost/foreach.hpp>

using namespace icinga;

REGISTER_TYPE(Dependency);
REGISTER_SCRIPTFUNCTION(ValidateDependencyFilters, &Dependency::ValidateFilters);

String DependencyNameComposer::MakeName(const String& shortName, const Dictionary::Ptr props) const
{
	if (!props)
		return "";

	String name = props->Get("child_host_name");

	if (props->Contains("child_service_name"))
		name += "!" + props->Get("child_service_name");

	name += "!" + shortName;

	return name;
}

void Dependency::OnConfigLoaded(void)
{
	Value defaultFilter;

	if (GetParentServiceName().IsEmpty())
		defaultFilter = StateFilterUp;
	else
		defaultFilter = StateFilterOK | StateFilterWarning;

	SetStateFilter(FilterArrayToInt(GetStates(), defaultFilter));
}

void Dependency::OnStateLoaded(void)
{
	DynamicObject::Start();

	ASSERT(!OwnsLock());

	if (!GetChild())
		Log(LogWarning, "icinga", "Dependency '" + GetName() + "' references an invalid child service and will be ignored.");
	else
		GetChild()->AddDependency(GetSelf());

	if (!GetParent())
		Log(LogWarning, "icinga", "Dependency '" + GetName() + "' references an invalid parent service and will always fail.");
	else
		GetParent()->AddReverseDependency(GetSelf());
}

void Dependency::Stop(void)
{
	DynamicObject::Stop();

	if (GetChild())
		GetChild()->RemoveDependency(GetSelf());

	if (GetParent())
		GetParent()->RemoveReverseDependency(GetSelf());
}

bool Dependency::IsAvailable(DependencyType dt) const
{
	Checkable::Ptr parent = GetParent();

	if (!parent)
		return false;

	/* ignore if it's the same service */
	if (parent == GetChild()) {
		Log(LogDebug, "icinga", "Dependency '" + GetName() + "' passed: Parent and child service are identical.");
		return true;
	}

	/* ignore pending services */
	if (!parent->GetLastCheckResult()) {
		Log(LogDebug, "icinga", "Dependency '" + GetName() + "' passed: Service hasn't been checked yet.");
		return true;
	}

	/* ignore soft states */
	if (parent->GetStateType() == StateTypeSoft) {
		Log(LogDebug, "icinga", "Dependency '" + GetName() + "' passed: Service is in a soft state.");
		return true;
	}

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(parent);

	int state;

	if (service)
		state = ServiceStateToFilter(service->GetState());
	else
		state = HostStateToFilter(host->GetState());

	/* check state */
	if (state & GetStateFilter()) {
		Log(LogDebug, "icinga", "Dependency '" + GetName() + "' passed: Object matches state filter.");
		return true;
	}

	/* ignore if not in time period */
	TimePeriod::Ptr tp = GetPeriod();
	if (tp && !tp->IsInside(Utility::GetTime())) {
		Log(LogDebug, "icinga", "Dependency '" + GetName() + "' passed: Outside time period.");
		return true;
	}

	if (dt == DependencyCheckExecution && !GetDisableChecks()) {
		Log(LogDebug, "icinga", "Dependency '" + GetName() + "' passed: Checks are not disabled.");
		return true;
	} else if (dt == DependencyNotification && !GetDisableNotifications()) {
		Log(LogDebug, "icinga", "Dependency '" + GetName() + "' passed: Notifications are not disabled");
		return true;
	}

	Log(LogDebug, "icinga", "Dependency '" + GetName() + "' failed.");
	return false;
}

Checkable::Ptr Dependency::GetChild(void) const
{
	Host::Ptr host = Host::GetByName(GetChildHostName());

	if (!host)
		return Service::Ptr();

	if (GetChildServiceName().IsEmpty())
		return host;
	else
		return host->GetServiceByShortName(GetChildServiceName());
}

Checkable::Ptr Dependency::GetParent(void) const
{
	Host::Ptr host = Host::GetByName(GetParentHostName());

	if (!host)
		return Service::Ptr();

	if (GetParentServiceName().IsEmpty())
		return host;
	else
		return host->GetServiceByShortName(GetParentServiceName());
}

TimePeriod::Ptr Dependency::GetPeriod(void) const
{
	return TimePeriod::GetByName(GetPeriodRaw());
}

void Dependency::ValidateFilters(const String& location, const Dictionary::Ptr& attrs)
{
	int sfilter = FilterArrayToInt(attrs->Get("state_filter"), 0);

	if (!attrs->Contains("parent_service_name") && (sfilter & ~(StateFilterUp | StateFilterDown)) != 0) {
		ConfigCompilerContext::GetInstance()->AddMessage(true, "Validation failed for " +
		    location + ": State filter is invalid.");
	}

	if (attrs->Contains("parent_service_name") && (sfilter & ~(StateFilterOK | StateFilterWarning | StateFilterCritical | StateFilterUnknown)) != 0) {
		ConfigCompilerContext::GetInstance()->AddMessage(true, "Validation failed for " +
		    location + ": State filter is invalid.");
	}
}
