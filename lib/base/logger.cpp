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

#include "base/application.h"
#include "base/streamlogger.h"
#include "base/logger.h"
#include "base/dynamictype.h"
#include "base/utility.h"
#include "base/objectlock.h"
#include "base/context.h"
#include "base/convert.h"
#include "base/scriptvariable.h"
#include <boost/make_shared.hpp>
#include <boost/foreach.hpp>
#include <iostream>

using namespace icinga;

REGISTER_TYPE(Logger);
INITIALIZE_ONCE(&Logger::StaticInitialize);

std::set<Logger::Ptr> Logger::m_Loggers;
boost::mutex Logger::m_Mutex;
bool Logger::m_ConsoleLogEnabled = true;

void Logger::StaticInitialize(void)
{
	ScriptVariable::Set("LogDebug", LogDebug, true, true);
	ScriptVariable::Set("LogInformation", LogInformation, true, true);
	ScriptVariable::Set("LogWarning", LogWarning, true, true);
	ScriptVariable::Set("LogCritical", LogCritical, true, true);
}

/**
 * Constructor for the Logger class.
 */
void Logger::Start(void)
{
	DynamicObject::Start();

	boost::mutex::scoped_lock lock(m_Mutex);
	m_Loggers.insert(GetSelf());
}

void Logger::Stop(void)
{
	boost::mutex::scoped_lock lock(m_Mutex);
	m_Loggers.erase(GetSelf());
}

std::set<Logger::Ptr> Logger::GetLoggers(void)
{
	boost::mutex::scoped_lock lock(m_Mutex);
	return m_Loggers;
}

/**
 * Writes a message to the application's log.
 *
 * @param severity The message severity.
 * @param facility The log facility.
 * @param message The message.
 */
void icinga::Log(LogSeverity severity, const String& facility,
    const String& message)
{
	LogEntry entry;
	entry.Timestamp = Utility::GetTime();
	entry.Severity = severity;
	entry.Facility = facility;
	entry.Message = message;

	if (severity >= LogWarning) {
		ContextTrace context;

		if (context.GetLength() > 0) {
			std::ostringstream trace;
			trace << context;
			entry.Message += "\nContext:" + trace.str();
		}
	}

	BOOST_FOREACH(const Logger::Ptr& logger, Logger::GetLoggers()) {
		ObjectLock llock(logger);

		if (!logger->IsActive())
			continue;

		if (entry.Severity >= logger->GetMinSeverity())
			logger->ProcessLogEntry(entry);
	}

	LogSeverity defaultLogLevel;

	if (Application::IsDebugging())
		defaultLogLevel = LogDebug;
	else
		defaultLogLevel = LogInformation;

	if (Logger::IsConsoleLogEnabled() && entry.Severity >= defaultLogLevel) {
		static bool tty = StreamLogger::IsTty(std::cout);

		StreamLogger::ProcessLogEntry(std::cout, tty, entry);
	}
}

/**
 * Retrieves the minimum severity for this logger.
 *
 * @returns The minimum severity.
 */
LogSeverity Logger::GetMinSeverity(void) const
{
	String severity = GetSeverity();
	if (severity.IsEmpty())
		return LogInformation;
	else
		return Logger::StringToSeverity(severity);
}

/**
 * Converts a severity enum value to a string.
 *
 * @param severity The severity value.
 */
String Logger::SeverityToString(LogSeverity severity)
{
	switch (severity) {
		case LogDebug:
			return "debug";
		case LogInformation:
			return "information";
		case LogWarning:
			return "warning";
		case LogCritical:
			return "critical";
		default:
			BOOST_THROW_EXCEPTION(std::invalid_argument("Invalid severity."));
	}
}

/**
 * Converts a string to a severity enum value.
 *
 * @param severity The severity.
 */
LogSeverity Logger::StringToSeverity(const String& severity)
{
	if (severity == "debug")
		return LogDebug;
	else if (severity == "information")
		return LogInformation;
	else if (severity == "warning")
		return LogWarning;
	else if (severity == "critical")
		return LogCritical;
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Invalid severity: " + severity));
}

void Logger::DisableConsoleLog(void)
{
	m_ConsoleLogEnabled = false;
}

bool Logger::IsConsoleLogEnabled(void)
{
	return m_ConsoleLogEnabled;
}

