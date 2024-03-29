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

#include "livestatus/servicestable.h"
#include "livestatus/hoststable.h"
#include "livestatus/endpointstable.h"
#include "icinga/service.h"
#include "icinga/checkcommand.h"
#include "icinga/eventcommand.h"
#include "icinga/timeperiod.h"
#include "icinga/macroprocessor.h"
#include "icinga/icingaapplication.h"
#include "icinga/compatutility.h"
#include "base/dynamictype.h"
#include "base/objectlock.h"
#include "base/convert.h"
#include "base/utility.h"
#include <boost/foreach.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/algorithm/string/replace.hpp>

using namespace icinga;

ServicesTable::ServicesTable(void)
{
	AddColumns(this);
}

void ServicesTable::AddColumns(Table *table, const String& prefix,
    const Column::ObjectAccessor& objectAccessor)
{
	table->AddColumn(prefix + "description", Column(&ServicesTable::ShortNameAccessor, objectAccessor));
	table->AddColumn(prefix + "display_name", Column(&ServicesTable::DisplayNameAccessor, objectAccessor));
	table->AddColumn(prefix + "check_command", Column(&ServicesTable::CheckCommandAccessor, objectAccessor));
	table->AddColumn(prefix + "check_command_expanded", Column(&ServicesTable::CheckCommandExpandedAccessor, objectAccessor));
	table->AddColumn(prefix + "event_handler", Column(&ServicesTable::EventHandlerAccessor, objectAccessor));
	table->AddColumn(prefix + "plugin_output", Column(&ServicesTable::PluginOutputAccessor, objectAccessor));
	table->AddColumn(prefix + "long_plugin_output", Column(&ServicesTable::LongPluginOutputAccessor, objectAccessor));
	table->AddColumn(prefix + "perf_data", Column(&ServicesTable::PerfDataAccessor, objectAccessor));
	table->AddColumn(prefix + "notification_period", Column(&ServicesTable::NotificationPeriodAccessor, objectAccessor));
	table->AddColumn(prefix + "check_period", Column(&ServicesTable::CheckPeriodAccessor, objectAccessor));
	table->AddColumn(prefix + "notes", Column(&ServicesTable::NotesAccessor, objectAccessor));
	table->AddColumn(prefix + "notes_expanded", Column(&ServicesTable::NotesExpandedAccessor, objectAccessor));
	table->AddColumn(prefix + "notes_url", Column(&ServicesTable::NotesUrlAccessor, objectAccessor));
	table->AddColumn(prefix + "notes_url_expanded", Column(&ServicesTable::NotesUrlExpandedAccessor, objectAccessor));
	table->AddColumn(prefix + "action_url", Column(&ServicesTable::ActionUrlAccessor, objectAccessor));
	table->AddColumn(prefix + "action_url_expanded", Column(&ServicesTable::ActionUrlExpandedAccessor, objectAccessor));
	table->AddColumn(prefix + "icon_image", Column(&ServicesTable::IconImageAccessor, objectAccessor));
	table->AddColumn(prefix + "icon_image_expanded", Column(&ServicesTable::IconImageExpandedAccessor, objectAccessor));
	table->AddColumn(prefix + "icon_image_alt", Column(&ServicesTable::IconImageAltAccessor, objectAccessor));
	table->AddColumn(prefix + "initial_state", Column(&Table::EmptyStringAccessor, objectAccessor));
	table->AddColumn(prefix + "max_check_attempts", Column(&ServicesTable::MaxCheckAttemptsAccessor, objectAccessor));
	table->AddColumn(prefix + "current_attempt", Column(&ServicesTable::CurrentAttemptAccessor, objectAccessor));
	table->AddColumn(prefix + "state", Column(&ServicesTable::StateAccessor, objectAccessor));
	table->AddColumn(prefix + "has_been_checked", Column(&ServicesTable::HasBeenCheckedAccessor, objectAccessor));
	table->AddColumn(prefix + "last_state", Column(&ServicesTable::LastStateAccessor, objectAccessor));
	table->AddColumn(prefix + "last_hard_state", Column(&ServicesTable::LastHardStateAccessor, objectAccessor));
	table->AddColumn(prefix + "state_type", Column(&ServicesTable::StateTypeAccessor, objectAccessor));
	table->AddColumn(prefix + "check_type", Column(&ServicesTable::CheckTypeAccessor, objectAccessor));
	table->AddColumn(prefix + "acknowledged", Column(&ServicesTable::AcknowledgedAccessor, objectAccessor));
	table->AddColumn(prefix + "acknowledgement_type", Column(&ServicesTable::AcknowledgementTypeAccessor, objectAccessor));
	table->AddColumn(prefix + "no_more_notifications", Column(&ServicesTable::NoMoreNotificationsAccessor, objectAccessor));
	table->AddColumn(prefix + "last_time_ok", Column(&ServicesTable::LastTimeOkAccessor, objectAccessor));
	table->AddColumn(prefix + "last_time_warning", Column(&ServicesTable::LastTimeWarningAccessor, objectAccessor));
	table->AddColumn(prefix + "last_time_critical", Column(&ServicesTable::LastTimeCriticalAccessor, objectAccessor));
	table->AddColumn(prefix + "last_time_unknown", Column(&ServicesTable::LastTimeUnknownAccessor, objectAccessor));
	table->AddColumn(prefix + "last_check", Column(&ServicesTable::LastCheckAccessor, objectAccessor));
	table->AddColumn(prefix + "next_check", Column(&ServicesTable::NextCheckAccessor, objectAccessor));
	table->AddColumn(prefix + "last_notification", Column(&ServicesTable::LastNotificationAccessor, objectAccessor));
	table->AddColumn(prefix + "next_notification", Column(&ServicesTable::NextNotificationAccessor, objectAccessor));
	table->AddColumn(prefix + "current_notification_number", Column(&ServicesTable::CurrentNotificationNumberAccessor, objectAccessor));
	table->AddColumn(prefix + "last_state_change", Column(&ServicesTable::LastStateChangeAccessor, objectAccessor));
	table->AddColumn(prefix + "last_hard_state_change", Column(&ServicesTable::LastHardStateChangeAccessor, objectAccessor));
	table->AddColumn(prefix + "scheduled_downtime_depth", Column(&ServicesTable::ScheduledDowntimeDepthAccessor, objectAccessor));
	table->AddColumn(prefix + "is_flapping", Column(&ServicesTable::IsFlappingAccessor, objectAccessor));
	table->AddColumn(prefix + "checks_enabled", Column(&ServicesTable::ChecksEnabledAccessor, objectAccessor));
	table->AddColumn(prefix + "accept_passive_checks", Column(&ServicesTable::AcceptPassiveChecksAccessor, objectAccessor));
	table->AddColumn(prefix + "event_handler_enabled", Column(&ServicesTable::EventHandlerEnabledAccessor, objectAccessor));
	table->AddColumn(prefix + "notifications_enabled", Column(&ServicesTable::NotificationsEnabledAccessor, objectAccessor));
	table->AddColumn(prefix + "process_performance_data", Column(&ServicesTable::ProcessPerformanceDataAccessor, objectAccessor));
	table->AddColumn(prefix + "is_executing", Column(&Table::ZeroAccessor, objectAccessor));
	table->AddColumn(prefix + "active_checks_enabled", Column(&ServicesTable::ActiveChecksEnabledAccessor, objectAccessor));
	table->AddColumn(prefix + "check_options", Column(&ServicesTable::CheckOptionsAccessor, objectAccessor));
	table->AddColumn(prefix + "flap_detection_enabled", Column(&ServicesTable::FlapDetectionEnabledAccessor, objectAccessor));
	table->AddColumn(prefix + "check_freshness", Column(&ServicesTable::CheckFreshnessAccessor, objectAccessor));
	table->AddColumn(prefix + "obsess_over_service", Column(&Table::ZeroAccessor, objectAccessor));
	table->AddColumn(prefix + "modified_attributes", Column(&ServicesTable::ModifiedAttributesAccessor, objectAccessor));
	table->AddColumn(prefix + "modified_attributes_list", Column(&ServicesTable::ModifiedAttributesListAccessor, objectAccessor));
	table->AddColumn(prefix + "pnpgraph_present", Column(&Table::ZeroAccessor, objectAccessor));
	table->AddColumn(prefix + "staleness", Column(&ServicesTable::StalenessAccessor, objectAccessor));
	table->AddColumn(prefix + "check_interval", Column(&ServicesTable::CheckIntervalAccessor, objectAccessor));
	table->AddColumn(prefix + "retry_interval", Column(&ServicesTable::RetryIntervalAccessor, objectAccessor));
	table->AddColumn(prefix + "notification_interval", Column(&ServicesTable::NotificationIntervalAccessor, objectAccessor));
	table->AddColumn(prefix + "first_notification_delay", Column(&Table::EmptyStringAccessor, objectAccessor));
	table->AddColumn(prefix + "low_flap_threshold", Column(&ServicesTable::LowFlapThresholdAccessor, objectAccessor));
	table->AddColumn(prefix + "high_flap_threshold", Column(&ServicesTable::HighFlapThresholdAccessor, objectAccessor));
	table->AddColumn(prefix + "latency", Column(&ServicesTable::LatencyAccessor, objectAccessor));
	table->AddColumn(prefix + "execution_time", Column(&ServicesTable::ExecutionTimeAccessor, objectAccessor));
	table->AddColumn(prefix + "percent_state_change", Column(&ServicesTable::PercentStateChangeAccessor, objectAccessor));
	table->AddColumn(prefix + "in_check_period", Column(&ServicesTable::InCheckPeriodAccessor, objectAccessor));
	table->AddColumn(prefix + "in_notification_period", Column(&ServicesTable::InNotificationPeriodAccessor, objectAccessor));
	table->AddColumn(prefix + "contacts", Column(&ServicesTable::ContactsAccessor, objectAccessor));
	table->AddColumn(prefix + "downtimes", Column(&ServicesTable::DowntimesAccessor, objectAccessor));
	table->AddColumn(prefix + "downtimes_with_info", Column(&ServicesTable::DowntimesWithInfoAccessor, objectAccessor));
	table->AddColumn(prefix + "comments", Column(&ServicesTable::CommentsAccessor, objectAccessor));
	table->AddColumn(prefix + "comments_with_info", Column(&ServicesTable::CommentsWithInfoAccessor, objectAccessor));
	table->AddColumn(prefix + "comments_with_extra_info", Column(&ServicesTable::CommentsWithExtraInfoAccessor, objectAccessor));
	table->AddColumn(prefix + "custom_variable_names", Column(&ServicesTable::CustomVariableNamesAccessor, objectAccessor));
	table->AddColumn(prefix + "custom_variable_values", Column(&ServicesTable::CustomVariableValuesAccessor, objectAccessor));
	table->AddColumn(prefix + "custom_variables", Column(&ServicesTable::CustomVariablesAccessor, objectAccessor));
	table->AddColumn(prefix + "groups", Column(&ServicesTable::GroupsAccessor, objectAccessor));
	table->AddColumn(prefix + "contact_groups", Column(&ServicesTable::ContactGroupsAccessor, objectAccessor));
	table->AddColumn(prefix + "check_source", Column(&ServicesTable::CheckSourceAccessor, objectAccessor));

	HostsTable::AddColumns(table, "host_", boost::bind(&ServicesTable::HostAccessor, _1, objectAccessor));
}

