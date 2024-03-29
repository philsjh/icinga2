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

#ifndef _WIN32
#	include <stdlib.h>
#endif /* _WIN32 */
#include "icinga/icingaapplication.h"
#include "methods/randomchecktask.h"
#include "base/utility.h"
#include "base/convert.h"
#include "base/scriptfunction.h"
#include "base/logger_fwd.h"

using namespace icinga;

REGISTER_SCRIPTFUNCTION(RandomCheck, &RandomCheckTask::ScriptFunc);

void RandomCheckTask::ScriptFunc(const Checkable::Ptr& service, const CheckResult::Ptr& cr)
{
	String output = "Hello from ";
	output += Utility::GetHostName();

	Dictionary::Ptr perfdata = make_shared<Dictionary>();
	perfdata->Set("time", Convert::ToDouble(Utility::GetTime()));

	cr->SetOutput(output);
	cr->SetPerformanceData(perfdata);
	cr->SetState(static_cast<ServiceState>(Utility::Random() % 4));

	service->ProcessCheckResult(cr);
}

