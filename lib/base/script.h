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

#ifndef SCRIPT_H
#define SCRIPT_H

namespace icinga
{

class ScriptInterpreter;

/**
 * A script.
 *
 * @ingroup base
 */
class I2_BASE_API Script : public DynamicObject
{
public:
	typedef shared_ptr<Script> Ptr;
	typedef weak_ptr<Script> WeakPtr;

	Script(const Dictionary::Ptr& properties);

	String GetLanguage(void) const;
	String GetCode(void) const;

protected:
	virtual void OnRegistrationCompleted(void);
	virtual void OnAttributeUpdate(const String& name, const Value& oldValue);

private:
	shared_ptr<ScriptInterpreter> m_Interpreter;

	void SpawnInterpreter(void);
};

}

#endif /* SCRIPT_H */