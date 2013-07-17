/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012 Icinga Development Team (http://www.icinga.org/)        *
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

#include "ido/hostdbobject.h"
#include "ido/dbtype.h"
#include "icinga/host.h"

using namespace icinga;

REGISTER_DBTYPE("Host", "host", 1, HostDbObject);

HostDbObject::HostDbObject(const String& name1, const String& name2)
	: DbObject(DbType::GetByName("Host"), name1, name2)
{ }

Dictionary::Ptr HostDbObject::GetFields(void) const
{
	Dictionary::Ptr fields = boost::make_shared<Dictionary>();
	Host::Ptr host = static_pointer_cast<Host>(GetObject());

	fields->Set("display_name", host->GetDisplayName());

	return fields;
}