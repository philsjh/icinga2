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

#include "db_ido/dbconnection.h"
#include "db_ido/dbvalue.h"
#include "icinga/icingaapplication.h"
#include "icinga/host.h"
#include "icinga/service.h"
#include "base/dynamictype.h"
#include "base/convert.h"
#include "base/objectlock.h"
#include "base/utility.h"
#include "base/initialize.h"
#include "base/logger_fwd.h"
#include <boost/foreach.hpp>

using namespace icinga;

REGISTER_TYPE(DbConnection);

Timer::Ptr DbConnection::m_ProgramStatusTimer;

INITIALIZE_ONCE(&DbConnection::StaticInitialize);

void DbConnection::Start(void)
{
	DynamicObject::Start();

	DbObject::OnQuery.connect(boost::bind(&DbConnection::ExecuteQuery, this, _1));

	m_CleanUpTimer = make_shared<Timer>();
	m_CleanUpTimer->SetInterval(60);
	m_CleanUpTimer->OnTimerExpired.connect(boost::bind(&DbConnection::CleanUpHandler, this));
	m_CleanUpTimer->Start();
}

void DbConnection::StaticInitialize(void)
{
	m_ProgramStatusTimer = make_shared<Timer>();
	m_ProgramStatusTimer->SetInterval(10);
	m_ProgramStatusTimer->OnTimerExpired.connect(boost::bind(&DbConnection::ProgramStatusHandler));
	m_ProgramStatusTimer->Start();
}

void DbConnection::InsertRuntimeVariable(const String& key, const Value& value)
{
	DbQuery query;
	query.Table = "runtimevariables";
	query.Type = DbQueryInsert;
	query.Category = DbCatProgramStatus;
	query.Fields = make_shared<Dictionary>();
	query.Fields->Set("instance_id", 0); /* DbConnection class fills in real ID */
	query.Fields->Set("varname", key);
	query.Fields->Set("varvalue", value);
	DbObject::OnQuery(query);
}

void DbConnection::ProgramStatusHandler(void)
{
	DbQuery query1;
	query1.Table = "programstatus";
	query1.Type = DbQueryDelete;
	query1.Category = DbCatProgramStatus;
	query1.WhereCriteria = make_shared<Dictionary>();
	query1.WhereCriteria->Set("instance_id", 0);  /* DbConnection class fills in real ID */
	DbObject::OnQuery(query1);

	DbQuery query2;
	query2.Table = "programstatus";
	query2.IdColumn = "programstatus_id";
	query2.Type = DbQueryInsert;
	query2.Category = DbCatProgramStatus;

	query2.Fields = make_shared<Dictionary>();
	query2.Fields->Set("instance_id", 0); /* DbConnection class fills in real ID */
	query2.Fields->Set("status_update_time", DbValue::FromTimestamp(Utility::GetTime()));
	query2.Fields->Set("program_start_time", DbValue::FromTimestamp(Application::GetStartTime()));
	query2.Fields->Set("is_currently_running", 1);
	query2.Fields->Set("process_id", Utility::GetPid());
	query2.Fields->Set("daemon_mode", 1);
	query2.Fields->Set("last_command_check", DbValue::FromTimestamp(Utility::GetTime()));
	query2.Fields->Set("notifications_enabled", (IcingaApplication::GetInstance()->GetEnableNotifications() ? 1 : 0));
	query2.Fields->Set("active_host_checks_enabled", (IcingaApplication::GetInstance()->GetEnableHostChecks() ? 1 : 0));
	query2.Fields->Set("passive_host_checks_enabled", 1);
	query2.Fields->Set("active_service_checks_enabled", (IcingaApplication::GetInstance()->GetEnableServiceChecks() ? 1 : 0));
	query2.Fields->Set("passive_service_checks_enabled", 1);
	query2.Fields->Set("event_handlers_enabled", (IcingaApplication::GetInstance()->GetEnableEventHandlers() ? 1 : 0));
	query2.Fields->Set("flap_detection_enabled", (IcingaApplication::GetInstance()->GetEnableFlapping() ? 1 : 0));
	query2.Fields->Set("process_performance_data", (IcingaApplication::GetInstance()->GetEnablePerfdata() ? 1 : 0));
	DbObject::OnQuery(query2);

	DbQuery query3;
	query3.Table = "runtimevariables";
	query3.Type = DbQueryDelete;
	query3.Category = DbCatProgramStatus;
	query3.WhereCriteria = make_shared<Dictionary>();
	query3.WhereCriteria->Set("instance_id", 0);  /* DbConnection class fills in real ID */
	DbObject::OnQuery(query3);

	InsertRuntimeVariable("total_services", std::distance(DynamicType::GetObjects<Service>().first, DynamicType::GetObjects<Service>().second));
	InsertRuntimeVariable("total_scheduled_services", std::distance(DynamicType::GetObjects<Service>().first, DynamicType::GetObjects<Service>().second));
	InsertRuntimeVariable("total_hosts", std::distance(DynamicType::GetObjects<Host>().first, DynamicType::GetObjects<Host>().second));
	InsertRuntimeVariable("total_scheduled_hosts", std::distance(DynamicType::GetObjects<Host>().first, DynamicType::GetObjects<Host>().second));

	Dictionary::Ptr vars = IcingaApplication::GetInstance()->GetVars();

	if (!vars)
		return;

	Log(LogDebug, "db_ido", "Dumping global vars for icinga application");

	ObjectLock olock(vars);

	BOOST_FOREACH(const Dictionary::Pair& kv, vars) {
		if (!kv.first.IsEmpty()) {
			Log(LogDebug, "db_ido", "icinga application customvar key: '" + kv.first + "' value: '" + Convert::ToString(kv.second) + "'");

			Dictionary::Ptr fields4 = make_shared<Dictionary>();
			fields4->Set("varname", Convert::ToString(kv.first));
			fields4->Set("varvalue", Convert::ToString(kv.second));
			fields4->Set("config_type", 1);
			fields4->Set("has_been_modified", 0);
			fields4->Set("instance_id", 0); /* DbConnection class fills in real ID */

			DbQuery query4;
			query4.Table = "customvariables";
			query4.Type = DbQueryInsert;
			query4.Category = DbCatConfig;
			query4.Fields = fields4;
			DbObject::OnQuery(query4);
		}
	}
}

