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

#include "icinga/externalcommandprocessor.h"
#include "icinga/host.h"
#include "icinga/service.h"
#include "icinga/user.h"
#include "icinga/hostgroup.h"
#include "icinga/servicegroup.h"
#include "icinga/pluginutility.h"
#include "icinga/icingaapplication.h"
#include "icinga/checkcommand.h"
#include "icinga/eventcommand.h"
#include "icinga/notificationcommand.h"
#include "base/convert.h"
#include "base/logger_fwd.h"
#include "base/objectlock.h"
#include "base/application.h"
#include "base/utility.h"
#include "base/exception.h"
#include <fstream>
#include <boost/algorithm/string/classification.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace icinga;

static boost::once_flag l_InitializeOnce = BOOST_ONCE_INIT;
static boost::mutex l_Mutex;

typedef boost::function<void (double time, const std::vector<String>& arguments)> ExternalCommandCallback;

struct ExternalCommandInfo
{
	ExternalCommandCallback Callback;
	size_t MinArgs;
	size_t MaxArgs;
};

static std::map<String, ExternalCommandInfo> l_Commands;

boost::signals2::signal<void (double, const String&, const std::vector<String>&)> ExternalCommandProcessor::OnNewExternalCommand;

static void RegisterCommand(const String& command, const ExternalCommandCallback& callback, size_t minArgs = 0, size_t maxArgs = -1)
{
	boost::mutex::scoped_lock lock(l_Mutex);
	ExternalCommandInfo eci;
	eci.Callback = callback;
	eci.MinArgs = minArgs;
	eci.MaxArgs = (maxArgs == -1) ? minArgs : maxArgs;
	l_Commands[command] = eci;
}

void ExternalCommandProcessor::Execute(const String& line)
{
	if (line.IsEmpty())
		return;

	if (line[0] != '[')
		BOOST_THROW_EXCEPTION(std::invalid_argument("Missing timestamp in command: " + line));

	size_t pos = line.FindFirstOf("]");

	if (pos == String::NPos)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Missing timestamp in command: " + line));

	String timestamp = line.SubStr(1, pos - 1);
	String args = line.SubStr(pos + 2, String::NPos);

	double ts = Convert::ToDouble(timestamp);

	if (ts == 0)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Invalid timestamp in command: " + line));

	std::vector<String> argv;
	boost::algorithm::split(argv, args, boost::is_any_of(";"));

	if (argv.empty())
		BOOST_THROW_EXCEPTION(std::invalid_argument("Missing arguments in command: " + line));

	std::vector<String> argvExtra(argv.begin() + 1, argv.end());
	Execute(ts, argv[0], argvExtra);
}

void ExternalCommandProcessor::Execute(double time, const String& command, const std::vector<String>& arguments)
{
	boost::call_once(l_InitializeOnce, &ExternalCommandProcessor::Initialize);

	ExternalCommandInfo eci;

	{
		boost::mutex::scoped_lock lock(l_Mutex);

		std::map<String, ExternalCommandInfo>::iterator it;
		it = l_Commands.find(command);

		if (it == l_Commands.end())
			BOOST_THROW_EXCEPTION(std::invalid_argument("The external command '" + command + "' does not exist."));

		eci = it->second;
	}

	if (arguments.size() < eci.MinArgs)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Expected " + Convert::ToString(eci.MinArgs) + " arguments"));

	size_t argnum = std::min(arguments.size(), eci.MaxArgs);

	std::vector<String> realArguments;
	realArguments.resize(argnum);

	if (argnum > 0) {
		std::copy(arguments.begin(), arguments.begin() + argnum - 1, realArguments.begin());
	
		String last_argument;
		for (int i = argnum - 1; i < arguments.size(); i++) {
			if (!last_argument.IsEmpty())
				last_argument += ";";

			last_argument += arguments[i];
		}

		realArguments[argnum - 1] = last_argument;
	}

	OnNewExternalCommand(time, command, realArguments);

	eci.Callback(time, realArguments);
}

