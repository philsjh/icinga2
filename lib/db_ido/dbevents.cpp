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

#include "db_ido/dbevents.h"
#include "db_ido/dbtype.h"
#include "db_ido/dbvalue.h"
#include "base/convert.h"
#include "base/objectlock.h"
#include "base/initialize.h"
#include "base/dynamictype.h"
#include "base/utility.h"
#include "remote/endpoint.h"
#include "icinga/notification.h"
#include "icinga/checkcommand.h"
#include "icinga/eventcommand.h"
#include "icinga/externalcommandprocessor.h"
#include "icinga/compatutility.h"
#include "icinga/icingaapplication.h"
#include <boost/foreach.hpp>
#include <boost/algorithm/string/join.hpp>

using namespace icinga;

INITIALIZE_ONCE(&DbEvents::StaticInitialize);

void DbEvents::StaticInitialize(void)
{
	/* Status */
	Service::OnCommentAdded.connect(boost::bind(&DbEvents::AddComment, _1, _2));
	Service::OnCommentRemoved.connect(boost::bind(&DbEvents::RemoveComment, _1, _2));
	Service::OnDowntimeAdded.connect(boost::bind(&DbEvents::AddDowntime, _1, _2));
	Service::OnDowntimeRemoved.connect(boost::bind(&DbEvents::RemoveDowntime, _1, _2));
	Service::OnDowntimeTriggered.connect(boost::bind(&DbEvents::TriggerDowntime, _1, _2));

	/* History */
	Service::OnCommentAdded.connect(boost::bind(&DbEvents::AddCommentHistory, _1, _2));
	Service::OnDowntimeAdded.connect(boost::bind(&DbEvents::AddDowntimeHistory, _1, _2));
	Service::OnAcknowledgementSet.connect(boost::bind(&DbEvents::AddAcknowledgementHistory, _1, _2, _3, _4, _5));

	Service::OnNotificationSentToAllUsers.connect(bind(&DbEvents::AddNotificationHistory, _1, _2, _3, _4, _5, _6, _7));

	Service::OnStateChange.connect(boost::bind(&DbEvents::AddStateChangeHistory, _1, _2, _3));

	Service::OnNewCheckResult.connect(bind(&DbEvents::AddCheckResultLogHistory, _1, _2));
	Service::OnNotificationSentToUser.connect(bind(&DbEvents::AddNotificationSentLogHistory, _1, _2, _3, _4, _5, _6, _7));
	Service::OnFlappingChanged.connect(bind(&DbEvents::AddFlappingLogHistory, _1, _2));
	Service::OnDowntimeTriggered.connect(boost::bind(&DbEvents::AddTriggerDowntimeLogHistory, _1, _2));
	Service::OnDowntimeRemoved.connect(boost::bind(&DbEvents::AddRemoveDowntimeLogHistory, _1, _2));

	Service::OnFlappingChanged.connect(bind(&DbEvents::AddFlappingHistory, _1, _2));
	Service::OnNewCheckResult.connect(bind(&DbEvents::AddServiceCheckHistory, _1, _2));

	Service::OnEventCommandExecuted.connect(bind(&DbEvents::AddEventHandlerHistory, _1));

	ExternalCommandProcessor::OnNewExternalCommand.connect(boost::bind(&DbEvents::AddExternalCommandHistory, _1, _2, _3));
}

/* comments */
void DbEvents::AddComments(const Checkable::Ptr& checkable)
{
	/* dump all comments */
	Dictionary::Ptr comments = checkable->GetComments();

	if (comments->GetLength() > 0)
		RemoveComments(checkable);

	ObjectLock olock(comments);

	BOOST_FOREACH(const Dictionary::Pair& kv, comments) {
		AddComment(checkable, kv.second);
	}
}

void DbEvents::AddComment(const Checkable::Ptr& checkable, const Comment::Ptr& comment)
{
	AddCommentInternal(checkable, comment, false);
}

void DbEvents::AddCommentHistory(const Checkable::Ptr& checkable, const Comment::Ptr& comment)
{
	AddCommentInternal(checkable, comment, true);
}

void DbEvents::AddCommentInternal(const Checkable::Ptr& checkable, const Comment::Ptr& comment, bool historical)
{
	if (!comment) {
		Log(LogWarning, "db_ido", "comment does not exist. not adding it.");
		return;
	}

	Log(LogDebug, "db_ido", "adding service comment (id = " + Convert::ToString(comment->GetLegacyId()) + ") for '" + checkable->GetName() + "'");

	/* add the service comment */
	AddCommentByType(checkable, comment, historical);
}