void DbConnection::CleanUpHandler(void)
{
	long now = static_cast<long>(Utility::GetTime());

	struct {
		String name;
		String time_column;
	} tables[] = {
		{ "acknowledgements", "entry_time" },
		{ "commenthistory", "entry_time" },
		{ "contactnotifications", "start_time" },
		{ "contactnotificationmethods", "start_time" },
		{ "downtimehistory", "entry_time" },
		{ "eventhandlers", "start_time" },
		{ "externalcommands", "entry_time" },
		{ "flappinghistory" "event_time" },
		{ "hostchecks", "start_time" },
		{ "logentries", "logentry_time" },
		{ "notifications", "start_time" },
		{ "processevents", "event_time" },
		{ "statehistory", "state_time" },
		{ "servicechecks", "start_time" },
		{ "systemcommands", "start_time" }
	};

	for (int i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
		double max_age = GetCleanup()->Get(tables[i].name + "_age");

		if (max_age == 0)
			continue;

		CleanUpExecuteQuery(tables[i].name, tables[i].time_column, now - max_age);
		Log(LogDebug, "db_ido", "Cleanup (" + tables[i].name + "): " + Convert::ToString(max_age) +
		    " now: " + Convert::ToString(now) +
		    " old: " + Convert::ToString(now - max_age));
	}

}

void DbConnection::CleanUpExecuteQuery(const String& table, const String& time_column, double max_age)
{
	/* Default handler does nothing. */
}

void DbConnection::SetObjectID(const DbObject::Ptr& dbobj, const DbReference& dbref)
{
	if (dbref.IsValid())
		m_ObjectIDs[dbobj] = dbref;
	else
		m_ObjectIDs.erase(dbobj);
}

DbReference DbConnection::GetObjectID(const DbObject::Ptr& dbobj) const
{
	std::map<DbObject::Ptr, DbReference>::const_iterator it;

	it = m_ObjectIDs.find(dbobj);

	if (it == m_ObjectIDs.end())
		return DbReference();

	return it->second;
}

void DbConnection::SetInsertID(const DbObject::Ptr& dbobj, const DbReference& dbref)
{
	SetInsertID(dbobj->GetType(), GetObjectID(dbobj), dbref);
}

void DbConnection::SetInsertID(const DbType::Ptr& type, const DbReference& objid, const DbReference& dbref)
{
	if (!objid.IsValid())
		return;

	if (dbref.IsValid())
		m_InsertIDs[std::make_pair(type, objid)] = dbref;
	else
		m_InsertIDs.erase(std::make_pair(type, objid));
}

DbReference DbConnection::GetInsertID(const DbObject::Ptr& dbobj) const
{
	return GetInsertID(dbobj->GetType(), GetObjectID(dbobj));
}

DbReference DbConnection::GetInsertID(const DbType::Ptr& type, const DbReference& objid) const
{
	if (!objid.IsValid())
		return DbReference();

	std::map<std::pair<DbType::Ptr, DbReference>, DbReference>::const_iterator it;

	it = m_InsertIDs.find(std::make_pair(type, objid));

	if (it == m_InsertIDs.end())
		return DbReference();

	return it->second;
}