void ExternalCommandProcessor::Initialize(void)
{
	RegisterCommand("PROCESS_HOST_CHECK_RESULT", &ExternalCommandProcessor::ProcessHostCheckResult, 3);
	RegisterCommand("PROCESS_SERVICE_CHECK_RESULT", &ExternalCommandProcessor::ProcessServiceCheckResult, 4);
	RegisterCommand("SCHEDULE_HOST_CHECK", &ExternalCommandProcessor::ScheduleHostCheck, 2);
	RegisterCommand("SCHEDULE_FORCED_HOST_CHECK", &ExternalCommandProcessor::ScheduleForcedHostCheck, 2);
	RegisterCommand("SCHEDULE_SVC_CHECK", &ExternalCommandProcessor::ScheduleSvcCheck, 3);
	RegisterCommand("SCHEDULE_FORCED_SVC_CHECK", &ExternalCommandProcessor::ScheduleForcedSvcCheck, 3);
	RegisterCommand("ENABLE_HOST_CHECK", &ExternalCommandProcessor::EnableHostCheck, 1);
	RegisterCommand("DISABLE_HOST_CHECK", &ExternalCommandProcessor::DisableHostCheck, 1);
	RegisterCommand("ENABLE_SVC_CHECK", &ExternalCommandProcessor::EnableSvcCheck, 2);
	RegisterCommand("DISABLE_SVC_CHECK", &ExternalCommandProcessor::DisableSvcCheck, 2);
	RegisterCommand("SHUTDOWN_PROCESS", &ExternalCommandProcessor::ShutdownProcess);
	RegisterCommand("RESTART_PROCESS", &ExternalCommandProcessor::RestartProcess);
	RegisterCommand("SCHEDULE_FORCED_HOST_SVC_CHECKS", &ExternalCommandProcessor::ScheduleForcedHostSvcChecks, 2);
	RegisterCommand("SCHEDULE_HOST_SVC_CHECKS", &ExternalCommandProcessor::ScheduleHostSvcChecks, 2);
	RegisterCommand("ENABLE_HOST_SVC_CHECKS", &ExternalCommandProcessor::EnableHostSvcChecks, 1);
	RegisterCommand("DISABLE_HOST_SVC_CHECKS", &ExternalCommandProcessor::DisableHostSvcChecks, 1);
	RegisterCommand("ACKNOWLEDGE_SVC_PROBLEM", &ExternalCommandProcessor::AcknowledgeSvcProblem, 7);
	RegisterCommand("ACKNOWLEDGE_SVC_PROBLEM_EXPIRE", &ExternalCommandProcessor::AcknowledgeSvcProblemExpire, 8);
	RegisterCommand("REMOVE_SVC_ACKNOWLEDGEMENT", &ExternalCommandProcessor::RemoveSvcAcknowledgement, 2);
	RegisterCommand("ACKNOWLEDGE_HOST_PROBLEM", &ExternalCommandProcessor::AcknowledgeHostProblem, 6);
	RegisterCommand("ACKNOWLEDGE_HOST_PROBLEM_EXPIRE", &ExternalCommandProcessor::AcknowledgeHostProblemExpire, 7);
	RegisterCommand("REMOVE_HOST_ACKNOWLEDGEMENT", &ExternalCommandProcessor::RemoveHostAcknowledgement, 1);
	RegisterCommand("DISABLE_HOST_FLAP_DETECTION", &ExternalCommandProcessor::DisableHostFlapping, 1);
	RegisterCommand("ENABLE_HOST_FLAP_DETECTION", &ExternalCommandProcessor::EnableHostFlapping, 1);
	RegisterCommand("DISABLE_SVC_FLAP_DETECTION", &ExternalCommandProcessor::DisableSvcFlapping, 2);
	RegisterCommand("ENABLE_SVC_FLAP_DETECTION", &ExternalCommandProcessor::EnableSvcFlapping, 2);
	RegisterCommand("ENABLE_HOSTGROUP_SVC_CHECKS", &ExternalCommandProcessor::EnableHostgroupSvcChecks, 1);
	RegisterCommand("DISABLE_HOSTGROUP_SVC_CHECKS", &ExternalCommandProcessor::DisableHostgroupSvcChecks, 1);
	RegisterCommand("ENABLE_SERVICEGROUP_SVC_CHECKS", &ExternalCommandProcessor::EnableServicegroupSvcChecks, 1);
	RegisterCommand("DISABLE_SERVICEGROUP_SVC_CHECKS", &ExternalCommandProcessor::DisableServicegroupSvcChecks, 1);
	RegisterCommand("ENABLE_PASSIVE_HOST_CHECKS", &ExternalCommandProcessor::EnablePassiveHostChecks, 1);
	RegisterCommand("DISABLE_PASSIVE_HOST_CHECKS", &ExternalCommandProcessor::DisablePassiveHostChecks, 1);
	RegisterCommand("ENABLE_PASSIVE_SVC_CHECKS", &ExternalCommandProcessor::EnablePassiveSvcChecks, 2);
	RegisterCommand("DISABLE_PASSIVE_SVC_CHECKS", &ExternalCommandProcessor::DisablePassiveSvcChecks, 2);
	RegisterCommand("ENABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS", &ExternalCommandProcessor::EnableServicegroupPassiveSvcChecks, 1);
	RegisterCommand("DISABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS", &ExternalCommandProcessor::DisableServicegroupPassiveSvcChecks, 1);
	RegisterCommand("ENABLE_HOSTGROUP_PASSIVE_SVC_CHECKS", &ExternalCommandProcessor::EnableHostgroupPassiveSvcChecks, 1);
	RegisterCommand("DISABLE_HOSTGROUP_PASSIVE_SVC_CHECKS", &ExternalCommandProcessor::DisableHostgroupPassiveSvcChecks, 1);
	RegisterCommand("PROCESS_FILE", &ExternalCommandProcessor::ProcessFile, 2);
	RegisterCommand("SCHEDULE_SVC_DOWNTIME", &ExternalCommandProcessor::ScheduleSvcDowntime, 9);
	RegisterCommand("DEL_SVC_DOWNTIME", &ExternalCommandProcessor::DelSvcDowntime, 1);
	RegisterCommand("SCHEDULE_HOST_DOWNTIME", &ExternalCommandProcessor::ScheduleHostDowntime, 8);
	RegisterCommand("DEL_HOST_DOWNTIME", &ExternalCommandProcessor::DelHostDowntime, 1);
	RegisterCommand("SCHEDULE_HOST_SVC_DOWNTIME", &ExternalCommandProcessor::ScheduleHostSvcDowntime, 8);
	RegisterCommand("SCHEDULE_HOSTGROUP_HOST_DOWNTIME", &ExternalCommandProcessor::ScheduleHostgroupHostDowntime, 8);
	RegisterCommand("SCHEDULE_HOSTGROUP_SVC_DOWNTIME", &ExternalCommandProcessor::ScheduleHostgroupSvcDowntime, 8);
	RegisterCommand("SCHEDULE_SERVICEGROUP_HOST_DOWNTIME", &ExternalCommandProcessor::ScheduleServicegroupHostDowntime, 8);
	RegisterCommand("SCHEDULE_SERVICEGROUP_SVC_DOWNTIME", &ExternalCommandProcessor::ScheduleServicegroupSvcDowntime, 8);
	RegisterCommand("ADD_HOST_COMMENT", &ExternalCommandProcessor::AddHostComment, 4);
	RegisterCommand("DEL_HOST_COMMENT", &ExternalCommandProcessor::DelHostComment, 1);
	RegisterCommand("ADD_SVC_COMMENT", &ExternalCommandProcessor::AddSvcComment, 5);
	RegisterCommand("DEL_SVC_COMMENT", &ExternalCommandProcessor::DelSvcComment, 1);
	RegisterCommand("DEL_ALL_HOST_COMMENTS", &ExternalCommandProcessor::DelAllHostComments, 1);
	RegisterCommand("DEL_ALL_SVC_COMMENTS", &ExternalCommandProcessor::DelAllSvcComments, 2);
	RegisterCommand("SEND_CUSTOM_HOST_NOTIFICATION", &ExternalCommandProcessor::SendCustomHostNotification, 4);
	RegisterCommand("SEND_CUSTOM_SVC_NOTIFICATION", &ExternalCommandProcessor::SendCustomSvcNotification, 5);
	RegisterCommand("DELAY_HOST_NOTIFICATION", &ExternalCommandProcessor::DelayHostNotification, 2);
	RegisterCommand("DELAY_SVC_NOTIFICATION", &ExternalCommandProcessor::DelaySvcNotification, 3);
	RegisterCommand("ENABLE_HOST_NOTIFICATIONS", &ExternalCommandProcessor::EnableHostNotifications, 1);
	RegisterCommand("DISABLE_HOST_NOTIFICATIONS", &ExternalCommandProcessor::DisableHostNotifications, 1);
	RegisterCommand("ENABLE_SVC_NOTIFICATIONS", &ExternalCommandProcessor::EnableSvcNotifications, 2);
	RegisterCommand("DISABLE_SVC_NOTIFICATIONS", &ExternalCommandProcessor::DisableSvcNotifications, 2);
	RegisterCommand("DISABLE_HOSTGROUP_HOST_CHECKS", &ExternalCommandProcessor::DisableHostgroupHostChecks, 1);
	RegisterCommand("DISABLE_HOSTGROUP_PASSIVE_HOST_CHECKS", &ExternalCommandProcessor::DisableHostgroupPassiveHostChecks, 1);
	RegisterCommand("DISABLE_SERVICEGROUP_HOST_CHECKS", &ExternalCommandProcessor::DisableServicegroupHostChecks, 1);
	RegisterCommand("DISABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS", &ExternalCommandProcessor::DisableServicegroupPassiveHostChecks, 1);
	RegisterCommand("ENABLE_HOSTGROUP_HOST_CHECKS", &ExternalCommandProcessor::EnableHostgroupHostChecks, 1);
	RegisterCommand("ENABLE_HOSTGROUP_PASSIVE_HOST_CHECKS", &ExternalCommandProcessor::EnableHostgroupPassiveHostChecks, 1);
	RegisterCommand("ENABLE_SERVICEGROUP_HOST_CHECKS", &ExternalCommandProcessor::EnableServicegroupHostChecks, 1);
	RegisterCommand("ENABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS", &ExternalCommandProcessor::EnableServicegroupPassiveHostChecks, 1);
	RegisterCommand("ENABLE_NOTIFICATIONS", &ExternalCommandProcessor::EnableNotifications);
	RegisterCommand("DISABLE_NOTIFICATIONS", &ExternalCommandProcessor::DisableNotifications);
	RegisterCommand("ENABLE_FLAP_DETECTION", &ExternalCommandProcessor::EnableFlapDetection);
	RegisterCommand("DISABLE_FLAP_DETECTION", &ExternalCommandProcessor::DisableFlapDetection);
	RegisterCommand("ENABLE_EVENT_HANDLERS", &ExternalCommandProcessor::EnableEventHandlers);
	RegisterCommand("DISABLE_EVENT_HANDLERS", &ExternalCommandProcessor::DisableEventHandlers);
	RegisterCommand("ENABLE_PERFORMANCE_DATA", &ExternalCommandProcessor::EnablePerformanceData);
	RegisterCommand("DISABLE_PERFORMANCE_DATA", &ExternalCommandProcessor::DisablePerformanceData);
	RegisterCommand("START_EXECUTING_SVC_CHECKS", &ExternalCommandProcessor::StartExecutingSvcChecks);
	RegisterCommand("STOP_EXECUTING_SVC_CHECKS", &ExternalCommandProcessor::StopExecutingSvcChecks);
	RegisterCommand("START_EXECUTING_HOST_CHECKS", &ExternalCommandProcessor::StartExecutingHostChecks);
	RegisterCommand("STOP_EXECUTING_HOST_CHECKS", &ExternalCommandProcessor::StopExecutingHostChecks);
	RegisterCommand("CHANGE_SVC_MODATTR", &ExternalCommandProcessor::ChangeSvcModattr, 3);
	RegisterCommand("CHANGE_HOST_MODATTR", &ExternalCommandProcessor::ChangeHostModattr, 2);
	RegisterCommand("CHANGE_USER_MODATTR", &ExternalCommandProcessor::ChangeUserModattr, 2);
	RegisterCommand("CHANGE_CHECKCOMMAND_MODATTR", &ExternalCommandProcessor::ChangeCheckcommandModattr, 2);
	RegisterCommand("CHANGE_EVENTCOMMAND_MODATTR", &ExternalCommandProcessor::ChangeEventcommandModattr, 2);
	RegisterCommand("CHANGE_NOTIFICATIONCOMMAND_MODATTR", &ExternalCommandProcessor::ChangeNotificationcommandModattr, 2);
	RegisterCommand("CHANGE_NORMAL_SVC_CHECK_INTERVAL", &ExternalCommandProcessor::ChangeNormalSvcCheckInterval, 3);
	RegisterCommand("CHANGE_NORMAL_HOST_CHECK_INTERVAL", &ExternalCommandProcessor::ChangeNormalHostCheckInterval, 2);
	RegisterCommand("CHANGE_RETRY_SVC_CHECK_INTERVAL", &ExternalCommandProcessor::ChangeRetrySvcCheckInterval, 3);
	RegisterCommand("CHANGE_RETRY_HOST_CHECK_INTERVAL", &ExternalCommandProcessor::ChangeRetryHostCheckInterval, 2);
	RegisterCommand("ENABLE_HOST_EVENT_HANDLER", &ExternalCommandProcessor::EnableHostEventHandler, 1);
	RegisterCommand("DISABLE_HOST_EVENT_HANDLER", &ExternalCommandProcessor::DisableHostEventHandler, 1);
	RegisterCommand("ENABLE_SVC_EVENT_HANDLER", &ExternalCommandProcessor::EnableSvcEventHandler, 2);
	RegisterCommand("DISABLE_SVC_EVENT_HANDLER", &ExternalCommandProcessor::DisableSvcEventHandler, 2);
	RegisterCommand("CHANGE_HOST_EVENT_HANDLER", &ExternalCommandProcessor::ChangeHostEventHandler, 2);
	RegisterCommand("CHANGE_SVC_EVENT_HANDLER", &ExternalCommandProcessor::ChangeSvcEventHandler, 3);
	RegisterCommand("CHANGE_HOST_CHECK_COMMAND", &ExternalCommandProcessor::ChangeHostCheckCommand, 2);
	RegisterCommand("CHANGE_SVC_CHECK_COMMAND", &ExternalCommandProcessor::ChangeSvcCheckCommand, 3);
	RegisterCommand("CHANGE_MAX_HOST_CHECK_ATTEMPTS", &ExternalCommandProcessor::ChangeMaxHostCheckAttempts, 2);
	RegisterCommand("CHANGE_MAX_SVC_CHECK_ATTEMPTS", &ExternalCommandProcessor::ChangeMaxSvcCheckAttempts, 3);
	RegisterCommand("CHANGE_HOST_CHECK_TIMEPERIOD", &ExternalCommandProcessor::ChangeHostCheckTimeperiod, 2);
	RegisterCommand("CHANGE_SVC_CHECK_TIMEPERIOD", &ExternalCommandProcessor::ChangeSvcCheckTimeperiod, 3);
	RegisterCommand("CHANGE_CUSTOM_HOST_VAR", &ExternalCommandProcessor::ChangeCustomHostVar, 3);
	RegisterCommand("CHANGE_CUSTOM_SVC_VAR", &ExternalCommandProcessor::ChangeCustomSvcVar, 4);
	RegisterCommand("CHANGE_CUSTOM_USER_VAR", &ExternalCommandProcessor::ChangeCustomUserVar, 3);
	RegisterCommand("CHANGE_CUSTOM_CHECKCOMMAND_VAR", &ExternalCommandProcessor::ChangeCustomCheckcommandVar, 3);
	RegisterCommand("CHANGE_CUSTOM_EVENTCOMMAND_VAR", &ExternalCommandProcessor::ChangeCustomEventcommandVar, 3);
	RegisterCommand("CHANGE_CUSTOM_NOTIFICATIONCOMMAND_VAR", &ExternalCommandProcessor::ChangeCustomNotificationcommandVar, 3);

	RegisterCommand("ENABLE_HOSTGROUP_HOST_NOTIFICATIONS", &ExternalCommandProcessor::EnableHostgroupHostNotifications, 1);
	RegisterCommand("ENABLE_HOSTGROUP_SVC_NOTIFICATIONS", &ExternalCommandProcessor::EnableHostgroupSvcNotifications, 1);
	RegisterCommand("DISABLE_HOSTGROUP_HOST_NOTIFICATIONS", &ExternalCommandProcessor::DisableHostgroupHostNotifications, 1);
	RegisterCommand("DISABLE_HOSTGROUP_SVC_NOTIFICATIONS", &ExternalCommandProcessor::DisableHostgroupSvcNotifications, 1);
	RegisterCommand("ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS", &ExternalCommandProcessor::EnableServicegroupHostNotifications, 1);
	RegisterCommand("DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS", &ExternalCommandProcessor::DisableServicegroupHostNotifications, 1);
	RegisterCommand("ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS", &ExternalCommandProcessor::EnableServicegroupSvcNotifications, 1);
	RegisterCommand("DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS", &ExternalCommandProcessor::DisableServicegroupSvcNotifications, 1);
}

