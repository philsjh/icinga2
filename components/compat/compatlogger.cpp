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

#include "compat/compatlogger.h"
#include "icinga/service.h"
#include "icinga/checkcommand.h"
#include "icinga/eventcommand.h"
#include "icinga/notification.h"
#include "icinga/macroprocessor.h"
#include "icinga/externalcommandprocessor.h"
#include "icinga/compatutility.h"
#include "config/configcompilercontext.h"
#include "base/dynamictype.h"
#include "base/objectlock.h"
#include "base/logger_fwd.h"
#include "base/exception.h"
#include "base/convert.h"
#include "base/application.h"
#include "base/utility.h"
#include "base/scriptfunction.h"
#include "base/statsfunction.h"
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>

using namespace icinga;

REGISTER_TYPE(CompatLogger);
REGISTER_SCRIPTFUNCTION(ValidateRotationMethod, &CompatLogger::ValidateRotationMethod);

REGISTER_STATSFUNCTION(CompatLoggerStats, &CompatLogger::StatsFunc);

Value CompatLogger::StatsFunc(Dictionary::Ptr& status, Dictionary::Ptr& perfdata)
{
	Dictionary::Ptr nodes = make_shared<Dictionary>();

	BOOST_FOREACH(const CompatLogger::Ptr& compat_logger, DynamicType::GetObjects<CompatLogger>()) {
		nodes->Set(compat_logger->GetName(), 1); //add more stats
	}

	status->Set("compatlogger", nodes);

	return 0;
}

/**
 * @threadsafety Always.
 */
void CompatLogger::Start(void)
{
	DynamicObject::Start();

	Checkable::OnNewCheckResult.connect(bind(&CompatLogger::CheckResultHandler, this, _1, _2));
	Checkable::OnNotificationSentToUser.connect(bind(&CompatLogger::NotificationSentHandler, this, _1, _2, _3, _4, _5, _6, _7, _8));
	Checkable::OnFlappingChanged.connect(bind(&CompatLogger::FlappingHandler, this, _1, _2));
	Checkable::OnDowntimeTriggered.connect(boost::bind(&CompatLogger::TriggerDowntimeHandler, this, _1, _2));
	Checkable::OnDowntimeRemoved.connect(boost::bind(&CompatLogger::RemoveDowntimeHandler, this, _1, _2));
	Checkable::OnEventCommandExecuted.connect(bind(&CompatLogger::EventCommandHandler, this, _1));
	ExternalCommandProcessor::OnNewExternalCommand.connect(boost::bind(&CompatLogger::ExternalCommandHandler, this, _2, _3));

	m_RotationTimer = make_shared<Timer>();
	m_RotationTimer->OnTimerExpired.connect(boost::bind(&CompatLogger::RotationTimerHandler, this));
	m_RotationTimer->Start();

	ReopenFile(false);
	ScheduleNextRotation();
}

/**
 * @threadsafety Always.
 */
void CompatLogger::CheckResultHandler(const Checkable::Ptr& checkable, const CheckResult::Ptr &cr)
{
	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr vars_after = cr->GetVarsAfter();

	long state_after = vars_after->Get("state");
	long stateType_after = vars_after->Get("state_type");
	long attempt_after = vars_after->Get("attempt");
	bool reachable_after = vars_after->Get("reachable");
	bool host_reachable_after = vars_after->Get("host_reachable");

	Dictionary::Ptr vars_before = cr->GetVarsBefore();

	if (vars_before) {
		long state_before = vars_before->Get("state");
		long stateType_before = vars_before->Get("state_type");
		long attempt_before = vars_before->Get("attempt");
		bool reachable_before = vars_before->Get("reachable");

		if (state_before == state_after && stateType_before == stateType_after &&
		    attempt_before == attempt_after && reachable_before == reachable_after)
			return; /* Nothing changed, ignore this checkresult. */
	}

	String output;
	if (cr)
		output = CompatUtility::GetCheckResultOutput(cr);

	std::ostringstream msgbuf;

	if (service) {
		msgbuf << "SERVICE ALERT: "
		       << host->GetName() << ";"
		       << service->GetShortName() << ";"
		       << Service::StateToString(static_cast<ServiceState>(state_after)) << ";"
		       << Service::StateTypeToString(static_cast<StateType>(stateType_after)) << ";"
		       << attempt_after << ";"
		       << output << ""
		       << "";
	} else {
		String state = Host::StateToString(Host::CalculateState(static_cast<ServiceState>(state_after)));

		if (!reachable_after)
			state = "UNREACHABLE";

		msgbuf << "HOST ALERT: "
		       << host->GetName() << ";"
		       << state << ";"
		       << Host::StateTypeToString(static_cast<StateType>(stateType_after)) << ";"
		       << attempt_after << ";"
		       << output << ""
		       << "";

	}

	{
		ObjectLock olock(this);
		WriteLine(msgbuf.str());
		Flush();
	}
}

