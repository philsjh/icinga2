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

#include "base/logger_fwd.h"
#include "base/objectlock.h"
#include "base/convert.h"
#include "base/utility.h"
#include "base/application.h"
#include "base/dynamictype.h"
#include "base/exception.h"
#include "base/context.h"
#include "base/statsfunction.h"
#include "db_ido/dbtype.h"
#include "db_ido/dbvalue.h"
#include "db_ido_pgsql/idopgsqlconnection.h"
#include <boost/tuple/tuple.hpp>
#include <boost/foreach.hpp>

using namespace icinga;

#define SCHEMA_VERSION "1.11.0"

REGISTER_TYPE(IdoPgsqlConnection);

REGISTER_STATSFUNCTION(IdoPgsqlConnectionStats, &IdoPgsqlConnection::StatsFunc);

Value IdoPgsqlConnection::StatsFunc(Dictionary::Ptr& status, Dictionary::Ptr& perfdata)
{
	Dictionary::Ptr nodes = make_shared<Dictionary>();

	BOOST_FOREACH(const IdoPgsqlConnection::Ptr& idopgsqlconnection, DynamicType::GetObjects<IdoPgsqlConnection>()) {
		size_t items = idopgsqlconnection->m_QueryQueue.GetLength();

		Dictionary::Ptr stats = make_shared<Dictionary>();
		stats->Set("version", SCHEMA_VERSION);
		stats->Set("instance_name", idopgsqlconnection->GetInstanceName());
		stats->Set("query_queue_items", items);

		nodes->Set(idopgsqlconnection->GetName(), stats);

		perfdata->Set("idopgsqlconnection_" + idopgsqlconnection->GetName() + "_query_queue_items", Convert::ToDouble(items));
	}

	status->Set("idopgsqlconnection", nodes);

	return 0;
}

void IdoPgsqlConnection::Start(void)
{
	DbConnection::Start();

	m_Connection = NULL;

	m_QueryQueue.SetExceptionCallback(boost::bind(&IdoPgsqlConnection::ExceptionHandler, this, _1));

	m_TxTimer = make_shared<Timer>();
	m_TxTimer->SetInterval(5);
	m_TxTimer->OnTimerExpired.connect(boost::bind(&IdoPgsqlConnection::TxTimerHandler, this));
	m_TxTimer->Start();

	m_ReconnectTimer = make_shared<Timer>();
	m_ReconnectTimer->SetInterval(10);
	m_ReconnectTimer->OnTimerExpired.connect(boost::bind(&IdoPgsqlConnection::ReconnectTimerHandler, this));
	m_ReconnectTimer->Start();
	m_ReconnectTimer->Reschedule(0);

	ASSERT(PQisthreadsafe());
}

void IdoPgsqlConnection::Stop(void)
{
	m_QueryQueue.Enqueue(boost::bind(&IdoPgsqlConnection::Disconnect, this));
	m_QueryQueue.Join();
}

void IdoPgsqlConnection::ExceptionHandler(boost::exception_ptr exp)
{
	Log(LogCritical, "db_ido_pgsql", "Exception during database operation: " + DiagnosticInformation(exp));

	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if (m_Connection) {
		PQfinish(m_Connection);
		m_Connection = NULL;
	}
}

void IdoPgsqlConnection::AssertOnWorkQueue(void)
{
	ASSERT(boost::this_thread::get_id() == m_QueryQueue.GetThreadId());
}

void IdoPgsqlConnection::Disconnect(void)
{
	AssertOnWorkQueue();

	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if (!m_Connection)
		return;

	Query("COMMIT");
	PQfinish(m_Connection);

	m_Connection = NULL;
}

void IdoPgsqlConnection::TxTimerHandler(void)
{
	m_QueryQueue.Enqueue(boost::bind(&IdoPgsqlConnection::NewTransaction, this), true);
}

void IdoPgsqlConnection::NewTransaction(void)
{
	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if (!m_Connection)
		return;

	Query("COMMIT");
	Query("BEGIN");
}

void IdoPgsqlConnection::ReconnectTimerHandler(void)
{
	m_QueryQueue.Enqueue(boost::bind(&IdoPgsqlConnection::Reconnect, this));
}

