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

#ifndef SERVICE_H
#define SERVICE_H

#include "icinga/i2-icinga.h"
#include "icinga/service.th"
#include "icinga/macroresolver.h"
#include "icinga/host.h"
#include "icinga/timeperiod.h"
#include "icinga/notification.h"
#include "icinga/comment.h"
#include "icinga/downtime.h"
#include "base/i2-base.h"
#include "base/array.h"
#include <boost/signals2.hpp>
#include <boost/thread/once.hpp>

namespace icinga
{

/**
 * An Icinga service.
 *
 * @ingroup icinga
 */
class I2_ICINGA_API Service : public ObjectImpl<Service>, public MacroResolver
{
public:
	DECLARE_PTR_TYPEDEFS(Service);
	DECLARE_TYPENAME(Service);

	static Service::Ptr GetByNamePair(const String& hostName, const String& serviceName);

	Host::Ptr GetHost(void) const;

	virtual bool ResolveMacro(const String& macro, const CheckResult::Ptr& cr, String *result) const;

	static ServiceState StateFromString(const String& state);
	static String StateToString(ServiceState state);

	static StateType StateTypeFromString(const String& state);
	static String StateTypeToString(StateType state);

	static void RegisterApplyRuleHandler(void);

protected:
	virtual void OnConfigLoaded(void);

private:
	Host::Ptr m_Host;

	static bool EvaluateApplyRule(const Host::Ptr& host, const ApplyRule& rule);
	static void EvaluateApplyRules(const std::vector<ApplyRule>& rules);
};

I2_ICINGA_API boost::tuple<Host::Ptr, Service::Ptr> GetHostService(const Checkable::Ptr& checkable);

}

#endif /* SERVICE_H */
