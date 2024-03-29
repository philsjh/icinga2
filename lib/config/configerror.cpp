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

#include "config/configerror.h"
#include <sstream>

using namespace icinga;

ConfigError::ConfigError(const String& message)
	: m_Message(message)
{ }

ConfigError::~ConfigError(void) throw()
{ }

const char *ConfigError::what(void) const throw()
{
	return m_Message.CStr();
}

std::string icinga::to_string(const errinfo_debuginfo& e)
{
	std::ostringstream msgbuf;
	msgbuf << "Config location: " << e.value() << "\n";
	ShowCodeFragment(msgbuf, e.value(), true);
	return msgbuf.str();
}
