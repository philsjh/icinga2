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

#ifndef DEPENDENCY_H
#define DEPENDENCY_H

#include "icinga/i2-icinga.h"
#include "icinga/dependency.th"
#include "config/applyrule.h"
#include "base/array.h"
#include "base/dictionary.h"

namespace icinga
{

/**
 * A service dependency..
 *
 * @ingroup icinga
 */
class I2_ICINGA_API Dependency : public ObjectImpl<Dependency>
{
public:
	DECLARE_PTR_TYPEDEFS(Dependency);
	DECLARE_TYPENAME(Dependency);

	shared_ptr<Checkable> GetParent(void) const;
	shared_ptr<Checkable> GetChild(void) const;

	TimePeriod::Ptr GetPeriod(void) const;

	bool IsAvailable(DependencyType dt) const;

	static void RegisterApplyRuleHandler(void);

	static void ValidateFilters(const String& location, const Dictionary::Ptr& attrs);

protected:
	virtual void OnConfigLoaded(void);
	virtual void OnStateLoaded(void);
	virtual void Stop(void);

private:
	static bool EvaluateApplyRule(const Checkable::Ptr& checkable, const ApplyRule& rule);
	static void EvaluateApplyRules(const std::vector<ApplyRule>& rules);
};

}

#endif /* DEPENDENCY_H */