void DbEvents::AddCommentByType(const DynamicObject::Ptr& object, const Comment::Ptr& comment, bool historical)
{
	unsigned long entry_time = static_cast<long>(comment->GetEntryTime());
	unsigned long entry_time_usec = (comment->GetEntryTime() - entry_time) * 1000 * 1000;

	Dictionary::Ptr fields1 = make_shared<Dictionary>();
	fields1->Set("entry_time", DbValue::FromTimestamp(entry_time));
	fields1->Set("entry_time_usec", entry_time_usec);
	fields1->Set("entry_type", comment->GetEntryType());
	fields1->Set("object_id", object);

	if (object->GetType() == DynamicType::GetByName("Host")) {
		fields1->Set("comment_type", 2);
		/* requires idoutils 1.10 schema fix */
		fields1->Set("internal_comment_id", comment->GetLegacyId());
	} else if (object->GetType() == DynamicType::GetByName("Service")) {
		fields1->Set("comment_type", 1);
		fields1->Set("internal_comment_id", comment->GetLegacyId());
	} else {
		Log(LogDebug, "db_ido", "unknown object type for adding comment.");
		return;
	}

	fields1->Set("comment_time", DbValue::FromTimestamp(entry_time)); /* same as entry_time */
	fields1->Set("author_name", comment->GetAuthor());
	fields1->Set("comment_data", comment->GetText());
	fields1->Set("is_persistent", 1);
	fields1->Set("comment_source", 1); /* external */
	fields1->Set("expires", (comment->GetExpireTime() > 0) ? 1 : 0);
	fields1->Set("expiration_time", DbValue::FromTimestamp(comment->GetExpireTime()));
	fields1->Set("instance_id", 0); /* DbConnection class fills in real ID */

	String node = IcingaApplication::GetInstance()->GetNodeName();

	Endpoint::Ptr endpoint = Endpoint::GetByName(node);
	if (endpoint)
		fields1->Set("endpoint_object_id", endpoint);

	DbQuery query1;
	if (!historical) {
		query1.Table = "comments";
	} else {
		query1.Table = "commenthistory";
	}
	query1.Type = DbQueryInsert;
	query1.Category = DbCatComment;
	query1.Fields = fields1;
	DbObject::OnQuery(query1);
}

void DbEvents::RemoveComments(const Checkable::Ptr& checkable)
{
	Log(LogDebug, "db_ido", "removing service comments for '" + checkable->GetName() + "'");

	DbQuery query1;
	query1.Table = "comments";
	query1.Type = DbQueryDelete;
	query1.Category = DbCatComment;
	query1.WhereCriteria = make_shared<Dictionary>();
	query1.WhereCriteria->Set("object_id", checkable);
	DbObject::OnQuery(query1);
}

void DbEvents::RemoveComment(const Checkable::Ptr& checkable, const Comment::Ptr& comment)
{
	if (!comment) {
		Log(LogWarning, "db_ido", "comment does not exist. not deleting it.");
		return;
	}

	Log(LogDebug, "db_ido", "removing service comment (id = " + Convert::ToString(comment->GetLegacyId()) + ") for '" + checkable->GetName() + "'");

	/* Status */
	DbQuery query1;
	query1.Table = "comments";
	query1.Type = DbQueryDelete;
	query1.Category = DbCatComment;
	query1.WhereCriteria = make_shared<Dictionary>();
	query1.WhereCriteria->Set("object_id", checkable);
	query1.WhereCriteria->Set("internal_comment_id", comment->GetLegacyId());
	DbObject::OnQuery(query1);

	/* History - update deletion time for service/host */
	unsigned long entry_time = static_cast<long>(comment->GetEntryTime());

	double now = Utility::GetTime();
	std::pair<unsigned long, unsigned long> time_bag = CompatUtility::ConvertTimestamp(now);

	DbQuery query2;
	query2.Table = "commenthistory";
	query2.Type = DbQueryUpdate;
	query2.Category = DbCatComment;

	Dictionary::Ptr fields2 = make_shared<Dictionary>();
	fields2->Set("deletion_time", DbValue::FromTimestamp(time_bag.first));
	fields2->Set("deletion_time_usec", time_bag.second);
	query2.Fields = fields2;

	query2.WhereCriteria = make_shared<Dictionary>();
	query2.WhereCriteria->Set("internal_comment_id", comment->GetLegacyId());
	query2.WhereCriteria->Set("comment_time", DbValue::FromTimestamp(entry_time));
	query2.WhereCriteria->Set("instance_id", 0); /* DbConnection class fills in real ID */

	DbObject::OnQuery(query2);
}

/* downtimes */
void DbEvents::AddDowntimes(const Checkable::Ptr& checkable)
{
	/* dump all downtimes */
	Dictionary::Ptr downtimes = checkable->GetDowntimes();

	if (downtimes->GetLength() > 0)
		RemoveDowntimes(checkable);

	ObjectLock olock(downtimes);

	BOOST_FOREACH(const Dictionary::Pair& kv, downtimes) {
		AddDowntime(checkable, kv.second);
	}
}

void DbEvents::AddDowntime(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime)
{
	AddDowntimeInternal(checkable, downtime, false);
}

void DbEvents::AddDowntimeHistory(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime)
{
	AddDowntimeInternal(checkable, downtime, true);
}