String ServicesTable::GetName(void) const
{
	return "services";
}

void ServicesTable::FetchRows(const AddRowFunction& addRowFn)
{
	BOOST_FOREACH(const Service::Ptr& service, DynamicType::GetObjects<Service>()) {
		addRowFn(service);
	}
}

Object::Ptr ServicesTable::HostAccessor(const Value& row, const Column::ObjectAccessor& parentObjectAccessor)
{
	Value service;

	if (parentObjectAccessor)
		service = parentObjectAccessor(row);
	else
		service = row;

	Service::Ptr svc = static_cast<Service::Ptr>(service);

	if (!svc)
		return Object::Ptr();

	return svc->GetHost();
}

Value ServicesTable::ShortNameAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetShortName();
}

Value ServicesTable::DisplayNameAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetDisplayName();
}

Value ServicesTable::CheckCommandAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	CheckCommand::Ptr checkcommand = service->GetCheckCommand();

	if (checkcommand)
		return checkcommand->GetName(); /* this is the name without '!' args */

	return Empty;
}

Value ServicesTable::CheckCommandExpandedAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	CheckCommand::Ptr checkcommand = service->GetCheckCommand();

	if (checkcommand)
		return checkcommand->GetName(); /* this is the name without '!' args */

	return Empty;
}