void ExternalCommandProcessor::ProcessHostCheckResult(double time, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot process passive host check result for non-existent host '" + arguments[0] + "'"));

	if (!host->GetEnablePassiveChecks())
		BOOST_THROW_EXCEPTION(std::invalid_argument("Got passive check result for host '" + arguments[0] + "' which has passive checks disabled."));

	int exitStatus = Convert::ToDouble(arguments[1]);
	CheckResult::Ptr result = make_shared<CheckResult>();
	std::pair<String, Value> co = PluginUtility::ParseCheckOutput(arguments[2]);
	result->SetOutput(co.first);
	result->SetPerformanceData(co.second);

	ServiceState state;

	if (exitStatus == 0)
		state = ServiceOK;
	else if (exitStatus == 1)
		state = ServiceCritical;
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Invalid status code: " + arguments[1]));

	result->SetState(state);

	result->SetScheduleStart(time);
	result->SetScheduleEnd(time);
	result->SetExecutionStart(time);
	result->SetExecutionEnd(time);
	result->SetActive(false);

	Log(LogInformation, "icinga", "Processing passive check result for host '" + arguments[0] + "'");
	host->ProcessCheckResult(result);

	{
		ObjectLock olock(host);

		/* Reschedule the next check. The side effect of this is that for as long
		 * as we receive passive results for a service we won't execute any
		 * active checks. */
		host->SetNextCheck(Utility::GetTime() + host->GetCheckInterval());
	}
}

void ExternalCommandProcessor::ProcessServiceCheckResult(double time, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot process passive service check result for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	if (!service->GetEnablePassiveChecks())
		BOOST_THROW_EXCEPTION(std::invalid_argument("Got passive check result for service '" + arguments[1] + "' which has passive checks disabled."));

	int exitStatus = Convert::ToDouble(arguments[2]);
	CheckResult::Ptr result = make_shared<CheckResult>();
	std::pair<String, Value> co = PluginUtility::ParseCheckOutput(arguments[3]);
	result->SetOutput(co.first);
	result->SetPerformanceData(co.second);
	result->SetState(PluginUtility::ExitStatusToState(exitStatus));

	result->SetScheduleStart(time);
	result->SetScheduleEnd(time);
	result->SetExecutionStart(time);
	result->SetExecutionEnd(time);
	result->SetActive(false);

	Log(LogInformation, "icinga", "Processing passive check result for service '" + arguments[1] + "'");
	service->ProcessCheckResult(result);

	{
		ObjectLock olock(service);

		/* Reschedule the next check. The side effect of this is that for as long
		 * as we receive passive results for a service we won't execute any
		 * active checks. */
		service->SetNextCheck(Utility::GetTime() + service->GetCheckInterval());
	}
}

void ExternalCommandProcessor::ScheduleHostCheck(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot reschedule host check for non-existent host '" + arguments[0] + "'"));

	double planned_check = Convert::ToDouble(arguments[1]);

	if (planned_check > host->GetNextCheck()) {
		Log(LogInformation, "icinga", "Ignoring reschedule request for host '" +
		    arguments[0] + "' (next check is already sooner than requested check time)");
		return;
	}

	Log(LogInformation, "icinga", "Rescheduling next check for host '" + arguments[0] + "'");

	if (planned_check < Utility::GetTime())
		planned_check = Utility::GetTime();

	{
		ObjectLock olock(host);

		host->SetNextCheck(planned_check);
	}
}

void ExternalCommandProcessor::ScheduleForcedHostCheck(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot reschedule forced host check for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Rescheduling next check for host '" + arguments[0] + "'");

	{
		ObjectLock olock(host);

		host->SetForceNextCheck(true);
		host->SetNextCheck(Convert::ToDouble(arguments[1]));
	}
}

void ExternalCommandProcessor::ScheduleSvcCheck(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot reschedule service check for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	double planned_check = Convert::ToDouble(arguments[2]);

	if (planned_check > service->GetNextCheck()) {
		Log(LogInformation, "icinga", "Ignoring reschedule request for service '" +
		    arguments[1] + "' (next check is already sooner than requested check time)");
		return;
	}

	Log(LogInformation, "icinga", "Rescheduling next check for service '" + arguments[1] + "'");

	if (planned_check < Utility::GetTime())
		planned_check = Utility::GetTime();

	{
		ObjectLock olock(service);

		service->SetNextCheck(planned_check);
	}
}