void DbEvents::AddDowntimeInternal(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime, bool historical)
{
	if (!downtime) {
		Log(LogWarning, "db_ido", "downtime does not exist. not adding it.");
		return;
	}

	Log(LogDebug, "db_ido", "adding service downtime (id = " + Convert::ToString(downtime->GetLegacyId()) + ") for '" + checkable->GetName() + "'");

	/* add the downtime */
	AddDowntimeByType(checkable, downtime, historical);}

void DbEvents::AddDowntimeByType(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime, bool historical)
{
	Dictionary::Ptr fields1 = make_shared<Dictionary>();
	fields1->Set("entry_time", DbValue::FromTimestamp(downtime->GetEntryTime()));
	fields1->Set("object_id", checkable);

	if (checkable->GetType() == DynamicType::GetByName("Host")) {
		fields1->Set("downtime_type", 2);
		/* requires idoutils 1.10 schema fix */
		fields1->Set("internal_downtime_id", downtime->GetLegacyId());
	} else if (checkable->GetType() == DynamicType::GetByName("Service")) {
		fields1->Set("downtime_type", 1);
		fields1->Set("internal_downtime_id", downtime->GetLegacyId());
	} else {
		Log(LogDebug, "db_ido", "unknown object type for adding downtime.");
		return;
	}

	fields1->Set("author_name", downtime->GetAuthor());
	fields1->Set("comment_data", downtime->GetComment());
	fields1->Set("triggered_by_id", Service::GetDowntimeByID(downtime->GetTriggeredBy()));
	fields1->Set("is_fixed", downtime->GetFixed());
	fields1->Set("duration", downtime->GetDuration());
	fields1->Set("scheduled_start_time", DbValue::FromTimestamp(downtime->GetStartTime()));
	fields1->Set("scheduled_end_time", DbValue::FromTimestamp(downtime->GetEndTime()));
	fields1->Set("was_started", Empty);
	fields1->Set("actual_start_time", Empty);
	fields1->Set("actual_start_time_usec", Empty);
	fields1->Set("is_in_effect", Empty);
	fields1->Set("trigger_time", DbValue::FromTimestamp(downtime->GetTriggerTime()));
	fields1->Set("instance_id", 0); /* DbConnection class fills in real ID */

	String node = IcingaApplication::GetInstance()->GetNodeName();

	Endpoint::Ptr endpoint = Endpoint::GetByName(node);
	if (endpoint)
		fields1->Set("endpoint_object_id", endpoint);

	DbQuery query1;

	if (!historical)
		query1.Table = "scheduleddowntime";
	else
		query1.Table = "downtimehistory";

	query1.Type = DbQueryInsert;
	query1.Category = DbCatDowntime;
	query1.Fields = fields1;
	DbObject::OnQuery(query1);
}

void DbEvents::RemoveDowntimes(const Checkable::Ptr& checkable)
{
	Log(LogDebug, "db_ido", "removing service downtimes for '" + checkable->GetName() + "'");

	DbQuery query1;
	query1.Table = "scheduleddowntime";
	query1.Type = DbQueryDelete;
	query1.Category = DbCatDowntime;
	query1.WhereCriteria = make_shared<Dictionary>();
	query1.WhereCriteria->Set("object_id", checkable);
	DbObject::OnQuery(query1);
}

void DbEvents::RemoveDowntime(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime)
{
	if (!downtime) {
		Log(LogWarning, "db_ido", "downtime does not exist. not adding it.");
		return;
	}

	Log(LogDebug, "db_ido", "removing service downtime (id = " + Convert::ToString(downtime->GetLegacyId()) + ") for '" + checkable->GetName() + "'");

	/* Status */
	DbQuery query1;
	query1.Table = "scheduleddowntime";
	query1.Type = DbQueryDelete;
	query1.Category = DbCatDowntime;
	query1.WhereCriteria = make_shared<Dictionary>();
	query1.WhereCriteria->Set("object_id", checkable);
	query1.WhereCriteria->Set("internal_downtime_id", downtime->GetLegacyId());
	DbObject::OnQuery(query1);

	/* History - update actual_end_time, was_cancelled for service (and host in case) */
	double now = Utility::GetTime();
	std::pair<unsigned long, unsigned long> time_bag = CompatUtility::ConvertTimestamp(now);

	DbQuery query3;
	query3.Table = "downtimehistory";
	query3.Type = DbQueryUpdate;
	query3.Category = DbCatDowntime;

	Dictionary::Ptr fields3 = make_shared<Dictionary>();
	fields3->Set("was_cancelled", downtime->GetWasCancelled() ? 1 : 0);
	fields3->Set("actual_end_time", DbValue::FromTimestamp(time_bag.first));
	fields3->Set("actual_end_time_usec", time_bag.second);
	query3.Fields = fields3;

	query3.WhereCriteria = make_shared<Dictionary>();
	query3.WhereCriteria->Set("internal_downtime_id", downtime->GetLegacyId());
	query3.WhereCriteria->Set("entry_time", DbValue::FromTimestamp(downtime->GetEntryTime()));
	query3.WhereCriteria->Set("scheduled_start_time", DbValue::FromTimestamp(downtime->GetStartTime()));
	query3.WhereCriteria->Set("scheduled_end_time", DbValue::FromTimestamp(downtime->GetEndTime()));
	query3.WhereCriteria->Set("instance_id", 0); /* DbConnection class fills in real ID */

	DbObject::OnQuery(query3);
}

