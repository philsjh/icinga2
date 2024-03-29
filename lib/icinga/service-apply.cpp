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
#include "config/configitembuilder.h"
#include "base/initialize.h"
#include "base/dynamictype.h"
#include "base/convert.h"
#include "base/logger_fwd.h"
#include "base/context.h"
#include <boost/foreach.hpp>

using namespace icinga;

INITIALIZE_ONCE(&Service::RegisterApplyRuleHandler);

void Service::RegisterApplyRuleHandler(void)
{
	std::vector<String> targets;
	targets.push_back("Host");
	ApplyRule::RegisterType("Service", targets, &Service::EvaluateApplyRules);
}

bool Service::EvaluateApplyRule(const Host::Ptr& host, const ApplyRule& rule)
{
	DebugInfo di = rule.GetDebugInfo();

	std::ostringstream msgbuf;
	msgbuf << "Evaluating 'apply' rule (" << di << ")";
	CONTEXT(msgbuf.str());

	Dictionary::Ptr locals = make_shared<Dictionary>();
	locals->Set("host", host);

	if (!rule.EvaluateFilter(locals))
		return false;

	std::ostringstream msgbuf2;
	msgbuf2 << "Applying service '" << rule.GetName() << "' to host '" << host->GetName() << "' for rule " << di;
	Log(LogDebug, "icinga", msgbuf2.str());

	ConfigItemBuilder::Ptr builder = make_shared<ConfigItemBuilder>(di);
	builder->SetType("Service");
	builder->SetName(rule.GetName());
	builder->SetScope(rule.GetScope());

	builder->AddExpression(make_shared<AExpression>(&AExpression::OpSet,
	    make_shared<AExpression>(&AExpression::OpLiteral, "host_name", di), 
	    make_shared<AExpression>(&AExpression::OpLiteral, host->GetName(), di),
	    di));

	builder->AddExpression(make_shared<AExpression>(&AExpression::OpSet,
	    make_shared<AExpression>(&AExpression::OpLiteral, "name", di),
	    make_shared<AExpression>(&AExpression::OpLiteral, rule.GetName(), di),
	    di));

	builder->AddExpression(rule.GetExpression());

	ConfigItem::Ptr serviceItem = builder->Compile();
	serviceItem->Register();
	DynamicObject::Ptr dobj = serviceItem->Commit();
	dobj->OnConfigLoaded();

	return true;
}

void Service::EvaluateApplyRules(const std::vector<ApplyRule>& rules)
{
	int apply_count = 0;

	BOOST_FOREACH(const ApplyRule& rule, rules) {
		apply_count = 0;

		BOOST_FOREACH(const Host::Ptr& host, DynamicType::GetObjects<Host>()) {
			CONTEXT("Evaluating 'apply' rules for host '" + host->GetName() + "'");

			if (EvaluateApplyRule(host, rule))
				apply_count++;
		}

		if (apply_count == 0)
			Log(LogWarning, "icinga", "Apply rule '" + rule.GetName() + "' for host does not match anywhere!");
	}
}