void ExternalCommandProcessor::ScheduleForcedSvcCheck(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot reschedule forced service check for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Rescheduling next check for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetForceNextCheck(true);
		service->SetNextCheck(Convert::ToDouble(arguments[2]));
	}
}

void ExternalCommandProcessor::EnableHostCheck(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable host checks for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Enabling active checks for host '" + arguments[0] + "'");

	{
		ObjectLock olock(host);

		host->SetEnableActiveChecks(true);
	}
}

void ExternalCommandProcessor::DisableHostCheck(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable host check non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Disabling active checks for host '" + arguments[0] + "'");

	{
		ObjectLock olock(host);

		host->SetEnableActiveChecks(false);
	}
}

void ExternalCommandProcessor::EnableSvcCheck(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable service check for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Enabling active checks for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetEnableActiveChecks(true);
	}
}

void ExternalCommandProcessor::DisableSvcCheck(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable service check for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Disabling active checks for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetEnableActiveChecks(false);
	}
}

void ExternalCommandProcessor::ShutdownProcess(double, const std::vector<String>&)
{
	Log(LogInformation, "icinga", "Shutting down Icinga via external command.");
	Application::RequestShutdown();
}

void ExternalCommandProcessor::RestartProcess(double, const std::vector<String>&)
{
	Log(LogInformation, "icinga", "Restarting Icinga via external command.");
	Application::RequestRestart();
}

void ExternalCommandProcessor::ScheduleForcedHostSvcChecks(double, const std::vector<String>& arguments)
{
	double planned_check = Convert::ToDouble(arguments[1]);

	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot reschedule forced host service checks for non-existent host '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
		Log(LogInformation, "icinga", "Rescheduling next check for service '" + service->GetName() + "'");

		{
			ObjectLock olock(service);

			service->SetNextCheck(planned_check);
			service->SetForceNextCheck(true);
		}
	}
}

void ExternalCommandProcessor::ScheduleHostSvcChecks(double, const std::vector<String>& arguments)
{
	double planned_check = Convert::ToDouble(arguments[1]);

	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot reschedule host service checks for non-existent host '" + arguments[0] + "'"));

	if (planned_check < Utility::GetTime())
		planned_check = Utility::GetTime();

	BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
		if (planned_check > service->GetNextCheck()) {
			Log(LogInformation, "icinga", "Ignoring reschedule request for service '" +
			    service->GetName() + "' (next check is already sooner than requested check time)");
			continue;
		}

		Log(LogInformation, "icinga", "Rescheduling next check for service '" + service->GetName() + "'");

		{
			ObjectLock olock(service);

			service->SetNextCheck(planned_check);
		}
	}
}

void ExternalCommandProcessor::EnableHostSvcChecks(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable host service checks for non-existent host '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
		Log(LogInformation, "icinga", "Enabling active checks for service '" + service->GetName() + "'");
		service->SetEnableActiveChecks(true);
	}
}

void ExternalCommandProcessor::DisableHostSvcChecks(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable host service checks for non-existent host '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
		Log(LogInformation, "icinga", "Disabling active checks for service '" + service->GetName() + "'");

		{
			ObjectLock olock(service);

			service->SetEnableActiveChecks(false);
		}
	}
}

void ExternalCommandProcessor::AcknowledgeSvcProblem(double, const std::vector<String>& arguments)
{
	bool sticky = (Convert::ToLong(arguments[2]) == 2 ? true : false);

	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot acknowledge service problem for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	if (service->GetState() == ServiceOK)
		BOOST_THROW_EXCEPTION(std::invalid_argument("The service '" + arguments[1] + "' is OK."));

	Log(LogInformation, "icinga", "Setting acknowledgement for service '" + service->GetName() + "'");

	service->AddComment(CommentAcknowledgement, arguments[5], arguments[6], 0);
	service->AcknowledgeProblem(arguments[5], arguments[6], sticky ? AcknowledgementSticky : AcknowledgementNormal);
}

void ExternalCommandProcessor::AcknowledgeSvcProblemExpire(double, const std::vector<String>& arguments)
{
	bool sticky = (Convert::ToLong(arguments[2]) == 2 ? true : false);
	double timestamp = Convert::ToDouble(arguments[5]);

	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot acknowledge service problem with expire time for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	if (service->GetState() == ServiceOK)
		BOOST_THROW_EXCEPTION(std::invalid_argument("The service '" + arguments[1] + "' is OK."));

	Log(LogInformation, "icinga", "Setting timed acknowledgement for service '" + service->GetName() + "'");

	service->AddComment(CommentAcknowledgement, arguments[6], arguments[7], 0);
	service->AcknowledgeProblem(arguments[6], arguments[7], sticky ? AcknowledgementSticky : AcknowledgementNormal, timestamp);
}

void ExternalCommandProcessor::RemoveSvcAcknowledgement(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot remove service acknowledgement for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Removing acknowledgement for service '" + service->GetName() + "'");

	{
		ObjectLock olock(service);
		service->ClearAcknowledgement();
	}

	service->RemoveCommentsByType(CommentAcknowledgement);
}

void ExternalCommandProcessor::AcknowledgeHostProblem(double, const std::vector<String>& arguments)
{
	bool sticky = (Convert::ToLong(arguments[1]) == 2 ? true : false);

	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot acknowledge host problem for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Setting acknowledgement for host '" + host->GetName() + "'");

	if (host->GetState() == HostUp)
		BOOST_THROW_EXCEPTION(std::invalid_argument("The host '" + arguments[0] + "' is OK."));

	host->AddComment(CommentAcknowledgement, arguments[4], arguments[5], 0);
	host->AcknowledgeProblem(arguments[4], arguments[5], sticky ? AcknowledgementSticky : AcknowledgementNormal);
}

void ExternalCommandProcessor::AcknowledgeHostProblemExpire(double, const std::vector<String>& arguments)
{
	bool sticky = (Convert::ToLong(arguments[1]) == 2 ? true : false);
	double timestamp = Convert::ToDouble(arguments[4]);

	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot acknowledge host problem with expire time for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Setting timed acknowledgement for host '" + host->GetName() + "'");

	if (host->GetState() == HostUp)
		BOOST_THROW_EXCEPTION(std::invalid_argument("The host '" + arguments[0] + "' is OK."));

	host->AddComment(CommentAcknowledgement, arguments[5], arguments[6], 0);
	host->AcknowledgeProblem(arguments[5], arguments[6], sticky ? AcknowledgementSticky : AcknowledgementNormal, timestamp);
}

void ExternalCommandProcessor::RemoveHostAcknowledgement(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot remove acknowledgement for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Removing acknowledgement for host '" + host->GetName() + "'");

	{
		ObjectLock olock(host);
		host->ClearAcknowledgement();
	}
	host->RemoveCommentsByType(CommentAcknowledgement);
}

void ExternalCommandProcessor::EnableHostgroupSvcChecks(double, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable hostgroup service checks for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
			Log(LogInformation, "icinga", "Enabling active checks for service '" + service->GetName() + "'");

			{
				ObjectLock olock(service);

				service->SetEnableActiveChecks(true);
			}
		}
	}
}

void ExternalCommandProcessor::DisableHostgroupSvcChecks(double, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable hostgroup service checks for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
			Log(LogInformation, "icinga", "Disabling active checks for service '" + service->GetName() + "'");

			{
				ObjectLock olock(service);

				service->SetEnableActiveChecks(false);
			}
		}
	}
}

void ExternalCommandProcessor::EnableServicegroupSvcChecks(double, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable servicegroup service checks for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Log(LogInformation, "icinga", "Enabling active checks for service '" + service->GetName() + "'");

		{
			ObjectLock olock(service);

			service->SetEnableActiveChecks(true);
		}
	}
}

void ExternalCommandProcessor::DisableServicegroupSvcChecks(double, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable servicegroup service checks for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Log(LogInformation, "icinga", "Disabling active checks for service '" + service->GetName() + "'");

		{
			ObjectLock olock(service);

			service->SetEnableActiveChecks(false);
		}
	}
}

void ExternalCommandProcessor::EnablePassiveHostChecks(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable passive host checks for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Enabling passive checks for host '" + arguments[0] + "'");

	{
		ObjectLock olock(host);

		host->SetEnablePassiveChecks(true);
	}
}