void DbEvents::TriggerDowntime(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime)
{
	if (!downtime) {
		Log(LogWarning, "db_ido", "downtime does not exist. not updating it.");
		return;
	}

	Log(LogDebug, "db_ido", "updating triggered service downtime (id = " + Convert::ToString(downtime->GetLegacyId()) + ") for '" + checkable->GetName() + "'");

	double now = Utility::GetTime();
	std::pair<unsigned long, unsigned long> time_bag = CompatUtility::ConvertTimestamp(now);

	/* Status */
	DbQuery query1;
	query1.Table = "scheduleddowntime";
	query1.Type = DbQueryUpdate;
	query1.Category = DbCatDowntime;

	Dictionary::Ptr fields1 = make_shared<Dictionary>();
	fields1->Set("was_started", 1);
	fields1->Set("actual_start_time", DbValue::FromTimestamp(time_bag.first));
	fields1->Set("actual_start_time_usec", time_bag.second);
	fields1->Set("is_in_effect", 1);
	fields1->Set("trigger_time", DbValue::FromTimestamp(downtime->GetTriggerTime()));
	fields1->Set("instance_id", 0); /* DbConnection class fills in real ID */

	query1.WhereCriteria = make_shared<Dictionary>();
	query1.WhereCriteria->Set("object_id", checkable);
	query1.WhereCriteria->Set("internal_downtime_id", downtime->GetLegacyId());

	query1.Fields = fields1;
	DbObject::OnQuery(query1);

	/* History - downtime was started for service (and host in case) */
	DbQuery query3;
	query3.Table = "downtimehistory";
	query3.Type = DbQueryUpdate;
	query3.Category = DbCatDowntime;

	Dictionary::Ptr fields3 = make_shared<Dictionary>();
	fields3->Set("was_started", 1);
	fields3->Set("is_in_effect", 1);
	fields3->Set("actual_start_time", DbValue::FromTimestamp(time_bag.first));
	fields3->Set("actual_start_time_usec", time_bag.second);
	fields3->Set("trigger_time", DbValue::FromTimestamp(downtime->GetTriggerTime()));
	query3.Fields = fields3;

	query3.WhereCriteria = make_shared<Dictionary>();
	query3.WhereCriteria->Set("internal_downtime_id", downtime->GetLegacyId());
	query3.WhereCriteria->Set("entry_time", DbValue::FromTimestamp(downtime->GetEntryTime()));
	query3.WhereCriteria->Set("scheduled_start_time", DbValue::FromTimestamp(downtime->GetStartTime()));
	query3.WhereCriteria->Set("scheduled_end_time", DbValue::FromTimestamp(downtime->GetEndTime()));
	query3.WhereCriteria->Set("instance_id", 0); /* DbConnection class fills in real ID */

	DbObject::OnQuery(query3);
}

/* acknowledgements */
void DbEvents::AddAcknowledgementHistory(const Checkable::Ptr& checkable, const String& author, const String& comment,
    AcknowledgementType type, double expiry)
{
	Log(LogDebug, "db_ido", "add acknowledgement history for '" + checkable->GetName() + "'");

	double now = Utility::GetTime();
	std::pair<unsigned long, unsigned long> time_bag = CompatUtility::ConvertTimestamp(now);

	unsigned long end_time = static_cast<long>(expiry);

	DbQuery query1;
	query1.Table = "acknowledgements";
	query1.Type = DbQueryInsert;
	query1.Category = DbCatAcknowledgement;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr fields1 = make_shared<Dictionary>();
	fields1->Set("entry_time", DbValue::FromTimestamp(time_bag.first));
	fields1->Set("entry_time_usec", time_bag.second);
	fields1->Set("acknowledgement_type", type);
	fields1->Set("object_id", checkable);
	fields1->Set("state", service ? service->GetState() : host->GetState());
	fields1->Set("author_name", author);
	fields1->Set("comment_data", comment);
	fields1->Set("is_sticky", type == AcknowledgementSticky ? 1 : 0);
	fields1->Set("end_time", DbValue::FromTimestamp(end_time));
	fields1->Set("instance_id", 0); /* DbConnection class fills in real ID */

	String node = IcingaApplication::GetInstance()->GetNodeName();

	Endpoint::Ptr endpoint = Endpoint::GetByName(node);
	if (endpoint)
		fields1->Set("endpoint_object_id", endpoint);

	query1.Fields = fields1;
	DbObject::OnQuery(query1);
}