Value ServicesTable::EventHandlerAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	EventCommand::Ptr eventcommand = service->GetEventCommand();

	if (eventcommand)
		return eventcommand->GetName();

	return Empty;
}

Value ServicesTable::PluginOutputAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	String output;
	CheckResult::Ptr cr = service->GetLastCheckResult();

	if (cr)
		output = CompatUtility::GetCheckResultOutput(cr);

	return output;
}

Value ServicesTable::LongPluginOutputAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	String long_output;
	CheckResult::Ptr cr = service->GetLastCheckResult();

	if (cr)
		long_output = CompatUtility::GetCheckResultLongOutput(cr);

	return long_output;
}

Value ServicesTable::PerfDataAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	String perfdata;
	CheckResult::Ptr cr = service->GetLastCheckResult();

	if (cr)
		perfdata = CompatUtility::GetCheckResultPerfdata(cr);

	return perfdata;
}

Value ServicesTable::NotificationPeriodAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableNotificationNotificationPeriod(service);
}

Value ServicesTable::CheckPeriodAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableCheckPeriod(service);
}

Value ServicesTable::NotesAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetNotes();
}

Value ServicesTable::NotesExpandedAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	MacroProcessor::ResolverList resolvers;
	resolvers.push_back(std::make_pair("service", service));
	resolvers.push_back(std::make_pair("host", service->GetHost()));
	resolvers.push_back(std::make_pair("icinga", IcingaApplication::GetInstance()));

	return MacroProcessor::ResolveMacros(service->GetNotes(), resolvers);
}

