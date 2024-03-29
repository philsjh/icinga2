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

#include "icinga/notification.h"
#include "icinga/notificationcommand.h"
#include "icinga/macroprocessor.h"
#include "icinga/service.h"
#include "config/configcompilercontext.h"
#include "base/dynamictype.h"
#include "base/objectlock.h"
#include "base/logger_fwd.h"
#include "base/utility.h"
#include "base/convert.h"
#include "base/exception.h"
#include "base/initialize.h"
#include "base/scriptvariable.h"
#include "base/scriptfunction.h"
#include <boost/foreach.hpp>

using namespace icinga;

REGISTER_TYPE(Notification);
REGISTER_SCRIPTFUNCTION(ValidateNotificationFilters, &Notification::ValidateFilters);
INITIALIZE_ONCE(&Notification::StaticInitialize);

boost::signals2::signal<void (const Notification::Ptr&, double, const String&)> Notification::OnNextNotificationChanged;

String NotificationNameComposer::MakeName(const String& shortName, const Dictionary::Ptr props) const
{
	if (!props)
		return "";

	String name = props->Get("host_name");

	if (props->Contains("service_name"))
		name += "!" + props->Get("service_name");

	name += "!" + shortName;

	return name;
}

void Notification::StaticInitialize(void)
{
	ScriptVariable::Set("OK", StateFilterOK, true, true);
	ScriptVariable::Set("Warning", StateFilterWarning, true, true);
	ScriptVariable::Set("Critical", StateFilterCritical, true, true);
	ScriptVariable::Set("Unknown", StateFilterUnknown, true, true);
	ScriptVariable::Set("Up", StateFilterUp, true, true);
	ScriptVariable::Set("Down", StateFilterDown, true, true);

	ScriptVariable::Set("DowntimeStart", 1 << NotificationDowntimeStart, true, true);
	ScriptVariable::Set("DowntimeEnd", 1 << NotificationDowntimeEnd, true, true);
	ScriptVariable::Set("DowntimeRemoved", 1 << NotificationDowntimeRemoved, true, true);
	ScriptVariable::Set("Custom", 1 << NotificationCustom, true, true);
	ScriptVariable::Set("Acknowledgement", 1 << NotificationAcknowledgement, true, true);
	ScriptVariable::Set("Problem", 1 << NotificationProblem, true, true);
	ScriptVariable::Set("Recovery", 1 << NotificationRecovery, true, true);
	ScriptVariable::Set("FlappingStart", 1 << NotificationFlappingStart, true, true);
	ScriptVariable::Set("FlappingEnd", 1 << NotificationFlappingEnd, true, true);
}

void Notification::OnConfigLoaded(void)
{
	SetTypeFilter(FilterArrayToInt(GetTypes(), ~0));
	SetStateFilter(FilterArrayToInt(GetStates(), ~0));

	GetCheckable()->AddNotification(GetSelf());
}

void Notification::Start(void)
{
	DynamicObject::Start();

	GetCheckable()->AddNotification(GetSelf());
}

void Notification::Stop(void)
{
	DynamicObject::Stop();

	GetCheckable()->RemoveNotification(GetSelf());
}

Checkable::Ptr Notification::GetCheckable(void) const
{
	Host::Ptr host = Host::GetByName(GetHostName());

	if (GetServiceName().IsEmpty())
		return host;
	else
		return host->GetServiceByShortName(GetServiceName());
}

NotificationCommand::Ptr Notification::GetCommand(void) const
{
	return NotificationCommand::GetByName(GetCommandRaw());
}

std::set<User::Ptr> Notification::GetUsers(void) const
{
	std::set<User::Ptr> result;

	Array::Ptr users = GetUsersRaw();

	if (users) {
		ObjectLock olock(users);

		BOOST_FOREACH(const String& name, users) {
			User::Ptr user = User::GetByName(name);

			if (!user)
				continue;

			result.insert(user);
		}
	}

	return result;
}

std::set<UserGroup::Ptr> Notification::GetUserGroups(void) const
{
	std::set<UserGroup::Ptr> result;

	Array::Ptr groups = GetUserGroupsRaw();

	if (groups) {
		ObjectLock olock(groups);

		BOOST_FOREACH(const String& name, groups) {
			UserGroup::Ptr ug = UserGroup::GetByName(name);

			if (!ug)
				continue;

			result.insert(ug);
		}
	}

	return result;
}

TimePeriod::Ptr Notification::GetPeriod(void) const
{
	return TimePeriod::GetByName(GetPeriodRaw());
}

double Notification::GetNextNotification(void) const
{
	return GetNextNotificationRaw();
}