/* notifications */
void DbEvents::AddNotificationHistory(const Notification::Ptr& notification, const Checkable::Ptr& checkable, const std::set<User::Ptr>& users, NotificationType type,
    const CheckResult::Ptr& cr, const String& author, const String& text)
{
	Log(LogDebug, "db_ido", "add notification history for '" + checkable->GetName() + "'");

	/* start and end happen at the same time */
	double now = Utility::GetTime();
	std::pair<unsigned long, unsigned long> time_bag = CompatUtility::ConvertTimestamp(now);

	DbQuery query1;
	query1.Table = "notifications";
	query1.Type = DbQueryInsert;
	query1.Category = DbCatNotification;
	/* store the object ptr for caching the insert id for this object */
	query1.NotificationObject = notification;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr fields1 = make_shared<Dictionary>();
	fields1->Set("notification_type", 1); /* service */
	fields1->Set("notification_reason", CompatUtility::MapNotificationReasonType(type));
	fields1->Set("object_id", checkable);
	fields1->Set("start_time", DbValue::FromTimestamp(time_bag.first));
	fields1->Set("start_time_usec", time_bag.second);
	fields1->Set("end_time", DbValue::FromTimestamp(time_bag.first));
	fields1->Set("end_time_usec", time_bag.second);
	fields1->Set("state", service ? service->GetState() : host->GetState());

	if (cr) {
		fields1->Set("output", CompatUtility::GetCheckResultOutput(cr));
		fields1->Set("long_output", CompatUtility::GetCheckResultLongOutput(cr));
	}

	fields1->Set("escalated", 0);
	fields1->Set("contacts_notified", static_cast<long>(users.size()));
	fields1->Set("instance_id", 0); /* DbConnection class fills in real ID */

	String node = IcingaApplication::GetInstance()->GetNodeName();

	Endpoint::Ptr endpoint = Endpoint::GetByName(node);
	if (endpoint)
		fields1->Set("endpoint_object_id", endpoint);

	query1.Fields = fields1;
	DbObject::OnQuery(query1);

	DbQuery query2;
	query2.Table = "contactnotifications";
	query2.Type = DbQueryInsert;
	query2.Category = DbCatNotification;

	/* filtered users */
	BOOST_FOREACH(const User::Ptr& user, users) {
		Log(LogDebug, "db_ido", "add contact notification history for service '" + checkable->GetName() + "' and user '" + user->GetName() + "'.");

		Dictionary::Ptr fields2 = make_shared<Dictionary>();
		fields2->Set("contact_object_id", user);
		fields2->Set("start_time", DbValue::FromTimestamp(time_bag.first));
		fields2->Set("start_time_usec", time_bag.second);
		fields2->Set("end_time", DbValue::FromTimestamp(time_bag.first));
		fields2->Set("end_time_usec", time_bag.second);

		fields2->Set("notification_id", notification); /* DbConnection class fills in real ID from notification insert id cache */
		fields2->Set("instance_id", 0); /* DbConnection class fills in real ID */

		query2.Fields = fields2;
		DbObject::OnQuery(query2);
	}
}

/* statehistory */
void DbEvents::AddStateChangeHistory(const Checkable::Ptr& checkable, const CheckResult::Ptr& cr, StateType type)
{
	Log(LogDebug, "db_ido", "add state change history for '" + checkable->GetName() + "'");

	double now = Utility::GetTime();
	std::pair<unsigned long, unsigned long> time_bag = CompatUtility::ConvertTimestamp(now);

	DbQuery query1;
	query1.Table = "statehistory";
	query1.Type = DbQueryInsert;
	query1.Category = DbCatStateHistory;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr fields1 = make_shared<Dictionary>();
	fields1->Set("state_time", DbValue::FromTimestamp(time_bag.first));
	fields1->Set("state_time_usec", time_bag.second);
	fields1->Set("object_id", checkable);
	fields1->Set("state_change", 1); /* service */
	fields1->Set("state", service ? service->GetState() : host->GetState());
	fields1->Set("state_type", checkable->GetStateType());
	fields1->Set("current_check_attempt", checkable->GetCheckAttempt());
	fields1->Set("max_check_attempts", checkable->GetMaxCheckAttempts());

	if (service) {
		fields1->Set("last_state", service->GetLastState());
		fields1->Set("last_hard_state", service->GetLastHardState());
	} else {
		fields1->Set("last_state", host->GetLastState());
		fields1->Set("last_hard_state", host->GetLastHardState());
	}

	if (cr) {
		fields1->Set("output", CompatUtility::GetCheckResultOutput(cr));
		fields1->Set("long_output", CompatUtility::GetCheckResultLongOutput(cr));
		fields1->Set("check_source", cr->GetCheckSource());
	}

	fields1->Set("instance_id", 0); /* DbConnection class fills in real ID */

	String node = IcingaApplication::GetInstance()->GetNodeName();

	Endpoint::Ptr endpoint = Endpoint::GetByName(node);
	if (endpoint)
		fields1->Set("endpoint_object_id", endpoint);

	query1.Fields = fields1;
	DbObject::OnQuery(query1);
}

