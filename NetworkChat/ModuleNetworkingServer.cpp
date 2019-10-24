#include "ModuleNetworkingServer.h"




//////////////////////////////////////////////////////////////////////
// ModuleNetworkingServer public methods
//////////////////////////////////////////////////////////////////////

bool ModuleNetworkingServer::start(int port)
{
	// TODO(jesus): TCP listen socket stuff

	// - Create the listenSocket
	listenSocket = socket(AF_INET, SOCK_STREAM, 0);

	// - Set address reuse
	int enable = 1;
	int res = setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&enable, sizeof(int));
	if (res == SOCKET_ERROR)
	{
		wchar_t* error = NULL;
		FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, WSAGetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPWSTR)&error, 0, NULL);

		ELOG("Socket error: %s", error);

		return false;
	}

	// - Bind the socket to a local interface
	sockaddr_in bindAddr;
	bindAddr.sin_family = AF_INET; // IPv4
	bindAddr.sin_port = htons(port); // Port
	bindAddr.sin_addr.S_un.S_addr = INADDR_ANY; // Any local IP address

	res = bind(listenSocket, (const sockaddr*)&bindAddr, sizeof(bindAddr));
	if (res != 0)
	{
		return false;
	}
	// - Enter in listen mode
	res = listen(listenSocket, 1);
	if (res != 0)
	{
		reportError("Socket error: %s");

		return false;
	}
	
	// - Add the listenSocket to the managed list of sockets using addSocket()
	addSocket(listenSocket);


	state = ServerState::Listening;

	LOG("Server started");

	return true;
}

bool ModuleNetworkingServer::isRunning() const
{
	return state != ServerState::Stopped;
}



//////////////////////////////////////////////////////////////////////
// Module virtual methods
//////////////////////////////////////////////////////////////////////

bool ModuleNetworkingServer::update()
{
	return true;
}

bool ModuleNetworkingServer::gui()
{
	if (state != ServerState::Stopped)
	{
		// NOTE(jesus): You can put ImGui code here for debugging purposes
		const ImU32 flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		ImGui::Begin("Server Window", nullptr, flags);

		Texture *tex = App->modResources->server;
		ImVec2 texSize(400.0f, 400.0f * tex->height / tex->width);
		ImGui::Image(tex->shaderResource, texSize);

		ImGui::Text("List of connected sockets:");

		for (auto& connectedSocket : connectedSockets)
		{
			ImGui::Separator();
			ImGui::Text("Socket ID: %d", connectedSocket.socket);
			ImGui::Text("Address: %d.%d.%d.%d:%d",
				connectedSocket.address.sin_addr.S_un.S_un_b.s_b1,
				connectedSocket.address.sin_addr.S_un.S_un_b.s_b2,
				connectedSocket.address.sin_addr.S_un.S_un_b.s_b3,
				connectedSocket.address.sin_addr.S_un.S_un_b.s_b4,
				ntohs(connectedSocket.address.sin_port));

			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, connectedSocket.isAdmin ? ImVec4(0.0, 0.7, 0.0, 1.0) : ImVec4(0.8, 0.0, 0.0, 1.0));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, connectedSocket.isAdmin ? ImVec4(0.0, 0.55, 0.0, 1.0) : ImVec4(0.6, 0.0, 0.0, 1.0));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, connectedSocket.isAdmin ? ImVec4(0.0, 0.4, 0.0, 1.0) : ImVec4(0.4, 0.0, 0.0, 1.0));
			std::string admin = "Admin##" + connectedSocket.playerName;
			if (ImGui::Button(admin.c_str()))
			{
				connectedSocket.isAdmin = !connectedSocket.isAdmin;

				SendMsg(connectedSocket.isAdmin == true ? "You are Admin now!" : "You are no longer Admin", connectedSocket.isAdmin == true ? 2 : 4, connectedSocket.socket);

			}
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();
			ImGui::PopStyleColor();

			ImGui::SameLine();
			std::string kick = "Kick##" + connectedSocket.playerName;
			if (ImGui::Button(kick.c_str()))
			{
					OutputMemoryStream stream;
					stream << ServerMessage::LogOut;
					stream << "You have been kicked out the server by the server";

					SendPacket(stream, connectedSocket.socket);
			}

			ImGui::Text("Player name: %s", connectedSocket.playerName.c_str());

		}


		ImGui::End();
	}

	return true;
}