void ExternalCommandProcessor::DisablePassiveHostChecks(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable passive host checks for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Disabling passive checks for host '" + arguments[0] + "'");

	{
		ObjectLock olock(host);

		host->SetEnablePassiveChecks(false);
	}
}

void ExternalCommandProcessor::EnablePassiveSvcChecks(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable service checks for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Enabling passive checks for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetEnablePassiveChecks(true);
	}
}

void ExternalCommandProcessor::DisablePassiveSvcChecks(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable service checks for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Disabling passive checks for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetEnablePassiveChecks(false);
	}
}

void ExternalCommandProcessor::EnableServicegroupPassiveSvcChecks(double, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable servicegroup passive service checks for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Log(LogInformation, "icinga", "Enabling passive checks for service '" + service->GetName() + "'");

		{
			ObjectLock olock(service);

			service->SetEnablePassiveChecks(true);
		}
	}
}

void ExternalCommandProcessor::DisableServicegroupPassiveSvcChecks(double, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable servicegroup passive service checks for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Log(LogInformation, "icinga", "Disabling passive checks for service '" + service->GetName() + "'");

		{
			ObjectLock olock(service);

			service->SetEnablePassiveChecks(false);
		}
	}
}

void ExternalCommandProcessor::EnableHostgroupPassiveSvcChecks(double, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable hostgroup passive service checks for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
			Log(LogInformation, "icinga", "Enabling passive checks for service '" + service->GetName() + "'");

			{
				ObjectLock olock(service);

				service->SetEnablePassiveChecks(true);
			}
		}
	}
}

void ExternalCommandProcessor::DisableHostgroupPassiveSvcChecks(double, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable hostgroup passive service checks for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
			Log(LogInformation, "icinga", "Disabling passive checks for service '" + service->GetName() + "'");

			{
				ObjectLock olock(service);

				service->SetEnablePassiveChecks(false);
			}
		}
	}
}

void ExternalCommandProcessor::ProcessFile(double, const std::vector<String>& arguments)
{
	String file = arguments[0];
	bool del = Convert::ToBool(arguments[1]);

	std::ifstream ifp;
	ifp.exceptions(std::ifstream::badbit);

	ifp.open(file.CStr(), std::ifstream::in);

	while(ifp.good()) {
		std::string line;
		std::getline(ifp, line);

		try {
			Log(LogInformation, "compat", "Executing external command: " + line);

			Execute(line);
		} catch (const std::exception& ex) {
			std::ostringstream msgbuf;
			msgbuf << "External command failed: " << DiagnosticInformation(ex);
			Log(LogWarning, "icinga", msgbuf.str());
		}
	}

	ifp.close();

	if (del)
		(void) unlink(file.CStr());
}

void ExternalCommandProcessor::ScheduleSvcDowntime(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot schedule service downtime for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	String triggeredBy;
	int triggeredByLegacy = Convert::ToLong(arguments[5]);
	if (triggeredByLegacy != 0)
		triggeredBy = Service::GetDowntimeIDFromLegacyID(triggeredByLegacy);

	Log(LogInformation, "icinga", "Creating downtime for service " + service->GetName());
	(void) service->AddDowntime(arguments[7], arguments[8],
	    Convert::ToDouble(arguments[2]), Convert::ToDouble(arguments[3]),
	    Convert::ToBool(arguments[4]), triggeredBy, Convert::ToDouble(arguments[6]));
}

void ExternalCommandProcessor::DelSvcDowntime(double, const std::vector<String>& arguments)
{
	int id = Convert::ToLong(arguments[0]);
	Log(LogInformation, "icinga", "Removing downtime ID " + arguments[0]);
	String rid = Service::GetDowntimeIDFromLegacyID(id);
	Service::RemoveDowntime(rid, true);
}

void ExternalCommandProcessor::ScheduleHostDowntime(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot schedule host downtime for non-existent host '" + arguments[0] + "'"));

	String triggeredBy;
	int triggeredByLegacy = Convert::ToLong(arguments[4]);
	if (triggeredByLegacy != 0)
		triggeredBy = Service::GetDowntimeIDFromLegacyID(triggeredByLegacy);

	Log(LogInformation, "icinga", "Creating downtime for host " + host->GetName());

	(void) host->AddDowntime(arguments[6], arguments[7],
	    Convert::ToDouble(arguments[1]), Convert::ToDouble(arguments[2]),
	    Convert::ToBool(arguments[3]), triggeredBy, Convert::ToDouble(arguments[5]));
}

void ExternalCommandProcessor::DelHostDowntime(double, const std::vector<String>& arguments)
{
	int id = Convert::ToLong(arguments[0]);
	Log(LogInformation, "icinga", "Removing downtime ID " + arguments[0]);
	String rid = Service::GetDowntimeIDFromLegacyID(id);
	Service::RemoveDowntime(rid, true);
}

void ExternalCommandProcessor::ScheduleHostSvcDowntime(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot schedule host services downtime for non-existent host '" + arguments[0] + "'"));

	String triggeredBy;
	int triggeredByLegacy = Convert::ToLong(arguments[4]);
	if (triggeredByLegacy != 0)
		triggeredBy = Service::GetDowntimeIDFromLegacyID(triggeredByLegacy);

	BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
		Log(LogInformation, "icinga", "Creating downtime for service " + service->GetName());
		(void) service->AddDowntime(arguments[6], arguments[7],
		    Convert::ToDouble(arguments[1]), Convert::ToDouble(arguments[2]),
		    Convert::ToBool(arguments[3]), triggeredBy, Convert::ToDouble(arguments[5]));
	}
}

void ExternalCommandProcessor::ScheduleHostgroupHostDowntime(double, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot schedule hostgroup host downtime for non-existent hostgroup '" + arguments[0] + "'"));

	String triggeredBy;
	int triggeredByLegacy = Convert::ToLong(arguments[4]);
	if (triggeredByLegacy != 0)
		triggeredBy = Service::GetDowntimeIDFromLegacyID(triggeredByLegacy);

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		Log(LogInformation, "icinga", "Creating downtime for host " + host->GetName());

		(void) host->AddDowntime(arguments[6], arguments[7],
		    Convert::ToDouble(arguments[1]), Convert::ToDouble(arguments[2]),
		    Convert::ToBool(arguments[3]), triggeredBy, Convert::ToDouble(arguments[5]));
	}
}

void ExternalCommandProcessor::ScheduleHostgroupSvcDowntime(double, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot schedule hostgroup service downtime for non-existent hostgroup '" + arguments[0] + "'"));

	String triggeredBy;
	int triggeredByLegacy = Convert::ToLong(arguments[4]);
	if (triggeredByLegacy != 0)
		triggeredBy = Service::GetDowntimeIDFromLegacyID(triggeredByLegacy);

	/* Note: we can't just directly create downtimes for all the services by iterating
	 * over all hosts in the host group - otherwise we might end up creating multiple
	 * downtimes for some services. */

	std::set<Service::Ptr> services;

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
			services.insert(service);
		}
	}

	BOOST_FOREACH(const Service::Ptr& service, services) {
		Log(LogInformation, "icinga", "Creating downtime for service " + service->GetName());
		(void) service->AddDowntime(arguments[6], arguments[7],
		    Convert::ToDouble(arguments[1]), Convert::ToDouble(arguments[2]),
		    Convert::ToBool(arguments[3]), triggeredBy, Convert::ToDouble(arguments[5]));
	}
}

void ExternalCommandProcessor::ScheduleServicegroupHostDowntime(double, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot schedule servicegroup host downtime for non-existent servicegroup '" + arguments[0] + "'"));

	String triggeredBy;
	int triggeredByLegacy = Convert::ToLong(arguments[4]);
	if (triggeredByLegacy != 0)
		triggeredBy = Service::GetDowntimeIDFromLegacyID(triggeredByLegacy);

	/* Note: we can't just directly create downtimes for all the hosts by iterating
	 * over all services in the service group - otherwise we might end up creating multiple
	 * downtimes for some hosts. */

	std::set<Host::Ptr> hosts;

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Host::Ptr host = service->GetHost();
		hosts.insert(host);
	}

	BOOST_FOREACH(const Host::Ptr& host, hosts) {
		Log(LogInformation, "icinga", "Creating downtime for host " + host->GetName());
		(void) host->AddDowntime(arguments[6], arguments[7],
		    Convert::ToDouble(arguments[1]), Convert::ToDouble(arguments[2]),
		    Convert::ToBool(arguments[3]), triggeredBy, Convert::ToDouble(arguments[5]));
	}
}