/**
 * @threadsafety Always.
 */
void CompatLogger::TriggerDowntimeHandler(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime)
{
	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	if (!downtime)
		return;

	std::ostringstream msgbuf;

	if (service) {
		msgbuf << "SERVICE DOWNTIME ALERT: "
			<< host->GetName() << ";"
			<< service->GetShortName() << ";"
			<< "STARTED" << "; "
			<< "Checkable has entered a period of scheduled downtime."
			<< "";
	} else {
		msgbuf << "HOST DOWNTIME ALERT: "
			<< host->GetName() << ";"
			<< "STARTED" << "; "
			<< "Checkable has entered a period of scheduled downtime."
			<< "";
	}

	{
		ObjectLock oLock(this);
		WriteLine(msgbuf.str());
		Flush();
	}
}

/**
 * @threadsafety Always.
 */
void CompatLogger::RemoveDowntimeHandler(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime)
{
	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	if (!downtime)
		return;

	String downtime_output;
	String downtime_state_str;

	if (downtime->GetWasCancelled()) {
		downtime_output = "Scheduled downtime for service has been cancelled.";
		downtime_state_str = "CANCELLED";
	} else {
		downtime_output = "Checkable has exited from a period of scheduled downtime.";
		downtime_state_str = "STOPPED";
	}

	std::ostringstream msgbuf;

	if (service) {
		msgbuf << "SERVICE DOWNTIME ALERT: "
			<< host->GetName() << ";"
			<< service->GetShortName() << ";"
			<< downtime_state_str << "; "
			<< downtime_output
			<< "";
	} else {
		msgbuf << "HOST DOWNTIME ALERT: "
			<< host->GetName() << ";"
			<< downtime_state_str << "; "
			<< downtime_output
			<< "";
	}

	{
		ObjectLock oLock(this);
		WriteLine(msgbuf.str());
		Flush();
	}
}

/**
 * @threadsafety Always.
 */
void CompatLogger::NotificationSentHandler(const Notification::Ptr& notification, const Checkable::Ptr& checkable,
    const User::Ptr& user, NotificationType const& notification_type, CheckResult::Ptr const& cr,
    const String& author, const String& comment_text, const String& command_name)
{
	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	String notification_type_str = Notification::NotificationTypeToString(notification_type);

	/* override problem notifications with their current state string */
	if (notification_type == NotificationProblem) {
		if (service)
			notification_type_str = Service::StateToString(service->GetState());
		else
			notification_type_str = host->IsReachable() ? Host::StateToString(host->GetState()) : "UNREACHABLE";
	}

	String author_comment = "";
	if (notification_type == NotificationCustom || notification_type == NotificationAcknowledgement) {
		author_comment = author + ";" + comment_text;
	}

        if (!cr)
                return;

	String output;
	if (cr)
		output = CompatUtility::GetCheckResultOutput(cr);

        std::ostringstream msgbuf;

	if (service) {
		msgbuf << "SERVICE NOTIFICATION: "
			<< user->GetName() << ";"
			<< host->GetName() << ";"
			<< service->GetShortName() << ";"
			<< notification_type_str << ";"
			<< command_name << ";"
			<< output << ";"
			<< author_comment
			<< "";
	} else {
                msgbuf << "HOST NOTIFICATION: "
			<< user->GetName() << ";"
                        << host->GetName() << ";"
                	<< notification_type_str << " "
			<< "(" << (host->IsReachable() ? Host::StateToString(host->GetState()) : "UNREACHABLE") << ");"
			<< command_name << ";"
			<< output << ";"
			<< author_comment
                        << "";
	}

	{
		ObjectLock oLock(this);
		WriteLine(msgbuf.str());
                Flush();
        }
}