//////////////////////////////////////////////////////////////////////
// ModuleNetworking virtual methods
//////////////////////////////////////////////////////////////////////

bool ModuleNetworkingServer::isListenSocket(SOCKET socket) const
{
	return socket == listenSocket;
}

void ModuleNetworkingServer::onSocketConnected(SOCKET socket, const sockaddr_in &socketAddress)
{
	// Add a new connected socket to the list
	ConnectedSocket connectedSocket;
	connectedSocket.socket = socket;
	connectedSocket.address = socketAddress;
	connectedSockets.push_back(connectedSocket);
}

void ModuleNetworkingServer::onSocketReceivedData(SOCKET socket, const InputMemoryStream& packet)
{
	ClientMessage clientMessage;
	packet >> clientMessage;

	if (clientMessage == ClientMessage::Hello)
	{
		std::string playerName;
		packet >> playerName;

		bool userNameInUse = false;
		// Set the player name of the corresponding connected socket proxy
		for (auto& connectedSocket : connectedSockets)
		{
			if (connectedSocket.socket == socket)
			{
				for (auto& connected : connectedSockets)
				{
					if (connected.playerName == playerName)
					{
						OutputMemoryStream noWelcomePacket;
						noWelcomePacket << ServerMessage::NoWelcome;
						noWelcomePacket << "User name already in use, please use other name";

						SendPacket(noWelcomePacket, socket);


						userNameInUse = true;
						break;
					}

				}

				if (!userNameInUse)
				{
					connectedSocket.playerName = playerName;
					connectedSocket.isConnected = true;

					OutputMemoryStream welcomePacket;
					welcomePacket << ServerMessage::Welcome;
					welcomePacket << "Welcome to the server";

					SendPacket(welcomePacket, socket);

					for (auto& connected : connectedSockets)
					{
						std::string user = "***** ";
						user += playerName;
						user += " joined *****";

						SendMsg(user.c_str(), 2u, connected.socket);
					}
				}
			}
		}

		if (userNameInUse)
		{
			onSocketDisconnected(socket);
		}
	}

	else if (clientMessage == ClientMessage::SendMsg)
	{
		std::string text;
		packet >> text;

		if (text.length() > 0)
		{
			char fLetter = text.at(0);

			if (fLetter == '/')
			{
				//text.erase(0, 1);

				std::string command = text.substr(1, text.find(" ") - 1);

				if (command.compare("help") == 0)
				{
					SendMsg("***** Commands list *****\n\t/help\n\t/kick [username]\n\t/list\n\t/whisper [username] [message]", 1u, socket);
				}

				else if (command.compare("list") == 0)
				{
					std::string userList = "***** Users List *****\n";

					for (auto& connected : connectedSockets)
					{
						userList += connected.playerName;
						userList += "\n\t";
					}

					SendMsg(userList.c_str(), 1u, socket);

				}

				else if (command.compare("kick") == 0)
				{
					ConnectedSocket connected;
					if (GetConnectedSocket(socket, connected))
					{
						if (connected.isAdmin)
						{
							std::string kickName = text.substr(text.find(" "), text.size() - 1);
							kickName = kickName.substr(1, kickName.find(" ") - 1);

							ConnectedSocket toKick;
							if (GetConnectedSocket(kickName, toKick))
							{
								if (!toKick.isAdmin)
								{
									OutputMemoryStream stream;
									stream << ServerMessage::LogOut;
									stream << "You have been kicked out the server by " + connected.playerName;

									SendPacket(stream, toKick.socket);
								}
							}
						}
					
						else
						{
							SendMsg("You are not Admin, get permission from the Server or an Admin ", 3u, socket);

						}
					}
				}

				else if (command.compare("whisper") == 0)
				{
					std::string msgText = text.substr(text.find(" ") + 1);
					std::string user = msgText.substr(0, msgText.find(" "));
					msgText = msgText.substr(user.size() + 1);

					
					bool sendMsg = false;
					for (auto& connected : connectedSockets)
					{
						if (connected.playerName == user)
						{
							std::string finalMsg = msgText;
							ConnectedSocket socketSender;
							if (GetConnectedSocket(socket, socketSender))
								finalMsg = socketSender.playerName + " whisper to " + connected.playerName + " : " + msgText;

							SendMsg(finalMsg.c_str(), 0u, connected.socket);
							SendMsg(finalMsg.c_str(), 0u, socket);
							sendMsg = true;
							break;

						}
					}
					if (!sendMsg)
						SendMsg("Wrong username", 3u, socket);
				}

				else if (command.compare("change_name") == 0)
				{
					ConnectedSocket connected;
					if (GetConnectedSocket(socket, connected))
					{
						if (text.find(" ") != std::string::npos)
						{
							std::string newName = text.substr(text.find(" "), text.size() - 1);
							newName = newName.substr(1, newName.find(" ") - 1);

							bool canChange = true;
							for (auto& connected : connectedSockets)
							{
								if (connected.playerName == newName)
								{
									SendMsg("Name already in use", 4u, socket);
									canChange = false;
									break;
								}
							}

							if (canChange)
								for (std::vector<ConnectedSocket>::iterator it = connectedSockets.begin(); it != connectedSockets.end(); ++it)
								{
									if ((*it).socket == socket)
									{
										(*it).playerName = newName;

										OutputMemoryStream stream;
										stream << ServerMessage::NameChange;
										stream << newName;
										SendPacket(stream, socket);
									}
								}
						}
						else SendMsg("Write your new name, see /help", 3u, socket);
					}
				}

				else if (command.compare("admin") == 0)
				{
					ConnectedSocket connected;
					if (GetConnectedSocket(socket, connected))
					{
						if (connected.isAdmin)
						{
							std::string newAdmin = text.substr(text.find(" "), text.size() - 1);
							newAdmin = newAdmin.substr(1, newAdmin.find(" ") - 1);

							SetNewAdmin(newAdmin);


						}
					}
				}

				else
				{
					SendMsg("Unknown command, try /help to get all commands", 3u, socket);

				}
			}

			else
			{
				ConnectedSocket socketSender;
				if (GetConnectedSocket(socket, socketSender))
				{
					std::string completedText = socketSender.playerName + ": " + text;
					for (auto& connected : connectedSockets)
					{
						if (connected.socket != socket)
							SendMsg(completedText.c_str(), 0u, connected.socket);
					}
				}
			}
		}
	}
}