void ExternalCommandProcessor::ScheduleServicegroupSvcDowntime(double, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot schedule servicegroup service downtime for non-existent servicegroup '" + arguments[0] + "'"));

	String triggeredBy;
	int triggeredByLegacy = Convert::ToLong(arguments[4]);
	if (triggeredByLegacy != 0)
		triggeredBy = Service::GetDowntimeIDFromLegacyID(triggeredByLegacy);

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Log(LogInformation, "icinga", "Creating downtime for service " + service->GetName());
		(void) service->AddDowntime(arguments[6], arguments[7],
		    Convert::ToDouble(arguments[1]), Convert::ToDouble(arguments[2]),
		    Convert::ToBool(arguments[3]), triggeredBy, Convert::ToDouble(arguments[5]));
	}
}

void ExternalCommandProcessor::AddHostComment(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot add host comment for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Creating comment for host " + host->GetName());
	(void) host->AddComment(CommentUser, arguments[2], arguments[3], 0);
}

void ExternalCommandProcessor::DelHostComment(double, const std::vector<String>& arguments)
{
	int id = Convert::ToLong(arguments[0]);
	Log(LogInformation, "icinga", "Removing comment ID " + arguments[0]);
	String rid = Service::GetCommentIDFromLegacyID(id);
	Service::RemoveComment(rid);
}

void ExternalCommandProcessor::AddSvcComment(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot add service comment for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Creating comment for service " + service->GetName());
	(void) service->AddComment(CommentUser, arguments[3], arguments[4], 0);
}

void ExternalCommandProcessor::DelSvcComment(double, const std::vector<String>& arguments)
{
	int id = Convert::ToLong(arguments[0]);
	Log(LogInformation, "icinga", "Removing comment ID " + arguments[0]);

	String rid = Service::GetCommentIDFromLegacyID(id);
	Service::RemoveComment(rid);
}

void ExternalCommandProcessor::DelAllHostComments(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot delete all host comments for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Removing all comments for host " + host->GetName());
	host->RemoveAllComments();
}

void ExternalCommandProcessor::DelAllSvcComments(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot delete all service comments for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Removing all comments for service " + service->GetName());
	service->RemoveAllComments();
}

void ExternalCommandProcessor::SendCustomHostNotification(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot send custom host notification for non-existent host '" + arguments[0] + "'"));

	int options = Convert::ToLong(arguments[1]);

	Log(LogInformation, "icinga", "Sending custom notification for host " + host->GetName());
	if (options & 2) {
		ObjectLock olock(host);
		host->SetForceNextNotification(true);
	}

	Checkable::OnNotificationsRequested(host, NotificationCustom, host->GetLastCheckResult(), arguments[2], arguments[3]);
}

void ExternalCommandProcessor::SendCustomSvcNotification(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot send custom service notification for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	int options = Convert::ToLong(arguments[2]);

	Log(LogInformation, "icinga", "Sending custom notification for service " + service->GetName());

	if (options & 2) {
		ObjectLock olock(service);
		service->SetForceNextNotification(true);
	}

	Service::OnNotificationsRequested(service, NotificationCustom, service->GetLastCheckResult(), arguments[3], arguments[4]);
}

void ExternalCommandProcessor::DelayHostNotification(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot delay host notification for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Delaying notifications for host '" + host->GetName() + "'");

	BOOST_FOREACH(const Notification::Ptr& notification, host->GetNotifications()) {
		ObjectLock olock(notification);

		notification->SetNextNotification(Convert::ToDouble(arguments[1]));
	}
}

void ExternalCommandProcessor::DelaySvcNotification(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot delay service notification for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Delaying notifications for service " + service->GetName());

	BOOST_FOREACH(const Notification::Ptr& notification, service->GetNotifications()) {
		ObjectLock olock(notification);

		notification->SetNextNotification(Convert::ToDouble(arguments[2]));
	}
}

void ExternalCommandProcessor::EnableHostNotifications(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable host notifications for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Enabling notifications for host '" + arguments[0] + "'");

	{
		ObjectLock olock(host);

		host->SetEnableNotifications(true);
	}
}

void ExternalCommandProcessor::DisableHostNotifications(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable host notifications for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Disabling notifications for host '" + arguments[0] + "'");

	{
		ObjectLock olock(host);

		host->SetEnableNotifications(false);
	}
}

void ExternalCommandProcessor::EnableSvcNotifications(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable service notifications for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Enabling notifications for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetEnableNotifications(true);
	}
}

void ExternalCommandProcessor::DisableSvcNotifications(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable service notifications for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Disabling notifications for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetEnableNotifications(false);
	}
}

void ExternalCommandProcessor::DisableHostgroupHostChecks(double, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable hostgroup host checks for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		Log(LogInformation, "icinga", "Disabling active checks for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnableActiveChecks(false);
		}
	}
}

void ExternalCommandProcessor::DisableHostgroupPassiveHostChecks(double, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable hostgroup passive host checks for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		Log(LogInformation, "icinga", "Disabling passive checks for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnablePassiveChecks(false);
		}
	}
}

void ExternalCommandProcessor::DisableServicegroupHostChecks(double, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable servicegroup host checks for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Host::Ptr host = service->GetHost();

		Log(LogInformation, "icinga", "Disabling active checks for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnableActiveChecks(false);
		}
	}
}

void ExternalCommandProcessor::DisableServicegroupPassiveHostChecks(double, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable servicegroup passive host checks for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Host::Ptr host = service->GetHost();

		Log(LogInformation, "icinga", "Disabling passive checks for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnablePassiveChecks(false);
		}
	}
}

void ExternalCommandProcessor::EnableHostgroupHostChecks(double, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable hostgroup host checks for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		Log(LogInformation, "icinga", "Enabling active checks for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnableActiveChecks(true);
		}
	}
}

void ExternalCommandProcessor::EnableHostgroupPassiveHostChecks(double, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable hostgroup passive host checks for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		Log(LogInformation, "icinga", "Enabling passive checks for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnablePassiveChecks(true);
		}
	}
}

void ExternalCommandProcessor::EnableServicegroupHostChecks(double, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable servicegroup host checks for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Host::Ptr host = service->GetHost();

		Log(LogInformation, "icinga", "Enabling active checks for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnableActiveChecks(true);
		}
	}
}

void ExternalCommandProcessor::EnableServicegroupPassiveHostChecks(double, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable servicegroup passive host checks for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Host::Ptr host = service->GetHost();

		Log(LogInformation, "icinga", "Enabling passive checks for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnablePassiveChecks(true);
		}
	}
}

void ExternalCommandProcessor::EnableHostFlapping(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable host flapping for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Enabling flapping detection for host '" + arguments[0] + "'");

	{
		ObjectLock olock(host);

		host->SetEnableFlapping(true);
	}
}

void ExternalCommandProcessor::DisableHostFlapping(double, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable host flapping for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Disabling flapping detection for host '" + arguments[0] + "'");

	{
		ObjectLock olock(host);

		host->SetEnableFlapping(false);
	}
}

void ExternalCommandProcessor::EnableSvcFlapping(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable service flapping for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Enabling flapping detection for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetEnableFlapping(true);
	}
}

void ExternalCommandProcessor::DisableSvcFlapping(double, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable service flapping for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Disabling flapping detection for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetEnableFlapping(false);
	}
}

void ExternalCommandProcessor::EnableNotifications(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally enabling notifications.");

	IcingaApplication::GetInstance()->SetEnableNotifications(true);
}

void ExternalCommandProcessor::DisableNotifications(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally disabling notifications.");

	IcingaApplication::GetInstance()->SetEnableNotifications(false);
}

void ExternalCommandProcessor::EnableFlapDetection(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally enabling flap detection.");

	IcingaApplication::GetInstance()->SetEnableFlapping(true);
}

void ExternalCommandProcessor::DisableFlapDetection(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally disabling flap detection.");

	IcingaApplication::GetInstance()->SetEnableFlapping(false);
}

void ExternalCommandProcessor::EnableEventHandlers(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally enabling event handlers.");

	IcingaApplication::GetInstance()->SetEnableEventHandlers(true);
}

void ExternalCommandProcessor::DisableEventHandlers(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally disabling event handlers.");

	IcingaApplication::GetInstance()->SetEnableEventHandlers(false);
}

void ExternalCommandProcessor::EnablePerformanceData(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally enabling performance data processing.");

	IcingaApplication::GetInstance()->SetEnablePerfdata(true);
}

void ExternalCommandProcessor::DisablePerformanceData(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally disabling performance data processing.");

	IcingaApplication::GetInstance()->SetEnablePerfdata(false);
}