/* logentries */
void DbEvents::AddCheckResultLogHistory(const Checkable::Ptr& checkable, const CheckResult::Ptr &cr)
{
	Dictionary::Ptr vars_after = cr->GetVarsAfter();

	long state_after = vars_after->Get("state");
	long stateType_after = vars_after->Get("state_type");
	long attempt_after = vars_after->Get("attempt");
	bool reachable_after = vars_after->Get("reachable");

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

	LogEntryType type;
	String output;

	if (cr)
		output = CompatUtility::GetCheckResultOutput(cr);

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

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

		switch (service->GetState()) {
			case ServiceOK:
				type = LogEntryTypeServiceOk;
				break;
			case ServiceUnknown:
				type = LogEntryTypeServiceUnknown;
				break;
			case ServiceWarning:
				type = LogEntryTypeServiceWarning;
				break;
			case ServiceCritical:
				type = LogEntryTypeServiceCritical;
				break;
			default:
				Log(LogCritical, "db_ido", "Unknown service state: " + Convert::ToString(state_after));
				return;
		}
	} else {
		String state = Host::StateToString(Host::CalculateState(static_cast<ServiceState>(state_after)));

		if (!reachable_after)
			state = "UNREACHABLE";

		msgbuf << "HOST ALERT: "
		       << host->GetName() << ";"
		       << state << ";"
		       << Service::StateTypeToString(static_cast<StateType>(stateType_after)) << ";"
		       << attempt_after << ";"
		       << output << ""
		       << "";

		switch (host->GetState()) {
			case HostUp:
				type = LogEntryTypeHostUp;
				break;
			case HostDown:
				type = LogEntryTypeHostDown;
				break;
			default:
				Log(LogCritical, "db_ido", "Unknown host state: " + Convert::ToString(state_after));
				return;
		}

		if (!reachable_after)
			type = LogEntryTypeHostUnreachable;
	}

	AddLogHistory(checkable, msgbuf.str(), type);
}

void DbEvents::AddTriggerDowntimeLogHistory(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime)
{
	if (!downtime)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	std::ostringstream msgbuf;

	if (service) {
		msgbuf << "SERVICE DOWNTIME ALERT: "
			<< host->GetName() << ";"
			<< service->GetShortName() << ";"
			<< "STARTED" << "; "
			<< "Service has entered a period of scheduled downtime."
			<< "";
	} else {
		msgbuf << "HOST DOWNTIME ALERT: "
			<< host->GetName() << ";"
			<< "STARTED" << "; "
			<< "Service has entered a period of scheduled downtime."
			<< "";
	}

	AddLogHistory(checkable, msgbuf.str(), LogEntryTypeInfoMessage);
}

void DbEvents::AddRemoveDowntimeLogHistory(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime)
{
	if (!downtime)
		return;

	String downtime_output;
	String downtime_state_str;

	if (downtime->GetWasCancelled()) {
		downtime_output = "Scheduled downtime for service has been cancelled.";
		downtime_state_str = "CANCELLED";
	} else {
		downtime_output = "Service has exited from a period of scheduled downtime.";
		downtime_state_str = "STOPPED";
	}

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

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

	AddLogHistory(checkable, msgbuf.str(), LogEntryTypeInfoMessage);
}

void DbEvents::AddNotificationSentLogHistory(const Notification::Ptr& notification, const Checkable::Ptr& checkable, const User::Ptr& user,
    NotificationType notification_type, const CheckResult::Ptr& cr,
    const String& author, const String& comment_text)
{
	CheckCommand::Ptr commandObj = checkable->GetCheckCommand();

	String check_command = "";
	if (commandObj)
		check_command = commandObj->GetName();

	String notification_type_str = Notification::NotificationTypeToString(notification_type);

	String author_comment = "";
	if (notification_type == NotificationCustom || notification_type == NotificationAcknowledgement) {
		author_comment = ";" + author + ";" + comment_text;
	}

	if (!cr)
		return;

	String output;

	if (cr)
		output = CompatUtility::GetCheckResultOutput(cr);

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	std::ostringstream msgbuf;

	if (service) {
		msgbuf << "SERVICE NOTIFICATION: "
		       << user->GetName() << ";"
		       << host->GetName() << ";"
		       << service->GetShortName() << ";"
		       << notification_type_str << " "
		       << "(" << Service::StateToString(service->GetState()) << ");"
		       << check_command << ";"
		       << output << author_comment
		       << "";
	} else {
		msgbuf << "HOST NOTIFICATION: "
		       << user->GetName() << ";"
		       << host->GetName() << ";"
		       << notification_type_str << " "
		       << "(" << Host::StateToString(host->GetState()) << ");"
		       << check_command << ";"
		       << output << author_comment
		       << "";
	}

	AddLogHistory(checkable, msgbuf.str(), LogEntryTypeHostNotification);
}