void ModuleNetworkingServer::onSocketDisconnected(SOCKET socket)
{
	// Remove the connected socket from the list
	ConnectedSocket leftSocket;
	GetConnectedSocket(socket, leftSocket);
	std::vector<ConnectedSocket>::iterator disconectedSocket = connectedSockets.end();
	for (std::vector<ConnectedSocket>::iterator it = connectedSockets.begin(); it != connectedSockets.end(); ++it)
	{
		auto &connectedSocket = *it;
		if (connectedSocket.socket == socket)
		{
			disconectedSocket = it;
		}
		else if (connectedSockets.back().isConnected)
		{
			std::string text = "***** ";
			text += leftSocket.playerName;
			text += " left *****";
			SendMsg(text.c_str(), 4u, connectedSocket.socket);
		}
	}
	if(disconectedSocket != connectedSockets.end())
		connectedSockets.erase(disconectedSocket);
}

bool ModuleNetworkingServer::GetConnectedSocket(SOCKET socket, ConnectedSocket& connected)
{
	for (std::vector<ConnectedSocket>::iterator it = connectedSockets.begin(); it != connectedSockets.end(); ++it)
	{
		if ((*it).socket == socket)
		{
			connected = *it;
			return true;
		}
	}
	return false;
}


bool ModuleNetworkingServer::GetConnectedSocket(std::string name, ConnectedSocket& connected)
{
	for (std::vector<ConnectedSocket>::iterator it = connectedSockets.begin(); it != connectedSockets.end(); ++it)
	{
		if ((*it).playerName.compare(name) == 0)
		{
			connected = *it;
			return true;
		}
	}
	return false;
}

void ModuleNetworkingServer::SetNewAdmin(std::string& newAdmin)
{
	for (std::vector<ConnectedSocket>::iterator it = connectedSockets.begin(); it != connectedSockets.end(); ++it)
	{
		if ((*it).playerName.compare(newAdmin) == 0)
		{
			(*it).isAdmin = true;

			SendMsg("You are Admin now!", 2, (*it).socket);
		}
	}
}