Value ServicesTable::NotesUrlAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetNotesUrl();
}

Value ServicesTable::NotesUrlExpandedAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	MacroProcessor::ResolverList resolvers;
	resolvers.push_back(std::make_pair("service", service));
	resolvers.push_back(std::make_pair("host", service->GetHost()));
	resolvers.push_back(std::make_pair("icinga", IcingaApplication::GetInstance()));

	return MacroProcessor::ResolveMacros(service->GetNotesUrl(), resolvers);
}

Value ServicesTable::ActionUrlAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetActionUrl();
}

Value ServicesTable::ActionUrlExpandedAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	MacroProcessor::ResolverList resolvers;
	resolvers.push_back(std::make_pair("service", service));
	resolvers.push_back(std::make_pair("host", service->GetHost()));
	resolvers.push_back(std::make_pair("icinga", IcingaApplication::GetInstance()));

	return MacroProcessor::ResolveMacros(service->GetActionUrl(), resolvers);
}

Value ServicesTable::IconImageAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetIconImage();
}

Value ServicesTable::IconImageExpandedAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	MacroProcessor::ResolverList resolvers;
	resolvers.push_back(std::make_pair("service", service));
	resolvers.push_back(std::make_pair("host", service->GetHost()));
	resolvers.push_back(std::make_pair("icinga", IcingaApplication::GetInstance()));

	return MacroProcessor::ResolveMacros(service->GetIconImage(), resolvers);
}

Value ServicesTable::IconImageAltAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetIconImageAlt();
}

Value ServicesTable::MaxCheckAttemptsAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetMaxCheckAttempts();
}

Value ServicesTable::CurrentAttemptAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetCheckAttempt();
}

Value ServicesTable::StateAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetState();
}

Value ServicesTable::HasBeenCheckedAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableHasBeenChecked(service);
}

Value ServicesTable::LastStateAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetLastState();
}

Value ServicesTable::LastHardStateAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetLastHardState();
}

Value ServicesTable::StateTypeAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetStateType();
}

Value ServicesTable::CheckTypeAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableCheckType(service);
}

Value ServicesTable::AcknowledgedAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	ObjectLock olock(service);
	return CompatUtility::GetCheckableIsAcknowledged(service);
}

Value ServicesTable::AcknowledgementTypeAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	ObjectLock olock(service);
	return CompatUtility::GetCheckableAcknowledgementType(service);
}

Value ServicesTable::NoMoreNotificationsAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

        return CompatUtility::GetCheckableNoMoreNotifications(service);
}

Value ServicesTable::LastTimeOkAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return static_cast<int>(service->GetLastStateOK());
}

Value ServicesTable::LastTimeWarningAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return static_cast<int>(service->GetLastStateWarning());
}

Value ServicesTable::LastTimeCriticalAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return static_cast<int>(service->GetLastStateCritical());
}

Value ServicesTable::LastTimeUnknownAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return static_cast<int>(service->GetLastStateUnknown());
}