/**
 * Sets the timestamp when the next periodical notification should be sent.
 * This does not affect notifications that are sent for state changes.
 */
void Notification::SetNextNotification(double time, const String& authority)
{
	SetNextNotificationRaw(time);

	OnNextNotificationChanged(GetSelf(), time, authority);
}

void Notification::UpdateNotificationNumber(void)
{
	SetNotificationNumber(GetNotificationNumber() + 1);
}

void Notification::ResetNotificationNumber(void)
{
	SetNotificationNumber(0);
}

String Notification::NotificationTypeToString(NotificationType type)
{
	switch (type) {
		case NotificationDowntimeStart:
			return "DOWNTIMESTART";
		case NotificationDowntimeEnd:
			return "DOWNTIMEEND";
		case NotificationDowntimeRemoved:
			return "DOWNTIMECANCELLED";
		case NotificationCustom:
			return "CUSTOM";
		case NotificationAcknowledgement:
			return "ACKNOWLEDGEMENT";
		case NotificationProblem:
			return "PROBLEM";
		case NotificationRecovery:
			return "RECOVERY";
		case NotificationFlappingStart:
			return "FLAPPINGSTART";
		case NotificationFlappingEnd:
			return "FLAPPINGEND";
		default:
			return "UNKNOWN_NOTIFICATION";
	}
}

void Notification::BeginExecuteNotification(NotificationType type, const CheckResult::Ptr& cr, bool force, const String& author, const String& text)
{
	ASSERT(!OwnsLock());

	Checkable::Ptr checkable = GetCheckable();

	if (!force) {
		TimePeriod::Ptr tp = GetPeriod();

		if (tp && !tp->IsInside(Utility::GetTime())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" + GetName() + "': not in timeperiod");
			return;
		}

		double now = Utility::GetTime();
		Dictionary::Ptr times = GetTimes();

		if (type == NotificationProblem) {
			if (times && times->Contains("begin") && now < checkable->GetLastHardStateChange() + times->Get("begin")) {
				Log(LogInformation, "icinga", "Not sending notifications for notification object '" + GetName() + "': before escalation range");
				return;
			}

			if (times && times->Contains("end") && now > checkable->GetLastHardStateChange() + times->Get("end")) {
				Log(LogInformation, "icinga", "Not sending notifications for notification object '" + GetName() + "': after escalation range");
				return;
			}
		}

		unsigned long ftype = 1 << type;

		Log(LogDebug, "icinga", "FType=" + Convert::ToString(ftype) + ", TypeFilter=" + Convert::ToString(GetTypeFilter()));

		if (!(ftype & GetTypeFilter())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" + GetName() + "': type filter does not match");
			return;
		}

		Host::Ptr host;
		Service::Ptr service;
		tie(host, service) = GetHostService(checkable);

		unsigned long fstate;

		if (service)
			fstate = ServiceStateToFilter(service->GetState());
		else
			fstate = HostStateToFilter(host->GetState());

		if (!(fstate & GetStateFilter())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" + GetName() + "': state filter does not match");
			return;
		}
	}

	{
		ObjectLock olock(this);

		double now = Utility::GetTime();
		SetLastNotification(now);

		if (type == NotificationProblem)
			SetLastProblemNotification(now);
	}

	std::set<User::Ptr> allUsers;

	std::set<User::Ptr> users = GetUsers();
	std::copy(users.begin(), users.end(), std::inserter(allUsers, allUsers.begin()));

	BOOST_FOREACH(const UserGroup::Ptr& ug, GetUserGroups()) {
		std::set<User::Ptr> members = ug->GetMembers();
		std::copy(members.begin(), members.end(), std::inserter(allUsers, allUsers.begin()));
	}

	Service::OnNotificationSendStart(GetSelf(), checkable, allUsers, type, cr, author, text);

	std::set<User::Ptr> allNotifiedUsers;
	BOOST_FOREACH(const User::Ptr& user, allUsers) {
		if (!CheckNotificationUserFilters(type, user, force))
			continue;

		Log(LogDebug, "icinga", "Sending notification for user '" + user->GetName() + "'");
		Utility::QueueAsyncCallback(boost::bind(&Notification::ExecuteNotificationHelper, this, type, user, cr, force, author, text));

		/* collect all notified users */
		allNotifiedUsers.insert(user);
	}

	/* used in db_ido for notification history */
	Service::OnNotificationSentToAllUsers(GetSelf(), checkable, allNotifiedUsers, type, cr, author, text);
}