void ExternalCommandProcessor::StartExecutingSvcChecks(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally enabling service checks.");

	IcingaApplication::GetInstance()->SetEnableServiceChecks(true);
}

void ExternalCommandProcessor::StopExecutingSvcChecks(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally disabling service checks.");

	IcingaApplication::GetInstance()->SetEnableServiceChecks(false);
}

void ExternalCommandProcessor::StartExecutingHostChecks(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally enabling host checks.");

	IcingaApplication::GetInstance()->SetEnableHostChecks(true);
}

void ExternalCommandProcessor::StopExecutingHostChecks(double time, const std::vector<String>& arguments)
{
	Log(LogInformation, "icinga", "Globally disabling host checks.");

	IcingaApplication::GetInstance()->SetEnableHostChecks(false);
}

void ExternalCommandProcessor::ChangeSvcModattr(double time, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot update modified attributes for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	int modifiedAttributes = Convert::ToLong(arguments[2]);

	Log(LogInformation, "icinga", "Updating modified attributes for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetModifiedAttributes(modifiedAttributes);
	}
}

void ExternalCommandProcessor::ChangeHostModattr(double time, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot update modified attributes for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Updating modified attributes for host '" + arguments[0] + "'");

	int modifiedAttributes = Convert::ToLong(arguments[1]);

	{
		ObjectLock olock(host);

		host->SetModifiedAttributes(modifiedAttributes);
	}
}

void ExternalCommandProcessor::ChangeUserModattr(double time, const std::vector<String>& arguments)
{
	User::Ptr user = User::GetByName(arguments[0]);

	if (!user)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot update modified attributes for non-existent user '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Updating modified attributes for user '" + arguments[0] + "'");

	int modifiedAttributes = Convert::ToLong(arguments[1]);

	{
		ObjectLock olock(user);

		user->SetModifiedAttributes(modifiedAttributes);
	}
}

void ExternalCommandProcessor::ChangeCheckcommandModattr(double time, const std::vector<String>& arguments)
{
	CheckCommand::Ptr command = CheckCommand::GetByName(arguments[0]);

	if (!command)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot update modified attributes for non-existent command '" + arguments[0] + "'"));

	ChangeCommandModattrInternal(command, Convert::ToLong(arguments[1]));
}

void ExternalCommandProcessor::ChangeEventcommandModattr(double time, const std::vector<String>& arguments)
{
	EventCommand::Ptr command = EventCommand::GetByName(arguments[0]);

	if (!command)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot update modified attributes for non-existent command '" + arguments[0] + "'"));

	ChangeCommandModattrInternal(command, Convert::ToLong(arguments[1]));
}

void ExternalCommandProcessor::ChangeNotificationcommandModattr(double time, const std::vector<String>& arguments)
{
	NotificationCommand::Ptr command = NotificationCommand::GetByName(arguments[0]);

	if (!command)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot update modified attributes for non-existent command '" + arguments[0] + "'"));

	ChangeCommandModattrInternal(command, Convert::ToLong(arguments[1]));
}

void ExternalCommandProcessor::ChangeCommandModattrInternal(const Command::Ptr& command, int mod_attr)
{
	Log(LogInformation, "icinga", "Updating modified attributes for command '" + command->GetName() + "'");

	{
		ObjectLock olock(command);

		command->SetModifiedAttributes(mod_attr);
	}
}

void ExternalCommandProcessor::ChangeNormalSvcCheckInterval(double time, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot update check interval for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	double interval = Convert::ToDouble(arguments[2]);

	Log(LogInformation, "icinga", "Updating check interval for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetCheckInterval(interval * 60);
	}
}

void ExternalCommandProcessor::ChangeNormalHostCheckInterval(double time, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot update check interval for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Updating check interval for host '" + arguments[0] + "'");

	double interval = Convert::ToDouble(arguments[1]);

	{
		ObjectLock olock(host);

		host->SetCheckInterval(interval * 60);
	}
}

void ExternalCommandProcessor::ChangeRetrySvcCheckInterval(double time, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot update retry interval for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	double interval = Convert::ToDouble(arguments[2]);

	Log(LogInformation, "icinga", "Updating retry interval for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetRetryInterval(interval * 60);
	}
}

void ExternalCommandProcessor::ChangeRetryHostCheckInterval(double time, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot update retry interval for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Updating retry interval for host '" + arguments[0] + "'");

	double interval = Convert::ToDouble(arguments[1]);

	{
		ObjectLock olock(host);

		host->SetRetryInterval(interval * 60);
	}
}

void ExternalCommandProcessor::EnableHostEventHandler(double time, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable event handler for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Enabling event handler for host '" + arguments[0] + "'");

	{
		ObjectLock olock(host);

		host->SetEnableEventHandler(true);
	}
}

void ExternalCommandProcessor::DisableHostEventHandler(double time, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable event handler for non-existent host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Disabling event handler for host '" + arguments[0] + "'");

	{
		ObjectLock olock(host);

		host->SetEnableEventHandler(false);
	}
}

void ExternalCommandProcessor::EnableSvcEventHandler(double time, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable event handler for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Enabling event handler for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetEnableEventHandler(true);
	}
}

void ExternalCommandProcessor::DisableSvcEventHandler(double time, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable event handler for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Log(LogInformation, "icinga", "Disabling event handler for service '" + arguments[1] + "'");

	{
		ObjectLock olock(service);

		service->SetEnableEventHandler(false);
	}
}

void ExternalCommandProcessor::ChangeHostEventHandler(double time, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change event handler for non-existent host '" + arguments[0] + "'"));

	/* empty command string implicitely disables event handler */
	if (arguments[1].IsEmpty()) {
		host->SetEnableEventHandler(false);
	} else {
		EventCommand::Ptr command = EventCommand::GetByName(arguments[1]);

		if (!command)
			BOOST_THROW_EXCEPTION(std::invalid_argument("Event command '" + arguments[1] + "' does not exist."));

		Log(LogInformation, "icinga", "Changing event handler for host '" + arguments[0] + "' to '" + arguments[1] + "'");

		{
			ObjectLock olock(host);

			host->SetEventCommand(command);
		}
	}
}

void ExternalCommandProcessor::ChangeSvcEventHandler(double time, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change event handler for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	/* empty command string implicitely disables event handler */
	if (arguments[2].IsEmpty()) {
		service->SetEnableEventHandler(false);
	} else {
		EventCommand::Ptr command = EventCommand::GetByName(arguments[2]);

		if (!command)
			BOOST_THROW_EXCEPTION(std::invalid_argument("Event command '" + arguments[2] + "' does not exist."));

		Log(LogInformation, "icinga", "Changing event handler for service '" + arguments[1] + "' to '" + arguments[2] + "'");

		{
			ObjectLock olock(service);

			service->SetEventCommand(command);
		}
	}
}

void ExternalCommandProcessor::ChangeHostCheckCommand(double time, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change check command for non-existent host '" + arguments[0] + "'"));

	CheckCommand::Ptr command = CheckCommand::GetByName(arguments[1]);

	if (!command)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Check command '" + arguments[1] + "' does not exist."));

	Log(LogInformation, "icinga", "Changing check command for host '" + arguments[0] + "' to '" + arguments[1] + "'");

	{
		ObjectLock olock(host);

		host->SetCheckCommand(command);
	}
}

void ExternalCommandProcessor::ChangeSvcCheckCommand(double time, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change check command for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	CheckCommand::Ptr command = CheckCommand::GetByName(arguments[2]);

	if (!command)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Check command '" + arguments[2] + "' does not exist."));

	Log(LogInformation, "icinga", "Changing check command for service '" + arguments[1] + "' to '" + arguments[2] + "'");

	{
		ObjectLock olock(service);

		service->SetCheckCommand(command);
	}
}

void ExternalCommandProcessor::ChangeMaxHostCheckAttempts(double time, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change max check attempts for non-existent host '" + arguments[0] + "'"));

	int attempts = Convert::ToLong(arguments[1]);

	Log(LogInformation, "icinga", "Changing max check attempts for host '" + arguments[0] + "' to '" + arguments[1] + "'");

	{
		ObjectLock olock(host);

		host->SetMaxCheckAttempts(attempts);
	}
}

void ExternalCommandProcessor::ChangeMaxSvcCheckAttempts(double time, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change max check attempts for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	int attempts = Convert::ToLong(arguments[2]);

	Log(LogInformation, "icinga", "Changing max check attempts for service '" + arguments[1] + "' to '" + arguments[2] + "'");

	{
		ObjectLock olock(service);

		service->SetMaxCheckAttempts(attempts);
	}
}