Value ServicesTable::LastCheckAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return static_cast<int>(service->GetLastCheck());
}

Value ServicesTable::NextCheckAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return static_cast<int>(service->GetNextCheck());
}

Value ServicesTable::LastNotificationAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableNotificationLastNotification(service);
}

Value ServicesTable::NextNotificationAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableNotificationNextNotification(service);
}

Value ServicesTable::CurrentNotificationNumberAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableNotificationNotificationNumber(service);
}

Value ServicesTable::LastStateChangeAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return static_cast<int>(service->GetLastStateChange());
}

Value ServicesTable::LastHardStateChangeAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return static_cast<int>(service->GetLastHardStateChange());
}

Value ServicesTable::ScheduledDowntimeDepthAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetDowntimeDepth();
}

Value ServicesTable::IsFlappingAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->IsFlapping();
}

Value ServicesTable::ChecksEnabledAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableActiveChecksEnabled(service);
}

Value ServicesTable::AcceptPassiveChecksAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckablePassiveChecksEnabled(service);
}

Value ServicesTable::EventHandlerEnabledAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableEventHandlerEnabled(service);
}

Value ServicesTable::NotificationsEnabledAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableNotificationsEnabled(service);
}

Value ServicesTable::ProcessPerformanceDataAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableProcessPerformanceData(service);
}

Value ServicesTable::ActiveChecksEnabledAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableActiveChecksEnabled(service);
}

Value ServicesTable::CheckOptionsAccessor(const Value& row)
{
	/* TODO - forcexec, freshness, orphan, none */
	return Empty;
}

Value ServicesTable::FlapDetectionEnabledAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableFlapDetectionEnabled(service);
}

Value ServicesTable::CheckFreshnessAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableFreshnessChecksEnabled(service);
}

Value ServicesTable::ModifiedAttributesAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return service->GetModifiedAttributes();
}

Value ServicesTable::ModifiedAttributesListAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetModifiedAttributesList(service);
}

Value ServicesTable::StalenessAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableStaleness(service);
}

Value ServicesTable::CheckIntervalAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableCheckInterval(service);
}

Value ServicesTable::RetryIntervalAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableRetryInterval(service);
}

Value ServicesTable::NotificationIntervalAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableNotificationNotificationInterval(service);
}

Value ServicesTable::LowFlapThresholdAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableLowFlapThreshold(service);
}

Value ServicesTable::HighFlapThresholdAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableHighFlapThreshold(service);
}

Value ServicesTable::LatencyAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return (Service::CalculateLatency(service->GetLastCheckResult()));
}

Value ServicesTable::ExecutionTimeAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return (Service::CalculateExecutionTime(service->GetLastCheckResult()));
}

Value ServicesTable::PercentStateChangeAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckablePercentStateChange(service);
}

Value ServicesTable::InCheckPeriodAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableInCheckPeriod(service);
}

Value ServicesTable::InNotificationPeriodAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	return CompatUtility::GetCheckableInNotificationPeriod(service);
}

Value ServicesTable::ContactsAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	Array::Ptr contact_names = make_shared<Array>();

	BOOST_FOREACH(const User::Ptr& user, CompatUtility::GetCheckableNotificationUsers(service)) {
		contact_names->Add(user->GetName());
	}

	return contact_names;
}

Value ServicesTable::DowntimesAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	Dictionary::Ptr downtimes = service->GetDowntimes();

	Array::Ptr ids = make_shared<Array>();

	ObjectLock olock(downtimes);

	String id;
	Downtime::Ptr downtime;
	BOOST_FOREACH(tie(id, downtime), downtimes) {

		if (!downtime)
			continue;

		if (downtime->IsExpired())
			continue;

		ids->Add(downtime->GetLegacyId());
	}

	return ids;
}

Value ServicesTable::DowntimesWithInfoAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	Dictionary::Ptr downtimes = service->GetDowntimes();

	Array::Ptr ids = make_shared<Array>();

	ObjectLock olock(downtimes);

	String id;
	Downtime::Ptr downtime;
	BOOST_FOREACH(tie(id, downtime), downtimes) {

		if (!downtime)
			continue;

		if (downtime->IsExpired())
			continue;

		Array::Ptr downtime_info = make_shared<Array>();
		downtime_info->Add(downtime->GetLegacyId());
		downtime_info->Add(downtime->GetAuthor());
		downtime_info->Add(downtime->GetComment());
		ids->Add(downtime_info);
	}

	return ids;
}