void IdoPgsqlConnection::Reconnect(void)
{
	AssertOnWorkQueue();

	CONTEXT("Reconnecting to PostgreSQL IDO database '" + GetName() + "'");

	std::vector<DbObject::Ptr> active_dbobjs;

	{
		boost::mutex::scoped_lock lock(m_ConnectionMutex);

		bool reconnect = false;

		if (m_Connection) {
			/* Check if we're really still connected */
			try {
				Query("SELECT 1");
				return;
			} catch (const std::exception&) {
				PQfinish(m_Connection);
				m_Connection = NULL;
				reconnect = true;
			}
		}

		ClearIDCache();

		String ihost, iport, iuser, ipasswd, idb;
		const char *host, *port, *user , *passwd, *db;

		ihost = GetHost();
		iport = GetPort();
		iuser = GetUser();
		ipasswd = GetPassword();
		idb = GetDatabase();

		host = (!ihost.IsEmpty()) ? ihost.CStr() : NULL;
		port = (!iport.IsEmpty()) ? iport.CStr() : NULL;
		user = (!iuser.IsEmpty()) ? iuser.CStr() : NULL;
		passwd = (!ipasswd.IsEmpty()) ? ipasswd.CStr() : NULL;
		db = (!idb.IsEmpty()) ? idb.CStr() : NULL;

		m_Connection = PQsetdbLogin(host, port, NULL, NULL, db, user, passwd);

		if (!m_Connection)
			return;

		if (PQstatus(m_Connection) != CONNECTION_OK) {
			String message = PQerrorMessage(m_Connection);
			PQfinish(m_Connection);
			m_Connection = NULL;

			BOOST_THROW_EXCEPTION(std::runtime_error(message));
		}

		String dbVersionName = "idoutils";
		IdoPgsqlResult result = Query("SELECT version FROM " + GetTablePrefix() + "dbversion WHERE name='" + Escape(dbVersionName) + "'");

		Dictionary::Ptr version_row = FetchRow(result, 0);

		if (!version_row)
			BOOST_THROW_EXCEPTION(std::runtime_error("Schema does not provide any valid version! Verify your schema installation."));

		String version = version_row->Get("version");

		if (Utility::CompareVersion(SCHEMA_VERSION, version) < 0) {
			BOOST_THROW_EXCEPTION(std::runtime_error("Schema version '" + version + "' does not match the required version '" +
			   SCHEMA_VERSION + "'! Please check the upgrade documentation."));
		}

		String instanceName = GetInstanceName();

		result = Query("SELECT instance_id FROM " + GetTablePrefix() + "instances WHERE instance_name = '" + Escape(instanceName) + "'");

		Dictionary::Ptr row = FetchRow(result, 0);

		if (!row) {
			Query("INSERT INTO " + GetTablePrefix() + "instances (instance_name, instance_description) VALUES ('" + Escape(instanceName) + "', '" + Escape(GetInstanceDescription()) + "')");
			m_InstanceID = GetSequenceValue(GetTablePrefix() + "instances", "instance_id");
		} else {
			m_InstanceID = DbReference(row->Get("instance_id"));
		}

		std::ostringstream msgbuf;
		msgbuf << "pgSQL IDO instance id: " << static_cast<long>(m_InstanceID) << " (schema version: '" + version + "')";
		Log(LogInformation, "db_ido_pgsql", msgbuf.str());

		/* record connection */
		Query("INSERT INTO " + GetTablePrefix() + "conninfo " +
		    "(instance_id, connect_time, last_checkin_time, agent_name, agent_version, connect_type, data_start_time) VALUES ("
		    + Convert::ToString(static_cast<long>(m_InstanceID)) + ", NOW(), NOW(), 'icinga2 db_ido_pgsql', '" + Escape(Application::GetVersion())
		    + "', '" + (reconnect ? "RECONNECT" : "INITIAL") + "', NOW())");

		/* clear config tables for the initial config dump */
		PrepareDatabase();

		std::ostringstream q1buf;
		q1buf << "SELECT object_id, objecttype_id, name1, name2, is_active FROM " + GetTablePrefix() + "objects WHERE instance_id = " << static_cast<long>(m_InstanceID);
		result = Query(q1buf.str());

		int index = 0;
		while ((row = FetchRow(result, index))) {
			index++;

			DbType::Ptr dbtype = DbType::GetByID(row->Get("objecttype_id"));

			if (!dbtype)
				continue;

			DbObject::Ptr dbobj = dbtype->GetOrCreateObjectByName(row->Get("name1"), row->Get("name2"));
			SetObjectID(dbobj, DbReference(row->Get("object_id")));
			SetObjectActive(dbobj, row->Get("is_active"));

			if (GetObjectActive(dbobj))
				active_dbobjs.push_back(dbobj);
		}

		Query("BEGIN");
	}

	UpdateAllObjects();

	/* deactivate all deleted configuration objects */
	BOOST_FOREACH(const DbObject::Ptr& dbobj, active_dbobjs) {
		if (dbobj->GetObject() == NULL) {
			Log(LogDebug, "db_ido", "Deactivate deleted object name1: '" + Convert::ToString(dbobj->GetName1() +
			    "' name2: '" + Convert::ToString(dbobj->GetName2() + "'.")));
			DeactivateObject(dbobj);
		}
	}
}