/**
 * @threadsafety Always.
 */
void CompatLogger::FlappingHandler(const Checkable::Ptr& checkable, FlappingState flapping_state)
{
	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	String flapping_state_str;
	String flapping_output;

	switch (flapping_state) {
		case FlappingStarted:
			flapping_output = "Checkable appears to have started flapping (" + Convert::ToString(checkable->GetFlappingCurrent()) + "% change >= " + Convert::ToString(checkable->GetFlappingThreshold()) + "% threshold)";
			flapping_state_str = "STARTED";
			break;
		case FlappingStopped:
			flapping_output = "Checkable appears to have stopped flapping (" + Convert::ToString(checkable->GetFlappingCurrent()) + "% change < " + Convert::ToString(checkable->GetFlappingThreshold()) + "% threshold)";
			flapping_state_str = "STOPPED";
			break;
		case FlappingDisabled:
			flapping_output = "Flap detection has been disabled";
			flapping_state_str = "DISABLED";
			break;
		default:
			Log(LogCritical, "compat", "Unknown flapping state: " + Convert::ToString(flapping_state));
			return;
	}

        std::ostringstream msgbuf;

	if (service) {
		msgbuf << "SERVICE FLAPPING ALERT: "
			<< host->GetName() << ";"
			<< service->GetShortName() << ";"
			<< flapping_state_str << "; "
			<< flapping_output
			<< "";
	} else {
                msgbuf << "HOST FLAPPING ALERT: "
                        << host->GetName() << ";"
                        << flapping_state_str << "; "
                        << flapping_output
                        << "";
	}

	{
		ObjectLock oLock(this);
		WriteLine(msgbuf.str());
                Flush();
        }
}

void CompatLogger::ExternalCommandHandler(const String& command, const std::vector<String>& arguments)
{
        std::ostringstream msgbuf;
        msgbuf << "EXTERNAL COMMAND: "
                << command << ";"
                << boost::algorithm::join(arguments, ";")
                << "";

        {
                ObjectLock oLock(this);
                WriteLine(msgbuf.str());
        }
}

void CompatLogger::EventCommandHandler(const Checkable::Ptr& checkable)
{
	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	EventCommand::Ptr event_command = checkable->GetEventCommand();
	String event_command_name = event_command->GetName();
	long current_attempt = checkable->GetCheckAttempt();

        std::ostringstream msgbuf;

	if (service) {
		msgbuf << "SERVICE EVENT HANDLER: "
			<< host->GetName() << ";"
			<< service->GetShortName() << ";"
			<< Service::StateToString(service->GetState()) << ";"
			<< Service::StateTypeToString(service->GetStateType()) << ";"
			<< current_attempt << ";"
			<< event_command_name;
	} else {
		msgbuf << "HOST EVENT HANDLER: "
                        << host->GetName() << ";"
			<< (host->IsReachable() ? Host::StateToString(host->GetState()) : "UNREACHABLE") << ";"
			<< Host::StateTypeToString(host->GetStateType()) << ";"
			<< current_attempt << ";"
			<< event_command_name;
	}

	{
		ObjectLock oLock(this);
		WriteLine(msgbuf.str());
                Flush();
        }
}

void CompatLogger::WriteLine(const String& line)
{
	ASSERT(OwnsLock());

	if (!m_OutputFile.good())
		return;

	m_OutputFile << "[" << (long)Utility::GetTime() << "] " << line << "\n";
}

void CompatLogger::Flush(void)
{
	ASSERT(OwnsLock());

	if (!m_OutputFile.good())
		return;

	m_OutputFile << std::flush;
}

/**
 * @threadsafety Always.
 */