void DbEvents::AddFlappingLogHistory(const Checkable::Ptr& checkable, FlappingState flapping_state)
{
	String flapping_state_str;
	String flapping_output;

	switch (flapping_state) {
		case FlappingStarted:
			flapping_output = "Service appears to have started flapping (" + Convert::ToString(checkable->GetFlappingCurrent()) + "% change >= " + Convert::ToString(checkable->GetFlappingThreshold()) + "% threshold)";
			flapping_state_str = "STARTED";
			break;
		case FlappingStopped:
			flapping_output = "Service appears to have stopped flapping (" + Convert::ToString(checkable->GetFlappingCurrent()) + "% change < " + Convert::ToString(checkable->GetFlappingThreshold()) + "% threshold)";
			flapping_state_str = "STOPPED";
			break;
		case FlappingDisabled:
			flapping_output = "Flap detection has been disabled";
			flapping_state_str = "DISABLED";
			break;
		default:
			Log(LogCritical, "db_ido", "Unknown flapping state: " + Convert::ToString(flapping_state));
			return;
	}

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

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

	AddLogHistory(checkable, msgbuf.str(), LogEntryTypeInfoMessage);
}

void DbEvents::AddLogHistory(const Checkable::Ptr& checkable, String buffer, LogEntryType type)
{
	Log(LogDebug, "db_ido", "add log entry history for '" + checkable->GetName() + "'");

	double now = Utility::GetTime();
	std::pair<unsigned long, unsigned long> time_bag = CompatUtility::ConvertTimestamp(now);

	DbQuery query1;
	query1.Table = "logentries";
	query1.Type = DbQueryInsert;
	query1.Category = DbCatLog;

	Dictionary::Ptr fields1 = make_shared<Dictionary>();
	fields1->Set("logentry_time", DbValue::FromTimestamp(time_bag.first));
	fields1->Set("entry_time", DbValue::FromTimestamp(time_bag.first));
	fields1->Set("entry_time_usec", time_bag.second);
	fields1->Set("object_id", checkable); // added in 1.10 see #4754
	fields1->Set("logentry_type", type);
	fields1->Set("logentry_data", buffer);

	fields1->Set("instance_id", 0); /* DbConnection class fills in real ID */

	String node = IcingaApplication::GetInstance()->GetNodeName();

	Endpoint::Ptr endpoint = Endpoint::GetByName(node);
	if (endpoint)
		fields1->Set("endpoint_object_id", endpoint);

	query1.Fields = fields1;
	DbObject::OnQuery(query1);
}

/* flappinghistory */
void DbEvents::AddFlappingHistory(const Checkable::Ptr& checkable, FlappingState flapping_state)
{
	Log(LogDebug, "db_ido", "add flapping history for '" + checkable->GetName() + "'");

	double now = Utility::GetTime();
	std::pair<unsigned long, unsigned long> time_bag = CompatUtility::ConvertTimestamp(now);

	DbQuery query1;
	query1.Table = "flappinghistory";
	query1.Type = DbQueryInsert;
	query1.Category = DbCatFlapping;

	Dictionary::Ptr fields1 = make_shared<Dictionary>();

	fields1->Set("event_time", DbValue::FromTimestamp(time_bag.first));
	fields1->Set("event_time_usec", time_bag.second);

	switch (flapping_state) {
		case FlappingStarted:
			fields1->Set("event_type", 1000);
			break;
		case FlappingStopped:
			fields1->Set("event_type", 1001);
			fields1->Set("reason_type", 1);
			break;
		case FlappingDisabled:
			fields1->Set("event_type", 1001);
			fields1->Set("reason_type", 2);
			break;
		default:
			Log(LogDebug, "db_ido", "Unhandled flapping state: " + Convert::ToString(flapping_state));
			return;
	}

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	fields1->Set("flapping_type", service ? 1 : 0);
	fields1->Set("object_id", checkable);
	fields1->Set("percent_state_change", checkable->GetFlappingCurrent());
	fields1->Set("low_threshold", checkable->GetFlappingThreshold());
	fields1->Set("high_threshold", checkable->GetFlappingThreshold());

	fields1->Set("instance_id", 0); /* DbConnection class fills in real ID */

	String node = IcingaApplication::GetInstance()->GetNodeName();

	Endpoint::Ptr endpoint = Endpoint::GetByName(node);
	if (endpoint)
		fields1->Set("endpoint_object_id", endpoint);

	query1.Fields = fields1;
	DbObject::OnQuery(query1);
}