void IdoPgsqlConnection::ClearConfigTable(const String& table)
{
	Query("DELETE FROM " + GetTablePrefix() + table + " WHERE instance_id = " + Convert::ToString(static_cast<long>(m_InstanceID)));
}

IdoPgsqlResult IdoPgsqlConnection::Query(const String& query)
{
	AssertOnWorkQueue();

	Log(LogDebug, "db_ido_pgsql", "Query: " + query);

	PGresult *result = PQexec(m_Connection, query.CStr());

	if (!result)
		BOOST_THROW_EXCEPTION(
		    database_error()
		        << errinfo_database_query(query)
		);

	char *rowCount = PQcmdTuples(result);
	m_AffectedRows = atoi(rowCount);

	if (PQresultStatus(result) == PGRES_COMMAND_OK)
		return IdoPgsqlResult();

	if (PQresultStatus(result) != PGRES_TUPLES_OK) {
		String message = PQresultErrorMessage(result);
		PQclear(result);

		BOOST_THROW_EXCEPTION(
		    database_error()
		        << errinfo_message(message)
		        << errinfo_database_query(query)
		);
	}

	return IdoPgsqlResult(result, std::ptr_fun(PQclear));
}

DbReference IdoPgsqlConnection::GetSequenceValue(const String& table, const String& column)
{
	AssertOnWorkQueue();

	IdoPgsqlResult result = Query("SELECT CURRVAL(pg_get_serial_sequence('" + Escape(table) + "', '" + Escape(column) + "')) AS id");

	Dictionary::Ptr row = FetchRow(result, 0);

	ASSERT(row);

	std::ostringstream msgbuf;
	msgbuf << "Sequence Value: " << row->Get("id");
	Log(LogDebug, "db_ido_pgsql", msgbuf.str());

	return DbReference(Convert::ToLong(row->Get("id")));
}

int IdoPgsqlConnection::GetAffectedRows(void)
{
	AssertOnWorkQueue();

	return m_AffectedRows;
}

String IdoPgsqlConnection::Escape(const String& s)
{
	AssertOnWorkQueue();

	size_t length = s.GetLength();
	char *to = new char[s.GetLength() * 2 + 1];

	PQescapeStringConn(m_Connection, to, s.CStr(), length, NULL);

	String result = String(to);

	delete [] to;

	return result;
}

Dictionary::Ptr IdoPgsqlConnection::FetchRow(const IdoPgsqlResult& result, int row)
{
	AssertOnWorkQueue();

	if (row >= PQntuples(result.get()))
		return Dictionary::Ptr();

	int columns = PQnfields(result.get());

	Dictionary::Ptr dict = make_shared<Dictionary>();

	for (int column = 0; column < columns; column++) {
		Value value;

		if (!PQgetisnull(result.get(), row, column))
			value = PQgetvalue(result.get(), row, column);

		dict->Set(PQfname(result.get(), column), value);
	}

	return dict;
}

void IdoPgsqlConnection::ActivateObject(const DbObject::Ptr& dbobj)
{
	boost::mutex::scoped_lock lock(m_ConnectionMutex);
	InternalActivateObject(dbobj);
}

