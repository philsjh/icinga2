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
#include "base/statsfunction.h"
#include "db_ido/dbtype.h"
#include "db_ido/dbvalue.h"
#include "db_ido_mysql/idomysqlconnection.h"
#include <boost/tuple/tuple.hpp>
#include <boost/foreach.hpp>

using namespace icinga;

#define SCHEMA_VERSION "1.11.0"

REGISTER_TYPE(IdoMysqlConnection);
REGISTER_STATSFUNCTION(IdoMysqlConnectionStats, &IdoMysqlConnection::StatsFunc);

Value IdoMysqlConnection::StatsFunc(Dictionary::Ptr& status, Dictionary::Ptr& perfdata)
{
	Dictionary::Ptr nodes = make_shared<Dictionary>();

	BOOST_FOREACH(const IdoMysqlConnection::Ptr& idomysqlconnection, DynamicType::GetObjects<IdoMysqlConnection>()) {
		size_t items = idomysqlconnection->m_QueryQueue.GetLength();

		Dictionary::Ptr stats = make_shared<Dictionary>();
		stats->Set("version", SCHEMA_VERSION);
		stats->Set("instance_name", idomysqlconnection->GetInstanceName());
		stats->Set("query_queue_items", items);

		nodes->Set(idomysqlconnection->GetName(), stats);

		perfdata->Set("idomysqlconnection_" + idomysqlconnection->GetName() + "_query_queue_items", Convert::ToDouble(items));
	}

	status->Set("idomysqlconnection", nodes);

	return 0;
}

void IdoMysqlConnection::Start(void)
{
	DbConnection::Start();

	m_Connected = false;

	m_QueryQueue.SetExceptionCallback(boost::bind(&IdoMysqlConnection::ExceptionHandler, this, _1));

	m_TxTimer = make_shared<Timer>();
	m_TxTimer->SetInterval(5);
	m_TxTimer->OnTimerExpired.connect(boost::bind(&IdoMysqlConnection::TxTimerHandler, this));
	m_TxTimer->Start();

	m_ReconnectTimer = make_shared<Timer>();
	m_ReconnectTimer->SetInterval(10);
	m_ReconnectTimer->OnTimerExpired.connect(boost::bind(&IdoMysqlConnection::ReconnectTimerHandler, this));
	m_ReconnectTimer->Start();
	m_ReconnectTimer->Reschedule(0);

	ASSERT(mysql_thread_safe());
}

void IdoMysqlConnection::Stop(void)
{
	m_QueryQueue.Enqueue(boost::bind(&IdoMysqlConnection::Disconnect, this));
	m_QueryQueue.Join();
}

void IdoMysqlConnection::ExceptionHandler(boost::exception_ptr exp)
{
	Log(LogCritical, "db_ido_mysql", "Exception during database operation: " + DiagnosticInformation(exp));

	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if (m_Connected) {
		mysql_close(&m_Connection);

		m_Connected = false;
	}
}

void IdoMysqlConnection::AssertOnWorkQueue(void)
{
	ASSERT(boost::this_thread::get_id() == m_QueryQueue.GetThreadId());
}

void IdoMysqlConnection::Disconnect(void)
{
	AssertOnWorkQueue();

	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if (!m_Connected)
		return;

	Query("COMMIT");
	mysql_close(&m_Connection);

	m_Connected = false;
}

void IdoMysqlConnection::TxTimerHandler(void)
{
	m_QueryQueue.Enqueue(boost::bind(&IdoMysqlConnection::NewTransaction, this), true);
}

void IdoMysqlConnection::NewTransaction(void)
{
	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if (!m_Connected)
		return;

	Query("COMMIT");
	Query("BEGIN");
}

void IdoMysqlConnection::ReconnectTimerHandler(void)
{
	m_QueryQueue.Enqueue(boost::bind(&IdoMysqlConnection::Reconnect, this));
}

