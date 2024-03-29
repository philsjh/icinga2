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

#ifndef QUERY_H
#define QUERY_H

#include "livestatus/filter.h"
#include "livestatus/aggregator.h"
#include "base/object.h"
#include "base/array.h"
#include "base/stream.h"
#include <deque>

using namespace icinga;

namespace icinga
{

enum LivestatusError
{
	LivestatusErrorOK = 200,
	LivestatusErrorNotFound = 404,
	LivestatusErrorQuery = 452
};

/**
 * @ingroup livestatus
 */
class Query : public Object
{
public:
	DECLARE_PTR_TYPEDEFS(Query);

	Query(const std::vector<String>& lines, const String& compat_log_path);

	bool Execute(const Stream::Ptr& stream);

	static int GetExternalCommands(void);

private:
	String m_Verb;

	bool m_KeepAlive;

	/* Parameters for GET queries. */
	String m_Table;
	std::vector<String> m_Columns;
	std::vector<String> m_Separators;

	Filter::Ptr m_Filter;
	std::deque<Aggregator::Ptr> m_Aggregators;

	String m_OutputFormat;
	bool m_ColumnHeaders;

	String m_ResponseHeader;

	/* Parameters for COMMAND queries. */
	String m_Command;

	/* Parameters for invalid queries. */
	int m_ErrorCode;
	String m_ErrorMessage;
	
	unsigned long m_LogTimeFrom;
	unsigned long m_LogTimeUntil;
	String m_CompatLogPath;

	void PrintResultSet(std::ostream& fp, const Array::Ptr& rs);
	void PrintCsvArray(std::ostream& fp, const Array::Ptr& array, int level);

	void ExecuteGetHelper(const Stream::Ptr& stream);
	void ExecuteCommandHelper(const Stream::Ptr& stream);
	void ExecuteErrorHelper(const Stream::Ptr& stream);

	void SendResponse(const Stream::Ptr& stream, int code, const String& data);
	void PrintFixed16(const Stream::Ptr& stream, int code, const String& data);
	
	static Filter::Ptr ParseFilter(const String& params, unsigned long& from, unsigned long& until);
};

}

#endif /* QUERY_H */
