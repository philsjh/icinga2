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

#ifndef USERGROUP_H
#define USERGROUP_H

namespace icinga
{

/**
 * An Icinga user group.
 *
 * @ingroup icinga
 */
class I2_ICINGA_API UserGroup : public DynamicObject
{
public:
	typedef shared_ptr<UserGroup> Ptr;
	typedef weak_ptr<UserGroup> WeakPtr;

	UserGroup(const Dictionary::Ptr& properties);
	~UserGroup(void);

	static UserGroup::Ptr GetByName(const String& name);

	String GetDisplayName(void) const;

	set<User::Ptr> GetMembers(void) const;

	static void InvalidateMembersCache(void);

protected:
	virtual void OnRegistrationCompleted(void);

private:
	Attribute<String> m_DisplayName;

	static boost::mutex m_Mutex;
	static map<String, vector<User::WeakPtr> > m_MembersCache;
	static bool m_MembersCacheNeedsUpdate;
	static Timer::Ptr m_MembersCacheTimer;

	static void RefreshMembersCache(void);
};

}

#endif /* HOSTGROUP_H */