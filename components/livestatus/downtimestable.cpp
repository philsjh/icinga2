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

#include "livestatus/downtimestable.h"
#include "livestatus/servicestable.h"
#include "icinga/service.h"
#include "base/dynamictype.h"
#include "base/objectlock.h"
#include <boost/tuple/tuple.hpp>
#include <boost/foreach.hpp>

using namespace icinga;

DowntimesTable::DowntimesTable(void)
{
	AddColumns(this);
}

void DowntimesTable::AddColumns(Table *table, const String& prefix,
    const Column::ObjectAccessor& objectAccessor)
{
	table->AddColumn(prefix + "author", Column(&DowntimesTable::AuthorAccessor, objectAccessor));
	table->AddColumn(prefix + "comment", Column(&DowntimesTable::CommentAccessor, objectAccessor));
	table->AddColumn(prefix + "id", Column(&DowntimesTable::IdAccessor, objectAccessor));
	table->AddColumn(prefix + "entry_time", Column(&DowntimesTable::EntryTimeAccessor, objectAccessor));
	table->AddColumn(prefix + "type", Column(&DowntimesTable::TypeAccessor, objectAccessor));
	table->AddColumn(prefix + "is_service", Column(&DowntimesTable::IsServiceAccessor, objectAccessor));
	table->AddColumn(prefix + "start_time", Column(&DowntimesTable::StartTimeAccessor, objectAccessor));
	table->AddColumn(prefix + "end_time", Column(&DowntimesTable::EndTimeAccessor, objectAccessor));
	table->AddColumn(prefix + "fixed", Column(&DowntimesTable::FixedAccessor, objectAccessor));
	table->AddColumn(prefix + "duration", Column(&DowntimesTable::DurationAccessor, objectAccessor));
	table->AddColumn(prefix + "triggered_by", Column(&DowntimesTable::TriggeredByAccessor, objectAccessor));

	ServicesTable::AddColumns(table, "service_", boost::bind(&DowntimesTable::ServiceAccessor, _1, objectAccessor));
}

String DowntimesTable::GetName(void) const
{
	return "downtimes";
}

void DowntimesTable::FetchRows(const AddRowFunction& addRowFn)
{
	BOOST_FOREACH(const Service::Ptr& service, DynamicType::GetObjects<Service>()) {
		Dictionary::Ptr downtimes = service->GetDowntimes();

		ObjectLock olock(downtimes);

		String id;
		Downtime::Ptr downtime;
		BOOST_FOREACH(boost::tie(id, downtime), downtimes) {
			if (Service::GetOwnerByDowntimeID(id) == service)
				addRowFn(downtime);
		}
	}
}

Object::Ptr DowntimesTable::ServiceAccessor(const Value& row, const Column::ObjectAccessor& parentObjectAccessor)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);
	return Service::GetOwnerByDowntimeID(downtime->GetId());
}

Value DowntimesTable::AuthorAccessor(const Value& row)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);

	return downtime->GetAuthor();
}

Value DowntimesTable::CommentAccessor(const Value& row)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);

	return downtime->GetComment();
}

Value DowntimesTable::IdAccessor(const Value& row)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);

	return downtime->GetLegacyId();
}

Value DowntimesTable::EntryTimeAccessor(const Value& row)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);

	return static_cast<int>(downtime->GetEntryTime());
}

Value DowntimesTable::TypeAccessor(const Value& row)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);
	// 1 .. active, 0 .. pending
	return (downtime->IsActive() ? 1 : 0);
}

Value DowntimesTable::IsServiceAccessor(const Value& row)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);
	Checkable::Ptr checkable = Checkable::GetOwnerByDowntimeID(downtime->GetId());

	return (dynamic_pointer_cast<Host>(checkable) ? 0 : 1);
}

Value DowntimesTable::StartTimeAccessor(const Value& row)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);

	return static_cast<int>(downtime->GetStartTime());
}

Value DowntimesTable::EndTimeAccessor(const Value& row)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);

	return static_cast<int>(downtime->GetEndTime());
}

Value DowntimesTable::FixedAccessor(const Value& row)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);

	return downtime->GetFixed();
}

Value DowntimesTable::DurationAccessor(const Value& row)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);

	return downtime->GetDuration();
}

Value DowntimesTable::TriggeredByAccessor(const Value& row)
{
	Downtime::Ptr downtime = static_cast<Downtime::Ptr>(row);

	return downtime->GetTriggeredByLegacyId();
}