Value ServicesTable::CommentsAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	Dictionary::Ptr comments = service->GetComments();

	Array::Ptr ids = make_shared<Array>();

	ObjectLock olock(comments);

	String id;
	Comment::Ptr comment;
	BOOST_FOREACH(tie(id, comment), comments) {

		if (!comment)
			continue;

		if (comment->IsExpired())
			continue;

		ids->Add(comment->GetLegacyId());
	}

	return ids;
}

Value ServicesTable::CommentsWithInfoAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	Dictionary::Ptr comments = service->GetComments();

	Array::Ptr ids = make_shared<Array>();

	ObjectLock olock(comments);

	String id;
	Comment::Ptr comment;
	BOOST_FOREACH(tie(id, comment), comments) {

		if (!comment)
			continue;

		if (comment->IsExpired())
			continue;

		Array::Ptr comment_info = make_shared<Array>();
		comment_info->Add(comment->GetLegacyId());
		comment_info->Add(comment->GetAuthor());
		comment_info->Add(comment->GetText());
		ids->Add(comment_info);
	}

	return ids;
}

Value ServicesTable::CommentsWithExtraInfoAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	Dictionary::Ptr comments = service->GetComments();

	Array::Ptr ids = make_shared<Array>();

	ObjectLock olock(comments);

	String id;
	Comment::Ptr comment;
	BOOST_FOREACH(tie(id, comment), comments) {

		if (!comment)
			continue;

		if (comment->IsExpired())
			continue;

		Array::Ptr comment_info = make_shared<Array>();
		comment_info->Add(comment->GetLegacyId());
		comment_info->Add(comment->GetAuthor());
		comment_info->Add(comment->GetText());
		comment_info->Add(comment->GetEntryType());
		comment_info->Add(static_cast<int>(comment->GetEntryTime()));
		ids->Add(comment_info);
	}

	return ids;
}

Value ServicesTable::CustomVariableNamesAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	Dictionary::Ptr vars;

	{
		ObjectLock olock(service);
		vars = CompatUtility::GetCustomAttributeConfig(service);
	}

	if (!vars)
		return Empty;

	Array::Ptr cv = make_shared<Array>();

	ObjectLock olock(vars);
	BOOST_FOREACH(const Dictionary::Pair kv, vars) {
		cv->Add(kv.second);
	}

	return cv;
}

Value ServicesTable::CustomVariableValuesAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	Dictionary::Ptr vars;

	{
		ObjectLock olock(service);
		vars = CompatUtility::GetCustomAttributeConfig(service);
	}

	if (!vars)
		return Empty;

	Array::Ptr cv = make_shared<Array>();

	ObjectLock olock(vars);
	BOOST_FOREACH(const Dictionary::Pair& kv, vars) {
		cv->Add(kv.second);
	}

	return cv;
}

Value ServicesTable::CustomVariablesAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	Dictionary::Ptr vars;

	{
		ObjectLock olock(service);
		vars = CompatUtility::GetCustomAttributeConfig(service);
	}

	if (!vars)
		return Empty;

	Array::Ptr cv = make_shared<Array>();

	String key;
	Value value;
	BOOST_FOREACH(tie(key, value), vars) {
		Array::Ptr key_val = make_shared<Array>();
		key_val->Add(key);
		key_val->Add(value);
		cv->Add(key_val);
	}

	return cv;
}

Value ServicesTable::GroupsAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	Array::Ptr groups = service->GetGroups();

	if (!groups)
		return Empty;

	return groups;
}

Value ServicesTable::ContactGroupsAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	Array::Ptr contactgroup_names = make_shared<Array>();

	BOOST_FOREACH(const UserGroup::Ptr& usergroup, CompatUtility::GetCheckableNotificationUserGroups(service)) {
		contactgroup_names->Add(usergroup->GetName());
	}

	return contactgroup_names;
}

Value ServicesTable::CheckSourceAccessor(const Value& row)
{
	Service::Ptr service = static_cast<Service::Ptr>(row);

	if (!service)
		return Empty;

	CheckResult::Ptr cr = service->GetLastCheckResult();

	if (cr)
		return cr->GetCheckSource();

	return Empty;
}