void IdoPgsqlConnection::InternalActivateObject(const DbObject::Ptr& dbobj)
{
	if (!m_Connection)
		return;

	DbReference dbref = GetObjectID(dbobj);
	std::ostringstream qbuf;

	if (!dbref.IsValid()) {
		qbuf << "INSERT INTO " + GetTablePrefix() + "objects (instance_id, objecttype_id, name1, name2, is_active) VALUES ("
		      << static_cast<long>(m_InstanceID) << ", " << dbobj->GetType()->GetTypeID() << ", "
		      << "'" << Escape(dbobj->GetName1()) << "', '" << Escape(dbobj->GetName2()) << "', 1)";
		Query(qbuf.str());
		SetObjectID(dbobj, GetSequenceValue(GetTablePrefix() + "objects", "object_id"));
	} else {
		qbuf << "UPDATE " + GetTablePrefix() + "objects SET is_active = 1 WHERE object_id = " << static_cast<long>(dbref);
		Query(qbuf.str());
	}
}

void IdoPgsqlConnection::DeactivateObject(const DbObject::Ptr& dbobj)
{
	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if (!m_Connection)
		return;

	DbReference dbref = GetObjectID(dbobj);

	if (!dbref.IsValid())
		return;

	std::ostringstream qbuf;
	qbuf << "UPDATE " + GetTablePrefix() + "objects SET is_active = 0 WHERE object_id = " << static_cast<long>(dbref);
	Query(qbuf.str());

	/* Note that we're _NOT_ clearing the db refs via SetReference/SetConfigUpdate/SetStatusUpdate
	 * because the object is still in the database. */
}

/* caller must hold m_ConnectionMutex */
bool IdoPgsqlConnection::FieldToEscapedString(const String& key, const Value& value, Value *result)
{
	if (key == "instance_id") {
		*result = static_cast<long>(m_InstanceID);
		return true;
	}
	if (key == "notification_id") {
		*result = static_cast<long>(GetNotificationInsertID(value));
		return true;
	}

	Value rawvalue = DbValue::ExtractValue(value);

	if (rawvalue.IsObjectType<DynamicObject>()) {
		DbObject::Ptr dbobjcol = DbObject::GetOrCreateByObject(rawvalue);

		if (!dbobjcol) {
			*result = 0;
			return true;
		}

		DbReference dbrefcol;

		if (DbValue::IsObjectInsertID(value)) {
			dbrefcol = GetInsertID(dbobjcol);

			ASSERT(dbrefcol.IsValid());
		} else {
			dbrefcol = GetObjectID(dbobjcol);

			if (!dbrefcol.IsValid()) {
				InternalActivateObject(dbobjcol);

				dbrefcol = GetObjectID(dbobjcol);

				if (!dbrefcol.IsValid())
					return false;
			}
		}

		*result = static_cast<long>(dbrefcol);
	} else if (DbValue::IsTimestamp(value)) {
		long ts = rawvalue;
		std::ostringstream msgbuf;
		msgbuf << "TO_TIMESTAMP(" << ts << ")";
		*result = Value(msgbuf.str());
	} else if (DbValue::IsTimestampNow(value)) {
		*result = "NOW()";
	} else {
		*result = "'" + Escape(rawvalue) + "'";
	}

	return true;
}

void IdoPgsqlConnection::ExecuteQuery(const DbQuery& query)
{
	ASSERT(query.Category != DbCatInvalid);

	m_QueryQueue.Enqueue(boost::bind(&IdoPgsqlConnection::InternalExecuteQuery, this, query, (DbQueryType *)NULL), true);
}

