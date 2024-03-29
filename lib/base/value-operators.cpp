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

#include "base/application.h"
#include "base/array.h"
#include "base/logger_fwd.h"
#include "base/utility.h"
#include "base/objectlock.h"
#include <cJSON.h>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

using namespace icinga;

Value::operator double(void) const
{
	const double *value = boost::get<double>(&m_Value);

	if (value)
		return *value;

	if (IsEmpty())
		return 0;

	return boost::lexical_cast<double>(m_Value);
}

Value::operator String(void) const
{
	Object *object;
	double integral, fractional;

	switch (GetType()) {
		case ValueEmpty:
			return String();
		case ValueNumber:
			fractional = modf(boost::get<double>(m_Value), &integral);

			if (fractional != 0)
				return boost::lexical_cast<std::string>(m_Value);
			else
				return boost::lexical_cast<std::string>((long)integral);
		case ValueString:
			return boost::get<String>(m_Value);
		case ValueObject:
			object = boost::get<Object::Ptr>(m_Value).get();
			return "Object of type '" + Utility::GetTypeName(typeid(*object)) + "'";
		default:
			BOOST_THROW_EXCEPTION(std::runtime_error("Unknown value type."));
	}
}

std::ostream& icinga::operator<<(std::ostream& stream, const Value& value)
{
	stream << static_cast<String>(value);
	return stream;
}

std::istream& icinga::operator>>(std::istream& stream, Value& value)
{
	String tstr;
	stream >> tstr;
	value = tstr;
	return stream;
}

bool Value::operator==(bool rhs) const
{
	return *this == Value(rhs);
}

bool Value::operator!=(bool rhs) const
{
	return !(*this == rhs);
}

bool Value::operator==(int rhs) const
{
	return *this == Value(rhs);
}

bool Value::operator!=(int rhs) const
{
	return !(*this == rhs);
}

bool Value::operator==(double rhs) const
{
	return *this == Value(rhs);
}

bool Value::operator!=(double rhs) const
{
	return !(*this == rhs);
}

bool Value::operator==(const char *rhs) const
{
	return static_cast<String>(*this) == rhs;
}

bool Value::operator!=(const char *rhs) const
{
	return !(*this == rhs);
}

bool Value::operator==(const String& rhs) const
{
	return static_cast<String>(*this) == rhs;
}

bool Value::operator!=(const String& rhs) const
{
	return !(*this == rhs);
}