void IdoMysqlConnection::Reconnect(void)
{
	AssertOnWorkQueue();

	CONTEXT("Reconnecting to MySQL IDO database '" + GetName() + "'");

	std::vector<DbObject::Ptr> active_dbobjs;

	{
		boost::mutex::scoped_lock lock(m_ConnectionMutex);

		bool reconnect = false;

		if (m_Connected) {
			/* Check if we're really still connected */
			if (mysql_ping(&m_Connection) == 0)
				return;

			mysql_close(&m_Connection);
			m_Connected = false;
			reconnect = true;
		}

		ClearIDCache();

		String ihost, iuser, ipasswd, idb;
		const char *host, *user , *passwd, *db;
		long port;

		ihost = GetHost();
		iuser = GetUser();
		ipasswd = GetPassword();
		idb = GetDatabase();

		host = (!ihost.IsEmpty()) ? ihost.CStr() : NULL;
		port = GetPort();
		user = (!iuser.IsEmpty()) ? iuser.CStr() : NULL;
		passwd = (!ipasswd.IsEmpty()) ? ipasswd.CStr() : NULL;
		db = (!idb.IsEmpty()) ? idb.CStr() : NULL;

		if (!mysql_init(&m_Connection))
			BOOST_THROW_EXCEPTION(std::bad_alloc());

		if (!mysql_real_connect(&m_Connection, host, user, passwd, db, port, NULL, CLIENT_FOUND_ROWS))
			BOOST_THROW_EXCEPTION(std::runtime_error(mysql_error(&m_Connection)));

		m_Connected = true;

		String dbVersionName = "idoutils";
		IdoMysqlResult result = Query("SELECT version FROM " + GetTablePrefix() + "dbversion WHERE name='" + Escape(dbVersionName) + "'");

		Dictionary::Ptr version_row = FetchRow(result);

		if (!version_row)
			BOOST_THROW_EXCEPTION(std::runtime_error("Schema does not provide any valid version! Verify your schema installation."));

		DiscardRows(result);

		String version = version_row->Get("version");

		if (Utility::CompareVersion(SCHEMA_VERSION, version) < 0) {
			BOOST_THROW_EXCEPTION(std::runtime_error("Schema version '" + version + "' does not match the required version '" +
			   SCHEMA_VERSION + "'! Please check the upgrade documentation."));
		}

		String instanceName = GetInstanceName();

		result = Query("SELECT instance_id FROM " + GetTablePrefix() + "instances WHERE instance_name = '" + Escape(instanceName) + "'");

		Dictionary::Ptr row = FetchRow(result);

		if (!row) {
			Query("INSERT INTO " + GetTablePrefix() + "instances (instance_name, instance_description) VALUES ('" + Escape(instanceName) + "', '" + Escape(GetInstanceDescription()) + "')");
			m_InstanceID = GetLastInsertID();
		} else {
			DiscardRows(result);

			m_InstanceID = DbReference(row->Get("instance_id"));
		}

		std::ostringstream msgbuf;
		msgbuf << "MySQL IDO instance id: " << static_cast<long>(m_InstanceID) << " (schema version: '" + version + "')";
		Log(LogInformation, "db_ido_mysql", msgbuf.str());

		/* set session time zone to utc */
		Query("SET SESSION TIME_ZONE='+00:00'");

		/* record connection */
		Query("INSERT INTO " + GetTablePrefix() + "conninfo " +
		    "(instance_id, connect_time, last_checkin_time, agent_name, agent_version, connect_type, data_start_time) VALUES ("
		    + Convert::ToString(static_cast<long>(m_InstanceID)) + ", NOW(), NOW(), 'icinga2 db_ido_mysql', '" + Escape(Application::GetVersion())
		    + "', '" + (reconnect ? "RECONNECT" : "INITIAL") + "', NOW())");

		/* clear config tables for the initial config dump */
		PrepareDatabase();

		std::ostringstream q1buf;
		q1buf << "SELECT object_id, objecttype_id, name1, name2, is_active FROM " + GetTablePrefix() + "objects WHERE instance_id = " << static_cast<long>(m_InstanceID);
		result = Query(q1buf.str());

		while ((row = FetchRow(result))) {
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

void IdoMysqlConnection::ClearConfigTable(const String& table)
{
	Query("DELETE FROM " + GetTablePrefix() + table + " WHERE instance_id = " + Convert::ToString(static_cast<long>(m_InstanceID)));
}

IdoMysqlResult IdoMysqlConnection::Query(const String& query)
{
	AssertOnWorkQueue();

	Log(LogDebug, "db_ido_mysql", "Query: " + query);

	if (mysql_query(&m_Connection, query.CStr()) != 0)
		BOOST_THROW_EXCEPTION(
		    database_error()
		        << errinfo_message(mysql_error(&m_Connection))
			<< errinfo_database_query(query)
		);

	m_AffectedRows = mysql_affected_rows(&m_Connection);

	MYSQL_RES *result = mysql_use_result(&m_Connection);

	if (!result) {
		if (mysql_field_count(&m_Connection) > 0)
			BOOST_THROW_EXCEPTION(
			    database_error()
				<< errinfo_message(mysql_error(&m_Connection))
				<< errinfo_database_query(query)
			);

		return IdoMysqlResult();
	}

	return IdoMysqlResult(result, std::ptr_fun(mysql_free_result));
}

DbReference IdoMysqlConnection::GetLastInsertID(void)
{
	AssertOnWorkQueue();

	return DbReference(mysql_insert_id(&m_Connection));
}

int IdoMysqlConnection::GetAffectedRows(void)
{
	AssertOnWorkQueue();

	return m_AffectedRows;
}

String IdoMysqlConnection::Escape(const String& s)
{
	AssertOnWorkQueue();

	size_t length = s.GetLength();
	char *to = new char[s.GetLength() * 2 + 1];

	mysql_real_escape_string(&m_Connection, to, s.CStr(), length);

	String result = String(to);

	delete [] to;

	return result;
}

Dictionary::Ptr IdoMysqlConnection::FetchRow(const IdoMysqlResult& result)
{
	AssertOnWorkQueue();

	MYSQL_ROW row;
	MYSQL_FIELD *field;
	unsigned long *lengths, i;

	row = mysql_fetch_row(result.get());

	if (!row)
		return Dictionary::Ptr();

	lengths = mysql_fetch_lengths(result.get());

	if (!lengths)
		return Dictionary::Ptr();

	Dictionary::Ptr dict = make_shared<Dictionary>();

	mysql_field_seek(result.get(), 0);
	for (field = mysql_fetch_field(result.get()), i = 0; field; field = mysql_fetch_field(result.get()), i++)
		dict->Set(field->name, String(row[i], row[i] + lengths[i]));

	return dict;
}

void IdoMysqlConnection::DiscardRows(const IdoMysqlResult& result)
{
	Dictionary::Ptr row;

	while ((row = FetchRow(result)))
		; /* empty loop body */
}

void IdoMysqlConnection::ActivateObject(const DbObject::Ptr& dbobj)
{
	boost::mutex::scoped_lock lock(m_ConnectionMutex);
	InternalActivateObject(dbobj);
}

void IdoMysqlConnection::InternalActivateObject(const DbObject::Ptr& dbobj)
{
	if (!m_Connected)
		return;

	DbReference dbref = GetObjectID(dbobj);
	std::ostringstream qbuf;

	if (!dbref.IsValid()) {
		qbuf << "INSERT INTO " + GetTablePrefix() + "objects (instance_id, objecttype_id, name1, name2, is_active) VALUES ("
		      << static_cast<long>(m_InstanceID) << ", " << dbobj->GetType()->GetTypeID() << ", "
		      << "'" << Escape(dbobj->GetName1()) << "', '" << Escape(dbobj->GetName2()) << "', 1)";
		Query(qbuf.str());
		SetObjectID(dbobj, GetLastInsertID());
	} else {
		qbuf << "UPDATE " + GetTablePrefix() + "objects SET is_active = 1 WHERE object_id = " << static_cast<long>(dbref);
		Query(qbuf.str());
	}
}

void IdoMysqlConnection::DeactivateObject(const DbObject::Ptr& dbobj)
{
	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if (!m_Connected)
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
bool IdoMysqlConnection::FieldToEscapedString(const String& key, const Value& value, Value *result)
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
		msgbuf << "FROM_UNIXTIME(" << ts << ")";
		*result = Value(msgbuf.str());
	} else if (DbValue::IsTimestampNow(value)) {
		*result = "NOW()";
	} else {
		*result = "'" + Escape(rawvalue) + "'";
	}

	return true;
}

void IdoMysqlConnection::ExecuteQuery(const DbQuery& query)
{
	ASSERT(query.Category != DbCatInvalid);

	m_QueryQueue.Enqueue(boost::bind(&IdoMysqlConnection::InternalExecuteQuery, this, query, (DbQueryType *)NULL), true);
}

void IdoMysqlConnection::InternalExecuteQuery(const DbQuery& query, DbQueryType *typeOverride)
{
	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if ((query.Category & GetCategories()) == 0)
		return;

	if (!m_Connected)
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

		bool first = true;
		BOOST_FOREACH(const Dictionary::Pair& kv, query.Fields) {
			Value value;

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

		if (type == DbQueryInsert && query.ConfigUpdate)
			SetInsertID(query.Object, GetLastInsertID());
	}

	if (type == DbQueryInsert && query.Table == "notifications" && query.NotificationObject) { // FIXME remove hardcoded table name
		SetNotificationInsertID(query.NotificationObject, GetLastInsertID());
		Log(LogDebug, "db_ido", "saving contactnotification notification_id=" + Convert::ToString(static_cast<long>(GetLastInsertID())));
	}
}

void IdoMysqlConnection::CleanUpExecuteQuery(const String& table, const String& time_column, double max_age)
{
	m_QueryQueue.Enqueue(boost::bind(&IdoMysqlConnection::InternalCleanUpExecuteQuery, this, table, time_column, max_age), true);
}

void IdoMysqlConnection::InternalCleanUpExecuteQuery(const String& table, const String& time_column, double max_age)
{
	boost::mutex::scoped_lock lock(m_ConnectionMutex);

	if (!m_Connected)
		return;

	Query("DELETE FROM " + GetTablePrefix() + table + " WHERE instance_id = " +
	    Convert::ToString(static_cast<long>(m_InstanceID)) + " AND " + time_column +
	    " < FROM_UNIXTIME(" + Convert::ToString(static_cast<long>(max_age)) + ")");
}

void IdoMysqlConnection::FillIDCache(const DbType::Ptr& type)
{
	String query = "SELECT " + type->GetIDColumn() + " AS object_id, " + type->GetTable() + "_id FROM " + GetTablePrefix() + type->GetTable() + "s";
	IdoMysqlResult result = Query(query);

	Dictionary::Ptr row;

	while ((row = FetchRow(result))) {
		SetInsertID(type, DbReference(row->Get("object_id")), DbReference(row->Get(type->GetTable() + "_id")));
	}
}