/* servicechecks */
void DbEvents::AddServiceCheckHistory(const Checkable::Ptr& checkable, const CheckResult::Ptr &cr)
{
	if (!cr)
		return;

	Log(LogDebug, "db_ido", "add service check history for '" + checkable->GetName() + "'");

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	std::ostringstream msgbuf;

	DbQuery query1;
	query1.Table = service ? "servicechecks" : "hostchecks";
	query1.Type = DbQueryInsert;
	query1.Category = DbCatCheck;

	Dictionary::Ptr fields1 = make_shared<Dictionary>();
	double execution_time = Service::CalculateExecutionTime(cr);

	fields1->Set("check_type", CompatUtility::GetCheckableCheckType(checkable));
	fields1->Set("current_check_attempt", checkable->GetCheckAttempt());
	fields1->Set("max_check_attempts", checkable->GetMaxCheckAttempts());
	fields1->Set("state_type", checkable->GetStateType());

	double now = Utility::GetTime();
	std::pair<unsigned long, unsigned long> time_bag = CompatUtility::ConvertTimestamp(now);

	double end = now + execution_time;
	std::pair<unsigned long, unsigned long> time_bag_end = CompatUtility::ConvertTimestamp(end);

	fields1->Set("start_time", DbValue::FromTimestamp(time_bag.first));
	fields1->Set("start_time_usec", time_bag.second);
	fields1->Set("end_time", DbValue::FromTimestamp(time_bag_end.first));
	fields1->Set("end_time_usec", time_bag_end.second);
	fields1->Set("command_object_id", checkable->GetCheckCommand());
	fields1->Set("command_args", Empty);
	fields1->Set("command_line", cr->GetCommand());
	fields1->Set("execution_time", Convert::ToString(execution_time));
	fields1->Set("latency", Convert::ToString(Service::CalculateLatency(cr)));
	fields1->Set("return_code", cr->GetExitStatus());
	fields1->Set("output", CompatUtility::GetCheckResultOutput(cr));
	fields1->Set("long_output", CompatUtility::GetCheckResultLongOutput(cr));
	fields1->Set("perfdata", CompatUtility::GetCheckResultPerfdata(cr));

	fields1->Set("instance_id", 0); /* DbConnection class fills in real ID */

	if (service) {
		fields1->Set("service_object_id", service);
		fields1->Set("state", service->GetState());
	} else {
		fields1->Set("host_object_id", host);
		fields1->Set("state", host->GetState());
	}

	String node = IcingaApplication::GetInstance()->GetNodeName();

	Endpoint::Ptr endpoint = Endpoint::GetByName(node);
	if (endpoint)
		fields1->Set("endpoint_object_id", endpoint);

	query1.Fields = fields1;
	DbObject::OnQuery(query1);
}

/* eventhandlers */
void DbEvents::AddEventHandlerHistory(const Checkable::Ptr& checkable)
{
	Log(LogDebug, "db_ido", "add eventhandler history for '" + checkable->GetName() + "'");

	double now = Utility::GetTime();
	std::pair<unsigned long, unsigned long> time_bag = CompatUtility::ConvertTimestamp(now);

	DbQuery query1;
	query1.Table = "eventhandlers";
	query1.Type = DbQueryInsert;
	query1.Category = DbCatEventHandler;

	Dictionary::Ptr fields1 = make_shared<Dictionary>();

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	fields1->Set("eventhandler_type", service ? 1 : 0);
	fields1->Set("object_id", checkable);
	fields1->Set("state", service ? service->GetState() : host->GetState());
	fields1->Set("state_type", checkable->GetStateType());

	fields1->Set("start_time", DbValue::FromTimestamp(time_bag.first));
	fields1->Set("start_time_usec", time_bag.second);
	fields1->Set("end_time", DbValue::FromTimestamp(time_bag.first));
	fields1->Set("end_time_usec", time_bag.second);
	fields1->Set("command_object_id", checkable->GetEventCommand());

	fields1->Set("instance_id", 0); /* DbConnection class fills in real ID */

	String node = IcingaApplication::GetInstance()->GetNodeName();

	Endpoint::Ptr endpoint = Endpoint::GetByName(node);
	if (endpoint)
		fields1->Set("endpoint_object_id", endpoint);

	query1.Fields = fields1;
	DbObject::OnQuery(query1);
}

/* externalcommands */
void DbEvents::AddExternalCommandHistory(double time, const String& command, const std::vector<String>& arguments)
{
	Log(LogDebug, "db_ido", "add external command history");

	DbQuery query1;
	query1.Table = "externalcommands";
	query1.Type = DbQueryInsert;
	query1.Category = DbCatExternalCommand;

	Dictionary::Ptr fields1 = make_shared<Dictionary>();

	fields1->Set("entry_time", DbValue::FromTimestamp(static_cast<long>(time)));
	fields1->Set("command_type", CompatUtility::MapExternalCommandType(command));
	fields1->Set("command_name", command);
	fields1->Set("command_args", boost::algorithm::join(arguments, ";"));

	fields1->Set("instance_id", 0); /* DbConnection class fills in real ID */

	String node = IcingaApplication::GetInstance()->GetNodeName();

	Endpoint::Ptr endpoint = Endpoint::GetByName(node);
	if (endpoint)
		fields1->Set("endpoint_object_id", endpoint);

	query1.Fields = fields1;
	DbObject::OnQuery(query1);
}
