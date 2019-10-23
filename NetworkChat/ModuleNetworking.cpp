#include "Networks.h"
#include "ModuleNetworking.h"
#include <list>

static uint8 NumModulesUsingWinsock = 0;



void ModuleNetworking::reportError(const char* inOperationDesc)
{
	LPVOID lpMsgBuf;
	DWORD errorNum = WSAGetLastError();

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		errorNum,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0, NULL);

	ELOG("Error %s: %d- %s", inOperationDesc, errorNum, lpMsgBuf);
}

bool ModuleNetworking::SendPacket(const OutputMemoryStream& packet, SOCKET socket)
{
	int result = send(socket, packet.GetBufferPtr(), packet.GetSize(), 0);
	if (result == SOCKET_ERROR)
	{
		reportError("Send");
		return false;
	}
	return true;
}

void ModuleNetworking::disconnect()
{
	for (SOCKET socket : sockets)
	{
		shutdown(socket, 2);
		closesocket(socket);
	}

	sockets.clear();
}

bool ModuleNetworking::init()
{
	if (NumModulesUsingWinsock == 0)
	{
		NumModulesUsingWinsock++;

		WORD version = MAKEWORD(2, 2);
		WSADATA data;
		if (WSAStartup(version, &data) != 0)
		{
			reportError("ModuleNetworking::init() - WSAStartup");
			return false;
		}
	}

	return true;
}

bool ModuleNetworking::preUpdate()
{
	if (sockets.empty()) return true;

	// NOTE(jesus): You can use this temporary buffer to store data from recv()
	const uint32 incomingDataBufferSize = Kilobytes(1);
	byte incomingDataBuffer[incomingDataBufferSize];

	// TODO(jesus): select those sockets that have a read operation available

	// New socket set
	fd_set readfds;
	FD_ZERO(&readfds);

	// Fill the set
	for (auto s : sockets) {
		FD_SET(s, &readfds);
	}

	// Timeout (return immediately)
	TIMEVAL timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	// Select (check for readability)
	int res = select(0, &readfds, nullptr, nullptr, &timeout);
	if (res == SOCKET_ERROR) {
		ELOG("Error selecting sockets");
		return false;
	}


	// TODO(jesus): for those sockets selected, check wheter or not they are
	// a listen socket or a standard socket and perform the corresponding
	// operation (accept() an incoming connection or recv() incoming data,
	// respectively).
	for (int i = 0; i < readfds.fd_count; i++)
	{
		SOCKET curr = readfds.fd_array[i];

		// On accept() success, communicate the new connected socket to the
		// subclass (use the callback onSocketConnected()), and add the new
		// connected socket to the managed list of sockets.
		if (isListenSocket(curr))
		{
			sockaddr_in bindAddr;
			int fromlen = sizeof(bindAddr);

			SOCKET client = accept(curr, (sockaddr*)&bindAddr, &fromlen);

			if (client == INVALID_SOCKET)
			{
				reportError("Socket error: %ls");

				continue;
			}

			onSocketConnected(client, bindAddr);
			sockets.push_back(client);

		}
		// On recv() success, communicate the incoming data received to the
		// subclass (use the callback onSocketReceivedData()).
		else
		{
			InputMemoryStream packet;
			int recvSize = recv(curr, packet.GetBufferPtr(), packet.GetCapacity(), 0);
			if (recvSize == SOCKET_ERROR)
			{
				int lastError = WSAGetLastError();

				if (lastError == WSAEWOULDBLOCK)
				{
					// Do nothing special, there was no data to receive
				}
				else
				{
					// Other error handling

					WLOG("Socket disconnected");

					// TODO(jesus): handle disconnections. Remember that a socket has been
					// disconnected from its remote end either when recv() returned 0,
					// or when it generated some errors such as ECONNRESET.
					// Communicate detected disconnections to the subclass using the callback
					// onSocketDisconnected().
					onSocketDisconnected(curr);

					for (std::vector<SOCKET>::iterator it = sockets.begin(); it != sockets.end(); ++it)
					{
						if ((*it) == curr)
						{
							sockets.erase(it);
							break;
						}
					}
					continue;
				}
			}
			else if (recvSize == 0)
			{
				onSocketDisconnected(curr);
				for (std::vector<SOCKET>::iterator it = sockets.begin(); it != sockets.end(); ++it)
				{
					if ((*it) == curr)
					{
						sockets.erase(it);
						break;
					}
				}
				continue;
			}

			else // Success
			{
				// Process received data
				packet.SetSize((uint32)recvSize);
				onSocketReceivedData(curr, packet);

			}

		}
	}

	// TODO(jesus): Finally, remove all disconnected sockets from the list
	// of managed sockets.

	return true;
}

bool ModuleNetworking::cleanUp()
{
	disconnect();

	NumModulesUsingWinsock--;
	if (NumModulesUsingWinsock == 0)
	{

		if (WSACleanup() != 0)
		{
			reportError("ModuleNetworking::cleanUp() - WSACleanup");
			return false;
		}
	}

	return true;
}

void ModuleNetworking::addSocket(SOCKET socket)
{
	sockets.push_back(socket);
}


void ModuleNetworking::SendMsg(const char* text, uint32 type, SOCKET socket)
{
	OutputMemoryStream stream;
	stream << ServerMessage::SendMsg;
	stream << text;
	stream << type;

	SendPacket(stream, socket);
}