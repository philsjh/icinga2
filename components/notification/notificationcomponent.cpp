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

#include "notification/notificationcomponent.h"
#include "icinga/service.h"
#include "base/dynamictype.h"
#include "base/objectlock.h"
#include "base/logger_fwd.h"
#include "base/utility.h"
#include "base/exception.h"
#include "base/statsfunction.h"
#include <boost/foreach.hpp>

using namespace icinga;

REGISTER_TYPE(NotificationComponent);

REGISTER_STATSFUNCTION(NotificationComponentStats, &NotificationComponent::StatsFunc);

Value NotificationComponent::StatsFunc(Dictionary::Ptr& status, Dictionary::Ptr& perfdata)
{
	Dictionary::Ptr nodes = make_shared<Dictionary>();

	BOOST_FOREACH(const NotificationComponent::Ptr& notification_component, DynamicType::GetObjects<NotificationComponent>()) {
		nodes->Set(notification_component->GetName(), 1); //add more stats
	}

	status->Set("notificationcomponent", nodes);

	return 0;
}

/**
 * Starts the component.
 */
void NotificationComponent::Start(void)
{
	DynamicObject::Start();

	Checkable::OnNotificationsRequested.connect(boost::bind(&NotificationComponent::SendNotificationsHandler, this, _1,
	    _2, _3, _4, _5));

	m_NotificationTimer = make_shared<Timer>();
	m_NotificationTimer->SetInterval(5);
	m_NotificationTimer->OnTimerExpired.connect(boost::bind(&NotificationComponent::NotificationTimerHandler, this));
	m_NotificationTimer->Start();
}

/**
 * Periodically sends notifications.
 *
 * @param - Event arguments for the timer.
 */
void NotificationComponent::NotificationTimerHandler(void)
{
	double now = Utility::GetTime();

	BOOST_FOREACH(const Notification::Ptr& notification, DynamicType::GetObjects<Notification>()) {
		Checkable::Ptr checkable = notification->GetCheckable();

		if (notification->GetInterval() <= 0 && notification->GetLastProblemNotification() > checkable->GetLastHardStateChange())
			continue;

		if (notification->GetNextNotification() > now)
			continue;

		bool reachable = checkable->IsReachable(DependencyNotification);

		{
			ObjectLock olock(notification);
			notification->SetNextNotification(Utility::GetTime() + notification->GetInterval());
		}

		{
			Host::Ptr host;
			Service::Ptr service;
			tie(host, service) = GetHostService(checkable);

			ObjectLock olock(checkable);

			if (checkable->GetStateType() == StateTypeSoft)
				continue;

			if ((service && service->GetState() == ServiceOK) || (!service && host->GetState() == HostUp))
				continue;

			if (!reachable || checkable->IsInDowntime() || checkable->IsAcknowledged())
				continue;
		}

		try {
			Log(LogInformation, "notification", "Sending reminder notification for object '" + checkable->GetName() + "'");
			notification->BeginExecuteNotification(NotificationProblem, checkable->GetLastCheckResult(), false);
		} catch (const std::exception& ex) {
			std::ostringstream msgbuf;
			msgbuf << "Exception occured during notification for object '"
			       << GetName() << "': " << DiagnosticInformation(ex);
			String message = msgbuf.str();

			Log(LogWarning, "icinga", message);
		}
	}
}

/**
 * Processes icinga::SendNotifications messages.
 */
void NotificationComponent::SendNotificationsHandler(const Checkable::Ptr& checkable, NotificationType type,
    const CheckResult::Ptr& cr, const String& author, const String& text)
{
	checkable->SendNotifications(type, cr, author, text);
}