void DbConnection::SetNotificationInsertID(const DynamicObject::Ptr& obj, const DbReference& dbref)
{
	if (dbref.IsValid())
		m_NotificationInsertIDs[obj] = dbref;
	else
		m_NotificationInsertIDs.erase(obj);
}

DbReference DbConnection::GetNotificationInsertID(const DynamicObject::Ptr& obj) const
{
	std::map<DynamicObject::Ptr, DbReference>::const_iterator it;

	it = m_NotificationInsertIDs.find(obj);

	if (it == m_NotificationInsertIDs.end())
		return DbReference();

	return it->second;
}

void DbConnection::SetObjectActive(const DbObject::Ptr& dbobj, bool active)
{
	if (active)
		m_ActiveObjects.insert(dbobj);
	else
		m_ActiveObjects.erase(dbobj);
}

bool DbConnection::GetObjectActive(const DbObject::Ptr& dbobj) const
{
	return (m_ActiveObjects.find(dbobj) != m_ActiveObjects.end());
}

void DbConnection::ClearIDCache(void)
{
	m_ObjectIDs.clear();
	m_InsertIDs.clear();
	m_NotificationInsertIDs.clear();
	m_ActiveObjects.clear();
	m_ConfigUpdates.clear();
	m_StatusUpdates.clear();
}

void DbConnection::SetConfigUpdate(const DbObject::Ptr& dbobj, bool hasupdate)
{
	if (hasupdate)
		m_ConfigUpdates.insert(dbobj);
	else
		m_ConfigUpdates.erase(dbobj);
}

bool DbConnection::GetConfigUpdate(const DbObject::Ptr& dbobj) const
{
	return (m_ConfigUpdates.find(dbobj) != m_ConfigUpdates.end());
}

void DbConnection::SetStatusUpdate(const DbObject::Ptr& dbobj, bool hasupdate)
{
	if (hasupdate)
		m_StatusUpdates.insert(dbobj);
	else
		m_StatusUpdates.erase(dbobj);
}

bool DbConnection::GetStatusUpdate(const DbObject::Ptr& dbobj) const
{
	return (m_StatusUpdates.find(dbobj) != m_StatusUpdates.end());
}

void DbConnection::ExecuteQuery(const DbQuery&)
{
	/* Default handler does nothing. */
}

void DbConnection::UpdateAllObjects(void)
{
	DynamicType::Ptr type;
	BOOST_FOREACH(const DynamicType::Ptr& dt, DynamicType::GetTypes()) {
		BOOST_FOREACH(const DynamicObject::Ptr& object, dt->GetObjects()) {
			DbObject::Ptr dbobj = DbObject::GetOrCreateByObject(object);

			if (dbobj) {
				if (!GetObjectActive(dbobj))
					ActivateObject(dbobj);

				dbobj->SendConfigUpdate();
				dbobj->SendStatusUpdate();
			}
		}
	}
}

void DbConnection::PrepareDatabase(void)
{
	/* 
	 * only clear tables on reconnect which
	 * cannot be updated by their existing ids
	 * for details check https://dev.icinga.org/issues/5565
	 */

	//ClearConfigTable("commands");
	ClearConfigTable("comments");
	ClearConfigTable("contact_addresses");
	ClearConfigTable("contact_notificationcommands");
	ClearConfigTable("contactgroup_members");
	//ClearConfigTable("contactgroups");
	//ClearConfigTable("contacts");
	//ClearConfigTable("contactstatus");
	ClearConfigTable("customvariables");
	ClearConfigTable("customvariablestatus");
	ClearConfigTable("endpoints");
	ClearConfigTable("endpointstatus");
	ClearConfigTable("host_contactgroups");
	ClearConfigTable("host_contacts");
	ClearConfigTable("host_parenthosts");
	ClearConfigTable("hostdependencies");
	ClearConfigTable("hostgroup_members");
	//ClearConfigTable("hostgroups");
	//ClearConfigTable("hosts");
	//ClearConfigTable("hoststatus");
	ClearConfigTable("programstatus");
	ClearConfigTable("scheduleddowntime");
	ClearConfigTable("service_contactgroups");
	ClearConfigTable("service_contacts");
	ClearConfigTable("servicedependencies");
	ClearConfigTable("servicegroup_members");
	//ClearConfigTable("servicegroups");
	//ClearConfigTable("services");
	//ClearConfigTable("servicestatus");
	ClearConfigTable("timeperiod_timeranges");
	//ClearConfigTable("timeperiods");

	BOOST_FOREACH(const DbType::Ptr& type, DbType::GetAllTypes()) {
		FillIDCache(type);
	}
}