bool Value::operator==(const Value& rhs) const
{
	if (IsEmpty() != rhs.IsEmpty())
		return false;

	if (IsEmpty())
		return true;

	if (IsObject() != rhs.IsObject())
		return false;

	if (IsObject()) {
		if (IsObjectType<Array>() && rhs.IsObjectType<Array>()) {
			Array::Ptr arr1 = *this;
			Array::Ptr arr2 = rhs;

			if (arr1 == arr2)
				return true;

			if (arr1->GetLength() != arr2->GetLength())
				return false;

			for (int i = 0; i < arr1->GetLength(); i++) {
				if (arr1->Get(i) != arr2->Get(i))
					return false;
			}

			return true;
		}

		return static_cast<Object::Ptr>(*this) == static_cast<Object::Ptr>(rhs);
	}

	if ((IsNumber() || IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(IsEmpty() && rhs.IsEmpty()))
		return static_cast<double>(*this) == static_cast<double>(rhs);

	if ((IsString() || IsEmpty()) && (rhs.IsString() || rhs.IsEmpty()) && !(IsEmpty() && rhs.IsEmpty()))
		return static_cast<String>(*this) == static_cast<String>(rhs);

	return false;
}

bool Value::operator!=(const Value& rhs) const
{
	return !(*this == rhs);
}

Value icinga::operator+(const Value& lhs, const char *rhs)
{
	return lhs + Value(rhs);
}

Value icinga::operator+(const char *lhs, const Value& rhs)
{
	return Value(lhs) + rhs;
}

Value icinga::operator+(const Value& lhs, const String& rhs)
{
	return lhs + Value(rhs);
}

Value icinga::operator+(const String& lhs, const Value& rhs)
{
	return Value(lhs) + rhs;
}

Value icinga::operator+(const Value& lhs, const Value& rhs)
{
	if ((lhs.IsString() || lhs.IsEmpty()) && (rhs.IsString() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<String>(lhs) + static_cast<String>(rhs);
	else if ((lhs.IsNumber() || lhs.IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<double>(lhs) + static_cast<double>(rhs);
	else if ((lhs.IsObjectType<Array>() || lhs.IsEmpty()) && (rhs.IsObjectType<Array>() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty())) {
		Array::Ptr result = make_shared<Array>();
		if (!lhs.IsEmpty())
			static_cast<Array::Ptr>(lhs)->CopyTo(result);
		if (!rhs.IsEmpty())
			static_cast<Array::Ptr>(rhs)->CopyTo(result);
		return result;
	} else if ((lhs.IsObjectType<Dictionary>() || lhs.IsEmpty()) && (rhs.IsObjectType<Dictionary>() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty())) {
		Dictionary::Ptr result = make_shared<Dictionary>();
		if (!lhs.IsEmpty())
			static_cast<Dictionary::Ptr>(lhs)->CopyTo(result);
		if (!rhs.IsEmpty())
			static_cast<Dictionary::Ptr>(rhs)->CopyTo(result);
		return result;
	} else {
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator + cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
	}
}

Value icinga::operator+(const Value& lhs, double rhs)
{
	return lhs + Value(rhs);
}

Value icinga::operator+(double lhs, const Value& rhs)
{
	return Value(lhs) + rhs;
}

Value icinga::operator+(const Value& lhs, int rhs)
{
	return lhs + Value(rhs);
}

Value icinga::operator+(int lhs, const Value& rhs)
{
	return Value(lhs) + rhs;
}

Value icinga::operator-(const Value& lhs, const Value& rhs)
{
	if ((lhs.IsNumber() || lhs.IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<double>(lhs) - static_cast<double>(rhs);
	else if ((lhs.IsObjectType<Array>() || lhs.IsEmpty()) && (rhs.IsObjectType<Array>() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty())) {
		if (lhs.IsEmpty())
			return make_shared<Array>();

		Array::Ptr result = make_shared<Array>();
		Array::Ptr left = lhs;
		Array::Ptr right = rhs;

		ObjectLock olock(left);
		BOOST_FOREACH(const Value& lv, left) {
			bool found = false;
			ObjectLock xlock(right);
			BOOST_FOREACH(const Value& rv, right) {
				if (lv == rv) {
					found = true;
					break;
				}
			}

			if (found)
				continue;

			result->Add(lv);
		}

		return result;
	} else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator - cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
}

Value icinga::operator-(const Value& lhs, double rhs)
{
	return lhs - Value(rhs);
}

Value icinga::operator-(double lhs, const Value& rhs)
{
	return Value(lhs) - rhs;
}

Value icinga::operator-(const Value& lhs, int rhs)
{
	return lhs - Value(rhs);
}

Value icinga::operator-(int lhs, const Value& rhs)
{
	return Value(lhs) - rhs;
}

Value icinga::operator*(const Value& lhs, const Value& rhs)
{
	if ((lhs.IsNumber() || lhs.IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<double>(lhs) * static_cast<double>(rhs);
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator * cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
}

Value icinga::operator*(const Value& lhs, double rhs)
{
	return lhs * Value(rhs);
}

Value icinga::operator*(double lhs, const Value& rhs)
{
	return Value(lhs) * rhs;
}

Value icinga::operator*(const Value& lhs, int rhs)
{
	return lhs * Value(rhs);
}

Value icinga::operator*(int lhs, const Value& rhs)
{
	return Value(lhs) * rhs;
}

Value icinga::operator/(const Value& lhs, const Value& rhs)
{
	if (lhs.IsEmpty())
		return 0;
	else if (rhs.IsEmpty())
		BOOST_THROW_EXCEPTION(std::invalid_argument("Right-hand side argument for operator / is Empty."));
	else if (lhs.IsNumber() && rhs.IsNumber()) {
		if (static_cast<double>(rhs) == 0)
			BOOST_THROW_EXCEPTION(std::invalid_argument("Right-hand side argument for operator / is 0."));

		return static_cast<double>(lhs) / static_cast<double>(rhs);
	} else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator / cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
}

Value icinga::operator/(const Value& lhs, double rhs)
{
	return lhs / Value(rhs);
}

Value icinga::operator/(double lhs, const Value& rhs)
{
	return Value(lhs) / rhs;
}

Value icinga::operator/(const Value& lhs, int rhs)
{
	return lhs / Value(rhs);
}

Value icinga::operator/(int lhs, const Value& rhs)
{
	return Value(lhs) / rhs;
}

Value icinga::operator&(const Value& lhs, const Value& rhs)
{
	if ((lhs.IsNumber() || lhs.IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<int>(lhs) & static_cast<int>(rhs);
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator & cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
}

Value icinga::operator&(const Value& lhs, double rhs)
{
	return lhs & Value(rhs);
}

Value icinga::operator&(double lhs, const Value& rhs)
{
	return Value(lhs) & rhs;
}

Value icinga::operator&(const Value& lhs, int rhs)
{
	return lhs & Value(rhs);
}

Value icinga::operator&(int lhs, const Value& rhs)
{
	return Value(lhs) & rhs;
}

Value icinga::operator|(const Value& lhs, const Value& rhs)
{
	if ((lhs.IsNumber() || lhs.IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<int>(lhs) | static_cast<int>(rhs);
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator | cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
}

Value icinga::operator|(const Value& lhs, double rhs)
{
	return lhs | Value(rhs);
}

Value icinga::operator|(double lhs, const Value& rhs)
{
	return Value(lhs) | rhs;
}

Value icinga::operator|(const Value& lhs, int rhs)
{
	return lhs | Value(rhs);
}

Value icinga::operator|(int lhs, const Value& rhs)
{
	return Value(lhs) | rhs;
}

Value icinga::operator<<(const Value& lhs, const Value& rhs)
{
	if ((lhs.IsNumber() || lhs.IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<int>(lhs) << static_cast<int>(rhs);
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator << cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
}

Value icinga::operator<<(const Value& lhs, double rhs)
{
	return lhs << Value(rhs);
}

Value icinga::operator<<(double lhs, const Value& rhs)
{
	return Value(lhs) << rhs;
}

Value icinga::operator<<(const Value& lhs, int rhs)
{
	return lhs << Value(rhs);
}

Value icinga::operator<<(int lhs, const Value& rhs)
{
	return Value(lhs) << rhs;
}

Value icinga::operator>>(const Value& lhs, const Value& rhs)
{
	if ((lhs.IsNumber() || lhs.IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<int>(lhs) >> static_cast<int>(rhs);
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator >> cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
}

Value icinga::operator>>(const Value& lhs, double rhs)
{
	return lhs >> Value(rhs);
}

Value icinga::operator>>(double lhs, const Value& rhs)
{
	return Value(lhs) >> rhs;
}

Value icinga::operator>>(const Value& lhs, int rhs)
{
	return lhs >> Value(rhs);
}

Value icinga::operator>>(int lhs, const Value& rhs)
{
	return Value(lhs) >> rhs;
}

Value icinga::operator<(const Value& lhs, const Value& rhs)
{
	if (lhs.IsString() && rhs.IsString())
		return static_cast<String>(lhs) < static_cast<String>(rhs);
	else if ((lhs.IsNumber() || lhs.IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<int>(lhs) < static_cast<int>(rhs);
	else if (lhs.GetTypeName() != rhs.GetTypeName())
		return lhs.GetTypeName() < rhs.GetTypeName();
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator < cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
}

Value icinga::operator<(const Value& lhs, double rhs)
{
	return lhs < Value(rhs);
}

Value icinga::operator<(double lhs, const Value& rhs)
{
	return Value(lhs) < rhs;
}

Value icinga::operator<(const Value& lhs, int rhs)
{
	return lhs < Value(rhs);
}

Value icinga::operator<(int lhs, const Value& rhs)
{
	return Value(lhs) < rhs;
}

Value icinga::operator>(const Value& lhs, const Value& rhs)
{
	if (lhs.IsString() && rhs.IsString())
		return static_cast<String>(lhs) > static_cast<String>(rhs);
	else if ((lhs.IsNumber() || lhs.IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<int>(lhs) > static_cast<int>(rhs);
	else if (lhs.GetTypeName() != rhs.GetTypeName())
		return lhs.GetTypeName() > rhs.GetTypeName();
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator > cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
}

Value icinga::operator>(const Value& lhs, double rhs)
{
	return lhs > Value(rhs);
}

Value icinga::operator>(double lhs, const Value& rhs)
{
	return Value(lhs) > rhs;
}

Value icinga::operator>(const Value& lhs, int rhs)
{
	return lhs > Value(rhs);
}

Value icinga::operator>(int lhs, const Value& rhs)
{
	return Value(lhs) > rhs;
}

Value icinga::operator<=(const Value& lhs, const Value& rhs)
{
	if (lhs.IsString() && rhs.IsString())
		return static_cast<String>(lhs) <= static_cast<String>(rhs);
	else if ((lhs.IsNumber() || lhs.IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<int>(lhs) <= static_cast<int>(rhs);
	else if (lhs.GetTypeName() != rhs.GetTypeName())
		return lhs.GetTypeName() <= rhs.GetTypeName();
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator <= cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
}

Value icinga::operator<=(const Value& lhs, double rhs)
{
	return lhs <= Value(rhs);
}

Value icinga::operator<=(double lhs, const Value& rhs)
{
	return Value(lhs) <= rhs;
}

Value icinga::operator<=(const Value& lhs, int rhs)
{
	return lhs <= Value(rhs);
}

Value icinga::operator<=(int lhs, const Value& rhs)
{
	return Value(lhs) <= rhs;
}

Value icinga::operator>=(const Value& lhs, const Value& rhs)
{
	if (lhs.IsString() && rhs.IsString())
		return static_cast<String>(lhs) >= static_cast<String>(rhs);
	else if ((lhs.IsNumber() || lhs.IsEmpty()) && (rhs.IsNumber() || rhs.IsEmpty()) && !(lhs.IsEmpty() && rhs.IsEmpty()))
		return static_cast<int>(lhs) >= static_cast<int>(rhs);
	else if (lhs.GetTypeName() != rhs.GetTypeName())
		return lhs.GetTypeName() >= rhs.GetTypeName();
	else
		BOOST_THROW_EXCEPTION(std::invalid_argument("Operator >= cannot be applied to values of type '" + lhs.GetTypeName() + "' and '" + rhs.GetTypeName() + "'"));
}

Value icinga::operator>=(const Value& lhs, double rhs)
{
	return lhs >= Value(rhs);
}

Value icinga::operator>=(double lhs, const Value& rhs)
{
	return Value(lhs) >= rhs;
}

Value icinga::operator>=(const Value& lhs, int rhs)
{
	return lhs >= Value(rhs);
}

Value icinga::operator>=(int lhs, const Value& rhs)
{
	return Value(lhs) >= rhs;
}
