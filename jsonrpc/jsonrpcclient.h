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

#ifndef JSONRPCCLIENT_H
#define JSONRPCCLIENT_H

namespace icinga
{

/**
 * Event arguments for the "new message" event.
 *
 * @ingroup jsonrpc
 */
struct I2_JSONRPC_API NewMessageEventArgs : public EventArgs
{
	icinga::MessagePart Message;
};

/**
 * A JSON-RPC client.
 *
 * @ingroup jsonrpc
 */
class I2_JSONRPC_API JsonRpcClient : public TlsClient
{
public:
	typedef shared_ptr<JsonRpcClient> Ptr;
	typedef weak_ptr<JsonRpcClient> WeakPtr;

	JsonRpcClient(TcpClientRole role, shared_ptr<SSL_CTX> sslContext);

	void SendMessage(const MessagePart& message);

	virtual void Start(void);

	boost::signal<void (const NewMessageEventArgs&)> OnNewMessage;

private:
	void DataAvailableHandler(const EventArgs&);
};

JsonRpcClient::Ptr JsonRpcClientFactory(TcpClientRole role, shared_ptr<SSL_CTX> sslContext);

}

#endif /* JSONRPCCLIENT_H */