void IdoPgsqlConnection::InternalExecuteQuery(const DbQuery& query, DbQueryType *typeOverride)
{
	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if ((query.Category & GetCategories()) == 0)
		return;

	if (!m_Connection)
		return;

	std::ostringstream qbuf, where;
	int type;

	if (query.WhereCriteria) {
		where << " WHERE ";

		ObjectLock olock(query.WhereCriteria);
		Value value;
		bool first = true;

		BOOST_FOREACH(const Dictionary::Pair& kv, query.WhereCriteria) {
			if (!FieldToEscapedString(kv.first, kv.second, &value))
				return;

			if (!first)
				where << " AND ";

			where << kv.first << " = " << value;

			if (first)
				first = false;
		}
	}

	type = typeOverride ? *typeOverride : query.Type;

	bool upsert = false;

	if ((type & DbQueryInsert) && (type & DbQueryUpdate)) {
		bool hasid = false;

		ASSERT(query.Object);

		if (query.ConfigUpdate)
			hasid = GetConfigUpdate(query.Object);
		else if (query.StatusUpdate)
			hasid = GetStatusUpdate(query.Object);
		else
			ASSERT(!"Invalid query flags.");

		if (!hasid)
			upsert = true;

		type = DbQueryUpdate;
	}

	switch (type) {
		case DbQueryInsert:
			qbuf << "INSERT INTO " << GetTablePrefix() << query.Table;
			break;
		case DbQueryUpdate:
			qbuf << "UPDATE " << GetTablePrefix() << query.Table << " SET";
			break;
		case DbQueryDelete:
			qbuf << "DELETE FROM " << GetTablePrefix() << query.Table;
			break;
		default:
			ASSERT(!"Invalid query type.");
	}

	if (type == DbQueryInsert || type == DbQueryUpdate) {
		std::ostringstream colbuf, valbuf;

		ObjectLock olock(query.Fields);

		Value value;
		bool first = true;
		BOOST_FOREACH(const Dictionary::Pair& kv, query.Fields) {
			if (kv.second.IsEmpty())
				continue;

			if (!FieldToEscapedString(kv.first, kv.second, &value))
				return;

			if (type == DbQueryInsert) {
				if (!first) {
					colbuf << ", ";
					valbuf << ", ";
				}

				colbuf << kv.first;
				valbuf << value;
			} else {
				if (!first)
					qbuf << ", ";

				qbuf << " " << kv.first << " = " << value;
			}

			if (first)
				first = false;
		}

		if (type == DbQueryInsert)
			qbuf << " (" << colbuf.str() << ") VALUES (" << valbuf.str() << ")";
	}

	if (type != DbQueryInsert)
		qbuf << where.str();

	Query(qbuf.str());

	if (upsert && GetAffectedRows() == 0) {
		lock.unlock();

		DbQueryType to = DbQueryInsert;
		InternalExecuteQuery(query, &to);

		return;
	}

	if (query.Object) {
		if (query.ConfigUpdate)
			SetConfigUpdate(query.Object, true);
		else if (query.StatusUpdate)
			SetStatusUpdate(query.Object, true);

		if (type == DbQueryInsert && query.ConfigUpdate) {
			String idField = query.IdColumn;

			if (idField.IsEmpty())
				idField = query.Table.SubStr(0, query.Table.GetLength() - 1) + "_id";

			SetInsertID(query.Object, GetSequenceValue(GetTablePrefix() + query.Table, idField));
		}
	}

	if (type == DbQueryInsert && query.Table == "notifications" && query.NotificationObject) { // FIXME remove hardcoded table name
		String idField = "notification_id";
		SetNotificationInsertID(query.NotificationObject, GetSequenceValue(GetTablePrefix() + query.Table, idField));
		Log(LogDebug, "db_ido", "saving contactnotification notification_id=" + Convert::ToString(static_cast<long>(GetSequenceValue(GetTablePrefix() + query.Table, idField))));
	}
}

void IdoPgsqlConnection::CleanUpExecuteQuery(const String& table, const String& time_column, double max_age)
{
	m_QueryQueue.Enqueue(boost::bind(&IdoPgsqlConnection::InternalCleanUpExecuteQuery, this, table, time_column, max_age), true);
}

void IdoPgsqlConnection::InternalCleanUpExecuteQuery(const String& table, const String& time_column, double max_age)
{
	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if (!m_Connection)
		return;

	Query("DELETE FROM " + GetTablePrefix() + table + " WHERE instance_id = " +
	    Convert::ToString(static_cast<long>(m_InstanceID)) + " AND " + time_column +
	    " < TO_TIMESTAMP(" + Convert::ToString(static_cast<long>(max_age)) + ")");
}

void IdoPgsqlConnection::FillIDCache(const DbType::Ptr& type)
{
	String query = "SELECT " + type->GetIDColumn() + " AS object_id, " + type->GetTable() + "_id FROM " + GetTablePrefix() + type->GetTable() + "s";
	IdoPgsqlResult result = Query(query);

	Dictionary::Ptr row;

	int index = 0;
	while ((row = FetchRow(result, index))) {
		index++;
		SetInsertID(type, DbReference(row->Get("object_id")), DbReference(row->Get(type->GetTable() + "_id")));
	}
}