void CompatLogger::ReopenFile(bool rotate)
{
	ObjectLock olock(this);

	String tempFile = GetLogDir() + "/icinga.log";

	if (m_OutputFile) {
		m_OutputFile.close();

		if (rotate) {
			String archiveFile = GetLogDir() + "/archives/icinga-" + Utility::FormatDateTime("%m-%d-%Y-%H", Utility::GetTime()) + ".log";

			Log(LogInformation, "compat", "Rotating compat log file '" + tempFile + "' -> '" + archiveFile + "'");
			(void) rename(tempFile.CStr(), archiveFile.CStr());
		}
	}

	m_OutputFile.open(tempFile.CStr(), std::ofstream::app);

	if (!m_OutputFile.good()) {
		Log(LogWarning, "icinga", "Could not open compat log file '" + tempFile + "' for writing. Log output will be lost.");

		return;
	}

	WriteLine("LOG ROTATION: " + GetRotationMethod());
	WriteLine("LOG VERSION: 2.0");

	BOOST_FOREACH(const Host::Ptr& host, DynamicType::GetObjects<Host>()) {
		String output;
		CheckResult::Ptr cr = host->GetLastCheckResult();

		if (cr)
			output = CompatUtility::GetCheckResultOutput(cr);

		std::ostringstream msgbuf;
		msgbuf << "CURRENT HOST STATE: "
		       << host->GetName() << ";"
		       << (host->IsReachable() ? Host::StateToString(host->GetState()) : "UNREACHABLE") << ";"
		       << Host::StateTypeToString(host->GetStateType()) << ";"
		       << host->GetCheckAttempt() << ";"
		       << output << "";

		WriteLine(msgbuf.str());
	}

	BOOST_FOREACH(const Service::Ptr& service, DynamicType::GetObjects<Service>()) {
		Host::Ptr host = service->GetHost();

		String output;
		CheckResult::Ptr cr = service->GetLastCheckResult();

		if (cr)
			output = CompatUtility::GetCheckResultOutput(cr);

		std::ostringstream msgbuf;
		msgbuf << "CURRENT SERVICE STATE: "
		       << host->GetName() << ";"
		       << service->GetShortName() << ";"
		       << Service::StateToString(service->GetState()) << ";"
		       << Service::StateTypeToString(service->GetStateType()) << ";"
		       << service->GetCheckAttempt() << ";"
		       << output << "";

		WriteLine(msgbuf.str());
	}

	Flush();
}

void CompatLogger::ScheduleNextRotation(void)
{
	time_t now = (time_t)Utility::GetTime();
	String method = GetRotationMethod();

	tm tmthen;

#ifdef _MSC_VER
	tm *temp = localtime(&now);

	if (temp == NULL) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("localtime")
		    << boost::errinfo_errno(errno));
	}

	tmthen = *temp;
#else /* _MSC_VER */
	if (localtime_r(&now, &tmthen) == NULL) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("localtime_r")
		    << boost::errinfo_errno(errno));
	}
#endif /* _MSC_VER */

	tmthen.tm_min = 0;
	tmthen.tm_sec = 0;

	if (method == "HOURLY") {
		tmthen.tm_hour++;
	} else if (method == "DAILY") {
		tmthen.tm_mday++;
		tmthen.tm_hour = 0;
	} else if (method == "WEEKLY") {
		tmthen.tm_mday += 7 - tmthen.tm_wday;
		tmthen.tm_hour = 0;
	} else if (method == "MONTHLY") {
		tmthen.tm_mon++;
		tmthen.tm_mday = 1;
		tmthen.tm_hour = 0;
	}

	time_t ts = mktime(&tmthen);

	Log(LogInformation, "compat", "Rescheduling rotation timer for compat log '"
	    + GetName() + "' to '" + Utility::FormatDateTime("%Y/%m/%d %H:%M:%S %z", ts) + "'");
	m_RotationTimer->Reschedule(ts);
}

/**
 * @threadsafety Always.
 */
void CompatLogger::RotationTimerHandler(void)
{
	try {
		ReopenFile(true);
	} catch (...) {
		ScheduleNextRotation();

		throw;
	}

	ScheduleNextRotation();
}

void CompatLogger::ValidateRotationMethod(const String& location, const Dictionary::Ptr& attrs)
{
	Value rotation_method = attrs->Get("rotation_method");

	if (!rotation_method.IsEmpty() && rotation_method != "HOURLY" && rotation_method != "DAILY" &&
	    rotation_method != "WEEKLY" && rotation_method != "MONTHLY" && rotation_method != "NONE") {
		ConfigCompilerContext::GetInstance()->AddMessage(true, "Validation failed for " +
		    location + ": Rotation method '" + rotation_method + "' is invalid.");
	}
}