void ExternalCommandProcessor::ChangeHostCheckTimeperiod(double time, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change check period for non-existent host '" + arguments[0] + "'"));

	TimePeriod::Ptr tp = TimePeriod::GetByName(arguments[1]);

	if (!tp)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Time period '" + arguments[1] + "' does not exist."));

	Log(LogInformation, "icinga", "Changing check period for host '" + arguments[0] + "' to '" + arguments[1] + "'");

	{
		ObjectLock olock(host);

		host->SetCheckPeriod(tp);
	}
}

void ExternalCommandProcessor::ChangeSvcCheckTimeperiod(double time, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change check period for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	TimePeriod::Ptr tp = TimePeriod::GetByName(arguments[2]);

	if (!tp)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Time period '" + arguments[2] + "' does not exist."));

	Log(LogInformation, "icinga", "Changing check period for service '" + arguments[1] + "' to '" + arguments[2] + "'");

	{
		ObjectLock olock(service);

		service->SetCheckPeriod(tp);
	}
}

void ExternalCommandProcessor::ChangeCustomHostVar(double time, const std::vector<String>& arguments)
{
	Host::Ptr host = Host::GetByName(arguments[0]);

	if (!host)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change custom var for non-existent host '" + arguments[0] + "'"));

	Dictionary::Ptr vars = host->GetVars();

	if (!vars || !vars->Contains(arguments[1]))
		BOOST_THROW_EXCEPTION(std::invalid_argument("Custom var '" + arguments[1] + "' for host '" + arguments[0] + "' does not exist."));

	Dictionary::Ptr override_vars = vars->ShallowClone();

	override_vars->Set(arguments[1], arguments[2]);

	Log(LogInformation, "icinga", "Changing custom var '" + arguments[1] + "' for host '" + arguments[0] + "' to value '" + arguments[2] + "'");

	{
		ObjectLock olock(host);

		host->SetVars(override_vars);
	}
}

void ExternalCommandProcessor::ChangeCustomSvcVar(double time, const std::vector<String>& arguments)
{
	Service::Ptr service = Service::GetByNamePair(arguments[0], arguments[1]);

	if (!service)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change custom var for non-existent service '" + arguments[1] + "' on host '" + arguments[0] + "'"));

	Dictionary::Ptr vars = service->GetVars();

	if (!vars || !vars->Contains(arguments[2]))
		BOOST_THROW_EXCEPTION(std::invalid_argument("Custom var '" + arguments[2] + "' for service '" + arguments[1] +
		    "' on host '" + arguments[0] + "' does not exist."));

	Dictionary::Ptr override_vars = vars->ShallowClone();

	override_vars->Set(arguments[2], arguments[3]);

	Log(LogInformation, "icinga", "Changing custom var '" + arguments[2] + "' for service '" + arguments[1] + "' on host '" +
	    arguments[0] + "' to value '" + arguments[3] + "'");

	{
		ObjectLock olock(service);

		service->SetVars(override_vars);
	}
}

void ExternalCommandProcessor::ChangeCustomUserVar(double time, const std::vector<String>& arguments)
{
	User::Ptr user = User::GetByName(arguments[0]);

	if (!user)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change custom var for non-existent user '" + arguments[0] + "'"));

	Dictionary::Ptr vars = user->GetVars();

	if (!vars || !vars->Contains(arguments[1]))
		BOOST_THROW_EXCEPTION(std::invalid_argument("Custom var '" + arguments[1] + "' for user '" + arguments[0] + "' does not exist."));

	Dictionary::Ptr override_vars = vars->ShallowClone();

	override_vars->Set(arguments[1], arguments[2]);

	Log(LogInformation, "icinga", "Changing custom var '" + arguments[1] + "' for user '" + arguments[0] + "' to value '" + arguments[2] + "'");

	{
		ObjectLock olock(user);

		user->SetVars(override_vars);
	}
}

void ExternalCommandProcessor::ChangeCustomCheckcommandVar(double time, const std::vector<String>& arguments)
{
	CheckCommand::Ptr command = CheckCommand::GetByName(arguments[0]);

	if (!command)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change custom var for non-existent command '" + arguments[0] + "'"));

	ChangeCustomCommandVarInternal(command, arguments[1], arguments[2]);
}

void ExternalCommandProcessor::ChangeCustomEventcommandVar(double time, const std::vector<String>& arguments)
{
	EventCommand::Ptr command = EventCommand::GetByName(arguments[0]);

	if (!command)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change custom var for non-existent command '" + arguments[0] + "'"));

	ChangeCustomCommandVarInternal(command, arguments[1], arguments[2]);
}

void ExternalCommandProcessor::ChangeCustomNotificationcommandVar(double time, const std::vector<String>& arguments)
{
	NotificationCommand::Ptr command = NotificationCommand::GetByName(arguments[0]);

	if (!command)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot change custom var for non-existent command '" + arguments[0] + "'"));

	ChangeCustomCommandVarInternal(command, arguments[1], arguments[2]);
}

void ExternalCommandProcessor::ChangeCustomCommandVarInternal(const Command::Ptr& command, const String& name, const Value& value)
{
	Dictionary::Ptr vars = command->GetVars();

	if (!vars || !vars->Contains(name))
		BOOST_THROW_EXCEPTION(std::invalid_argument("Custom var '" + name + "' for command '" + command->GetName() + "' does not exist."));

	Dictionary::Ptr override_vars = vars->ShallowClone();

	override_vars->Set(name, value);

	Log(LogInformation, "icinga", "Changing custom var '" + name + "' for command '" + command->GetName() + "' to value '" + Convert::ToString(value) + "'");

	{
		ObjectLock olock(command);

		command->SetVars(override_vars);
	}
}

void ExternalCommandProcessor::EnableHostgroupHostNotifications(double time, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable host notifications for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		Log(LogInformation, "icinga", "Enabling notifications for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnableNotifications(true);
		}
	}
}

void ExternalCommandProcessor::EnableHostgroupSvcNotifications(double time, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable service notifications for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
			Log(LogInformation, "icinga", "Enabling notifications for service '" + service->GetName() + "'");

			{
				ObjectLock olock(service);

				service->SetEnableNotifications(true);
			}
		}
	}
}

void ExternalCommandProcessor::DisableHostgroupHostNotifications(double time, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable host notifications for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		Log(LogInformation, "icinga", "Disabling notifications for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnableNotifications(false);
		}
	}
}

void ExternalCommandProcessor::DisableHostgroupSvcNotifications(double time, const std::vector<String>& arguments)
{
	HostGroup::Ptr hg = HostGroup::GetByName(arguments[0]);

	if (!hg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable service notifications for non-existent hostgroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Host::Ptr& host, hg->GetMembers()) {
		BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
			Log(LogInformation, "icinga", "Disabling notifications for service '" + service->GetName() + "'");

			{
				ObjectLock olock(service);

				service->SetEnableNotifications(false);
			}
		}
	}
}

void ExternalCommandProcessor::EnableServicegroupHostNotifications(double time, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable host notifications for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Host::Ptr host = service->GetHost();

		Log(LogInformation, "icinga", "Enabling notifications for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnableNotifications(true);
		}
	}
}

void ExternalCommandProcessor::EnableServicegroupSvcNotifications(double time, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot enable service notifications for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Log(LogInformation, "icinga", "Enabling notifications for service '" + service->GetName() + "'");

		{
			ObjectLock olock(service);

			service->SetEnableNotifications(true);
		}
	}
}

void ExternalCommandProcessor::DisableServicegroupHostNotifications(double time, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable host notifications for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Host::Ptr host = service->GetHost();

		Log(LogInformation, "icinga", "Disabling notifications for host '" + host->GetName() + "'");

		{
			ObjectLock olock(host);

			host->SetEnableNotifications(false);
		}
	}
}

void ExternalCommandProcessor::DisableServicegroupSvcNotifications(double time, const std::vector<String>& arguments)
{
	ServiceGroup::Ptr sg = ServiceGroup::GetByName(arguments[0]);

	if (!sg)
		BOOST_THROW_EXCEPTION(std::invalid_argument("Cannot disable service notifications for non-existent servicegroup '" + arguments[0] + "'"));

	BOOST_FOREACH(const Service::Ptr& service, sg->GetMembers()) {
		Log(LogInformation, "icinga", "Disabling notifications for service '" + service->GetName() + "'");

		{
			ObjectLock olock(service);

			service->SetEnableNotifications(false);
		}
	}
}