bool Notification::CheckNotificationUserFilters(NotificationType type, const User::Ptr& user, bool force)
{
	ASSERT(!OwnsLock());

	if (!force) {
		TimePeriod::Ptr tp = user->GetPeriod();

		if (tp && !tp->IsInside(Utility::GetTime())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" +
			    GetName() + " and user '" + user->GetName() + "': user not in timeperiod");
			return false;
		}

		unsigned long ftype = 1 << type;

		if (!(ftype & user->GetTypeFilter())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" +
			    GetName() + " and user '" + user->GetName() + "': type filter does not match");
			return false;
		}

		Checkable::Ptr checkable = GetCheckable();
		Host::Ptr host;
		Service::Ptr service;
		tie(host, service) = GetHostService(checkable);

		unsigned long fstate;

		if (service)
				fstate = ServiceStateToFilter(service->GetState());
		else
				fstate = HostStateToFilter(host->GetState());

		if (!(fstate & user->GetStateFilter())) {
			Log(LogInformation, "icinga", "Not sending notifications for notification object '" +
			    GetName() + " and user '" + user->GetName() + "': state filter does not match");
			return false;
		}
	}

	return true;
}

void Notification::ExecuteNotificationHelper(NotificationType type, const User::Ptr& user, const CheckResult::Ptr& cr, bool force, const String& author, const String& text)
{
	ASSERT(!OwnsLock());

	try {
		NotificationCommand::Ptr command = GetCommand();

		if (!command) {
			Log(LogDebug, "icinga", "No notification_command found for notification '" + GetName() + "'. Skipping execution.");
			return;
		}

		command->Execute(GetSelf(), user, cr, type, author, text);

		{
			ObjectLock olock(this);
			UpdateNotificationNumber();
			SetLastNotification(Utility::GetTime());
		}

		/* required by compatlogger */
		Service::OnNotificationSentToUser(GetSelf(), GetCheckable(), user, type, cr, author, text, command->GetName());

		Log(LogInformation, "icinga", "Completed sending notification for object '" + GetCheckable()->GetName() + "'");
	} catch (const std::exception& ex) {
		std::ostringstream msgbuf;
		msgbuf << "Exception occured during notification for object '"
		       << GetCheckable()->GetName() << "': " << DiagnosticInformation(ex);
		Log(LogWarning, "icinga", msgbuf.str());
	}
}

int icinga::ServiceStateToFilter(ServiceState state)
{
	switch (state) {
		case ServiceOK:
			return StateFilterOK;
		case ServiceWarning:
			return StateFilterWarning;
		case ServiceCritical:
			return StateFilterCritical;
		case ServiceUnknown:
			return StateFilterUnknown;
		default:
			VERIFY(!"Invalid state type.");
	}
}

int icinga::HostStateToFilter(HostState state)
{
	switch (state) {
		case HostUp:
			return StateFilterUp;
		case HostDown:
			return StateFilterDown;
		default:
			VERIFY(!"Invalid state type.");
	}
}

int icinga::FilterArrayToInt(const Array::Ptr& typeFilters, int defaultValue)
{
	Value resultTypeFilter;

	if (!typeFilters)
		return defaultValue;

	resultTypeFilter = 0;

	ObjectLock olock(typeFilters);
	BOOST_FOREACH(const Value& typeFilter, typeFilters) {
		resultTypeFilter = resultTypeFilter | typeFilter;
	}

	return resultTypeFilter;
}

void Notification::ValidateFilters(const String& location, const Dictionary::Ptr& attrs)
{
	int sfilter = FilterArrayToInt(attrs->Get("notification_state_filter"), 0);

	if (!attrs->Contains("service_name") && (sfilter & ~(StateFilterUp | StateFilterDown)) != 0) {
		ConfigCompilerContext::GetInstance()->AddMessage(true, "Validation failed for " +
		    location + ": State filter is invalid.");
	}

	if (attrs->Contains("service_name") && (sfilter & ~(StateFilterOK | StateFilterWarning | StateFilterCritical | StateFilterUnknown)) != 0) {
		ConfigCompilerContext::GetInstance()->AddMessage(true, "Validation failed for " +
		    location + ": State filter is invalid.");
	}

	int tfilter = FilterArrayToInt(attrs->Get("notification_type_filter"), 0);

	if ((tfilter & ~(1 << NotificationDowntimeStart | 1 << NotificationDowntimeEnd | 1 << NotificationDowntimeRemoved |
	    1 << NotificationCustom | 1 << NotificationAcknowledgement | 1 << NotificationProblem | 1 << NotificationRecovery |
	    1 << NotificationFlappingStart | 1 << NotificationFlappingEnd)) != 0) {
		ConfigCompilerContext::GetInstance()->AddMessage(true, "Validation failed for " +
		    location + ": Type filter is invalid.");
	}
}

