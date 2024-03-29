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

#include "icinga/service.h"
#include "icinga/servicegroup.h"
#include "icinga/checkcommand.h"
#include "icinga/icingaapplication.h"
#include "icinga/macroprocessor.h"
#include "icinga/pluginutility.h"
#include "icinga/dependency.h"
#include "config/configitembuilder.h"
#include "base/dynamictype.h"
#include "base/objectlock.h"
#include "base/convert.h"
#include "base/utility.h"
#include "base/initialize.h"
#include <boost/foreach.hpp>
#include <boost/bind/apply.hpp>

using namespace icinga;

REGISTER_TYPE(Checkable);

INITIALIZE_ONCE(&Checkable::StartDowntimesExpiredTimer);

boost::signals2::signal<void (const Checkable::Ptr&, const String&, const String&, AcknowledgementType, double, const String&)> Checkable::OnAcknowledgementSet;
boost::signals2::signal<void (const Checkable::Ptr&, const String&)> Checkable::OnAcknowledgementCleared;

Checkable::Checkable(void)
	: m_CheckRunning(false)
{ }

void Checkable::Start(void)
{
	double now = Utility::GetTime();

	if (GetNextCheck() < now + 300)
		UpdateNextCheck();

	DynamicObject::Start();
}

void Checkable::OnConfigLoaded(void)
{
	DynamicObject::OnConfigLoaded();

	SetSchedulingOffset(Utility::Random());
}

void Checkable::OnStateLoaded(void)
{
	AddDowntimesToCache();
	AddCommentsToCache();

	std::vector<String> ids;
	Dictionary::Ptr downtimes = GetDowntimes();

	{
		ObjectLock dlock(downtimes);
		BOOST_FOREACH(const Dictionary::Pair& kv, downtimes) {
			Downtime::Ptr downtime = kv.second;

			if (downtime->GetScheduledBy().IsEmpty())
				continue;

			ids.push_back(kv.first);
		}
	}

	BOOST_FOREACH(const String& id, ids) {
		RemoveDowntime(id, true);
	}
}

AcknowledgementType Checkable::GetAcknowledgement(void)
{
	ASSERT(OwnsLock());

	AcknowledgementType avalue = static_cast<AcknowledgementType>(GetAcknowledgementRaw());

	if (avalue != AcknowledgementNone) {
		double expiry = GetAcknowledgementExpiry();

		if (expiry != 0 && expiry < Utility::GetTime()) {
			avalue = AcknowledgementNone;
			ClearAcknowledgement();
		}
	}

	return avalue;
}

bool Checkable::IsAcknowledged(void)
{
	return GetAcknowledgement() != AcknowledgementNone;
}

void Checkable::AcknowledgeProblem(const String& author, const String& comment, AcknowledgementType type, double expiry, const String& authority)
{
	{
		ObjectLock olock(this);

		SetAcknowledgementRaw(type);
		SetAcknowledgementExpiry(expiry);
	}

	OnNotificationsRequested(GetSelf(), NotificationAcknowledgement, GetLastCheckResult(), author, comment);

	OnAcknowledgementSet(GetSelf(), author, comment, type, expiry, authority);
}

void Checkable::ClearAcknowledgement(const String& authority)
{
	ASSERT(OwnsLock());

	SetAcknowledgementRaw(AcknowledgementNone);
	SetAcknowledgementExpiry(0);

	OnAcknowledgementCleared(GetSelf(), authority);
}

bool Checkable::GetEnablePerfdata(void) const
{
	if (!GetOverrideEnablePerfdata().IsEmpty())
		return GetOverrideEnablePerfdata();
	else
		return GetEnablePerfdataRaw();
}

void Checkable::SetEnablePerfdata(bool enabled, const String& authority)
{
	SetOverrideEnablePerfdata(enabled);
}

int Checkable::GetModifiedAttributes(void) const
{
	int attrs = 0;

	if (!GetOverrideEnableNotifications().IsEmpty())
		attrs |= ModAttrNotificationsEnabled;

	if (!GetOverrideEnableActiveChecks().IsEmpty())
		attrs |= ModAttrActiveChecksEnabled;

	if (!GetOverrideEnablePassiveChecks().IsEmpty())
		attrs |= ModAttrPassiveChecksEnabled;

	if (!GetOverrideEnableFlapping().IsEmpty())
		attrs |= ModAttrFlapDetectionEnabled;

	if (!GetOverrideEnableEventHandler().IsEmpty())
		attrs |= ModAttrEventHandlerEnabled;

	if (!GetOverrideEnablePerfdata().IsEmpty())
		attrs |= ModAttrPerformanceDataEnabled;

	if (!GetOverrideCheckInterval().IsEmpty())
		attrs |= ModAttrNormalCheckInterval;

	if (!GetOverrideRetryInterval().IsEmpty())
		attrs |= ModAttrRetryCheckInterval;

	if (!GetOverrideEventCommand().IsEmpty())
		attrs |= ModAttrEventHandlerCommand;

	if (!GetOverrideCheckCommand().IsEmpty())
		attrs |= ModAttrCheckCommand;

	if (!GetOverrideMaxCheckAttempts().IsEmpty())
		attrs |= ModAttrMaxCheckAttempts;

	if (!GetOverrideCheckPeriod().IsEmpty())
		attrs |= ModAttrCheckTimeperiod;

	if (!GetOverrideVars().IsEmpty())
		attrs |= ModAttrCustomVariable;

	// TODO: finish

	return attrs;
}

void Checkable::SetModifiedAttributes(int flags)
{
	if ((flags & ModAttrNotificationsEnabled) == 0)
		SetOverrideEnableNotifications(Empty);

	if ((flags & ModAttrActiveChecksEnabled) == 0)
		SetOverrideEnableActiveChecks(Empty);

	if ((flags & ModAttrPassiveChecksEnabled) == 0)
		SetOverrideEnablePassiveChecks(Empty);

	if ((flags & ModAttrFlapDetectionEnabled) == 0)
		SetOverrideEnableFlapping(Empty);

	if ((flags & ModAttrEventHandlerEnabled) == 0)
		SetOverrideEnableEventHandler(Empty);

	if ((flags & ModAttrPerformanceDataEnabled) == 0)
		SetOverrideEnablePerfdata(Empty);

	if ((flags & ModAttrNormalCheckInterval) == 0)
		SetOverrideCheckInterval(Empty);

	if ((flags & ModAttrRetryCheckInterval) == 0)
		SetOverrideRetryInterval(Empty);

	if ((flags & ModAttrEventHandlerCommand) == 0)
		SetOverrideEventCommand(Empty);

	if ((flags & ModAttrCheckCommand) == 0)
		SetOverrideCheckCommand(Empty);

	if ((flags & ModAttrMaxCheckAttempts) == 0)
		SetOverrideMaxCheckAttempts(Empty);

	if ((flags & ModAttrCheckTimeperiod) == 0)
		SetOverrideCheckPeriod(Empty);

	if ((flags & ModAttrCustomVariable) == 0) {
		SetOverrideVars(Empty);
		OnVarsChanged(GetSelf());
	}
}
