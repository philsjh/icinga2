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

#include "perfdata/graphitewriter.h"
#include "icinga/service.h"
#include "icinga/macroprocessor.h"
#include "icinga/icingaapplication.h"
#include "icinga/compatutility.h"
#include "icinga/perfdatavalue.h"
#include "base/tcpsocket.h"
#include "base/dynamictype.h"
#include "base/objectlock.h"
#include "base/logger_fwd.h"
#include "base/convert.h"
#include "base/utility.h"
#include "base/application.h"
#include "base/stream.h"
#include "base/networkstream.h"
#include "base/exception.h"
#include "base/statsfunction.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/replace.hpp>

using namespace icinga;

REGISTER_TYPE(GraphiteWriter);

REGISTER_STATSFUNCTION(GraphiteWriterStats, &GraphiteWriter::StatsFunc);

Value GraphiteWriter::StatsFunc(Dictionary::Ptr& status, Dictionary::Ptr& perfdata)
{
	Dictionary::Ptr nodes = make_shared<Dictionary>();

	BOOST_FOREACH(const GraphiteWriter::Ptr& graphitewriter, DynamicType::GetObjects<GraphiteWriter>()) {
		nodes->Set(graphitewriter->GetName(), 1); //add more stats
	}

	status->Set("graphitewriter", nodes);

	return 0;
}

void GraphiteWriter::Start(void)
{
	DynamicObject::Start();

	m_ReconnectTimer = make_shared<Timer>();
	m_ReconnectTimer->SetInterval(10);
	m_ReconnectTimer->OnTimerExpired.connect(boost::bind(&GraphiteWriter::ReconnectTimerHandler, this));
	m_ReconnectTimer->Start();
	m_ReconnectTimer->Reschedule(0);

	Service::OnNewCheckResult.connect(boost::bind(&GraphiteWriter::CheckResultHandler, this, _1, _2));
}

void GraphiteWriter::ReconnectTimerHandler(void)
{
	try {
		if (m_Stream) {
			m_Stream->Write("\n", 1);
			Log(LogWarning, "perfdata", "GraphiteWriter already connected on socket on host '" + GetHost() + "' port '" + GetPort() + "'.");
			return;
		}
	} catch (const std::exception& ex) {
		Log(LogWarning, "perfdata", "GraphiteWriter socket on host '" + GetHost() + "' port '" + GetPort() + "' gone. Attempting to reconnect.");
	}

	TcpSocket::Ptr socket = make_shared<TcpSocket>();

	Log(LogDebug, "perfdata", "GraphiteWriter: Reconnect to tcp socket on host '" + GetHost() + "' port '" + GetPort() + "'.");
	socket->Connect(GetHost(), GetPort());

	m_Stream = make_shared<NetworkStream>(socket);
}

void GraphiteWriter::CheckResultHandler(const Checkable::Ptr& checkable, const CheckResult::Ptr& cr)
{
	if (!IcingaApplication::GetInstance()->GetEnablePerfdata() || !checkable->GetEnablePerfdata())
		return;

	Service::Ptr service = dynamic_pointer_cast<Service>(checkable);
	Host::Ptr host;

	if (service)
		host = service->GetHost();
	else
		host = static_pointer_cast<Host>(checkable);

	String hostName = host->GetName();
	SanitizeMetric(hostName);

	String prefix;

	if (service) {
		String serviceName = service->GetShortName();
		SanitizeMetric(serviceName);
		prefix = "icinga." + hostName + "." + serviceName;

		SendMetric(prefix, "state", service->GetState());
	} else {
		prefix = "icinga." + hostName;

		SendMetric(prefix, "state", host->GetState());
	}

	SendMetric(prefix, "current_attempt", checkable->GetCheckAttempt());
	SendMetric(prefix, "max_check_attempts", checkable->GetMaxCheckAttempts());
	SendMetric(prefix, "state_type", checkable->GetStateType());
	SendMetric(prefix, "reachable", checkable->IsReachable());
	SendMetric(prefix, "latency", Service::CalculateLatency(cr));
	SendMetric(prefix, "execution_time", Service::CalculateExecutionTime(cr));
	SendPerfdata(prefix, cr);
}

void GraphiteWriter::SendPerfdata(const String& prefix, const CheckResult::Ptr& cr)
{
	Value pdv = cr->GetPerformanceData();

	if (!pdv.IsObjectType<Dictionary>())
		return;

	Dictionary::Ptr perfdata = pdv;

	ObjectLock olock(perfdata);
	BOOST_FOREACH(const Dictionary::Pair& kv, perfdata) {
		double valueNum;

		if (!kv.second.IsObjectType<PerfdataValue>())
			valueNum = kv.second;
		else
			valueNum = static_cast<PerfdataValue::Ptr>(kv.second)->GetValue();

		String escaped_key = kv.first;
		SanitizeMetric(escaped_key);
		boost::algorithm::replace_all(escaped_key, "::", ".");

		SendMetric(prefix, escaped_key, valueNum);
	}
}

void GraphiteWriter::SendMetric(const String& prefix, const String& name, double value)
{
	std::ostringstream msgbuf;
	msgbuf << prefix << "." << name << " " << value << " " << static_cast<long>(Utility::GetTime()) << "\n";

	String metric = msgbuf.str();
	Log(LogDebug, "perfdata", "GraphiteWriter: Add to metric list:'" + metric + "'.");

	ObjectLock olock(this);

	if (!m_Stream)
		return;

	try {
		m_Stream->Write(metric.CStr(), metric.GetLength());
	} catch (const std::exception& ex) {
		std::ostringstream msgbuf;
		msgbuf << "Exception thrown while writing to the Graphite socket: " << std::endl
		       << DiagnosticInformation(ex);

		Log(LogCritical, "base", msgbuf.str());

		m_Stream.reset();
	}
}

void GraphiteWriter::SanitizeMetric(String& str)
{
	boost::replace_all(str, " ", "_");
	boost::replace_all(str, ".", "_");
	boost::replace_all(str, "-", "_");
	boost::replace_all(str, "\\", "_");
	boost::replace_all(str, "/", "_");
}
