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

#ifndef ARRAY_H
#define ARRAY_H

#include "base/i2-base.h"
#include "base/value.h"
#include <vector>

namespace icinga
{

/**
 * An array of Value items.
 *
 * @ingroup base
 */
class I2_BASE_API Array : public Object
{
public:
	DECLARE_PTR_TYPEDEFS(Array);

	/**
	 * An iterator that can be used to iterate over array elements.
	 */
	typedef std::vector<Value>::iterator Iterator;

	Value Get(unsigned int index) const;
	void Set(unsigned int index, const Value& value);
	void Add(const Value& value);

	Iterator Begin(void);
	Iterator End(void);

	size_t GetLength(void) const;

	void Insert(unsigned int index, const Value& value);
	void Remove(unsigned int index);
	void Remove(Iterator it);

	void Resize(size_t new_size);
	void Clear(void);

	void CopyTo(const Array::Ptr& dest) const;
	Array::Ptr ShallowClone(void) const;

	static Array::Ptr FromJson(cJSON *json);
	cJSON *ToJson(void) const;

private:
	std::vector<Value> m_Data; /**< The data for the array. */
};

inline Array::Iterator range_begin(Array::Ptr x)
{
	return x->Begin();
}

inline Array::Iterator range_end(Array::Ptr x)
{
	return x->End();
}

}

namespace boost
{

template<>
struct range_mutable_iterator<icinga::Array::Ptr>
{
	typedef icinga::Array::Iterator type;
};

template<>
struct range_const_iterator<icinga::Array::Ptr>
{
	typedef icinga::Array::Iterator type;
};

}

#endif /* ARRAY_H */
