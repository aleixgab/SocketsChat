#include "ModuleNetworkingClient.h"


bool  ModuleNetworkingClient::start(const char * serverAddressStr, int serverPort, const char *pplayerName)
{
	playerName = pplayerName;


	// TODO(jesus): TCP connection stuff
	// - Create the socket
	socket = ::socket(AF_INET, SOCK_STREAM, 0);

	//// Set non-blocking socket
	//u_long nonBlocking = 1;
	//int res = ioctlsocket(socket, FIONBIO, &nonBlocking);
	//if (res == SOCKET_ERROR) {
	//	ELOG("Socket error, exiting...");
	//	return false;
	//}

	// - Create the remote address object
	sockaddr_in remoteAddr;
	remoteAddr.sin_family = AF_INET; // IPv4
	remoteAddr.sin_port = htons(serverPort); // Port
	inet_pton(AF_INET, serverAddressStr, &remoteAddr.sin_addr);
	
	// - Connect to the remote address
	int res = connect(socket, (const sockaddr*)&remoteAddr, sizeof(remoteAddr));
	if (res != NO_ERROR)
	{
		ELOG("Error connecting to server");
		return false;
	}
	// - Add the created socket to the managed list of sockets using addSocket()
	addSocket(socket);
	

	// If everything was ok... change the state
	state = ClientState::Start;

	return true;
}

bool ModuleNetworkingClient::isRunning() const
{
	return state != ClientState::Stopped;
}

bool ModuleNetworkingClient::update()
{
	if (state == ClientState::Start)
	{
		OutputMemoryStream packet;
		packet << ClientMessage::Hello;
		packet << playerName;

		if (SendPacket(packet, socket))
			state = ClientState::Logging;
		else
		{
			disconnect();
			state = ClientState::Stopped;
		}
	}

	return true;
}

bool ModuleNetworkingClient::gui()
{
	if (state != ClientState::Stopped)
	{
		// NOTE(jesus): You can put ImGui code here for debugging purposes
		const ImU32 flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		ImGui::Begin("Client Window", nullptr, flags);

		Texture *tex = App->modResources->client;
		ImVec2 texSize(400.0f, 400.0f * tex->height / tex->width);
		ImGui::Image(tex->shaderResource, texSize);

		ImGui::Text("Hello %s: Welcome to the chat", playerName.c_str());
		ImGui::SameLine();
		if (ImGui::Button("Logout"))
		{
			disconnect();
			onSocketDisconnected(socket);

			textVec.clear();
		}

		if (state == ClientState::Connected)
		{
			ImGui::BeginChildFrame(1,ImVec2(400,425));
			for (uint32 i = 0u; i < textVec.size(); ++i)
			{
				TextEntry entry = textVec[i];
				switch (entry.type)
				{
				case 1://Yellow text
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
					break;
				case 2://Green text
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
					break;
				case 3://Grey text
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
					break;
				case 4://Red text
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
					break;
				default:
					break;
				}
				ImGui::TextWrapped("%s", entry.message);

				if(entry.type > 0)
					ImGui::PopStyleColor();
			}

			ImGui::EndChildFrame();

			char inputText[128] = "";
			if (ImGui::InputText("##inputText", inputText, 128, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				SendMsg(inputText, 0u, socket);



				if (inputText[0] != '/')
				{
					std::string msg = "You: ";
					msg.append(inputText);
					msg += "\n";

					char* newMsg = new char[msg.length()];
					lstrcpyA(newMsg, msg.c_str());

					TextEntry text;
					text.message = newMsg;
					textVec.push_back(text);
				}
			}

			// Demonstrate keeping auto focus on the input box
			if (ImGui::IsItemHovered() || (ImGui::IsRootWindowOrAnyChildFocused() && !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)))
				ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget
		}

		ImGui::End();

	}

	return true;
}

void ModuleNetworkingClient::onSocketReceivedData(SOCKET socket, const InputMemoryStream& packet)
{
	ServerMessage serverMessage;
	packet >> serverMessage;

	LOG("New data received, type %i", serverMessage);

	switch (serverMessage)
	{
	case ServerMessage::Welcome:
	{
		TextEntry text;
		text.message = "*** You have joined the server *** \n";
		text.type = 2u;

		textVec.push_back(text);

		state = ClientState::Connected;
		break;
	}
	case ServerMessage::NoWelcome:
		ELOG("User name already in use, please use other name");
		state = ClientState::Stopped;
		break;
	case ServerMessage::SendMsg:
	{
		std::string msg;
		uint32 type = 0u;
		packet >> msg;
		packet >> type;
		msg += "\n";

		char* newMsg = new char[msg.length()];
		lstrcpyA(newMsg, msg.c_str());

		TextEntry text;
		text.message = newMsg;
		text.type = type;

		textVec.push_back(text);
	}
		break;
	case ServerMessage::LogOut:
	{
		disconnect();
		onSocketDisconnected(socket);

		std::string msg;
		packet >> msg;
		LOG(msg.c_str());
	}
		break;
	case ServerMessage::NameChange:
	{
		std::string name;
		packet >> name;
		playerName = name;

		TextEntry text;
		text.message = "Name changed succesfully\n";
		text.type = 3u;

		textVec.push_back(text);
	}
	break;
	default:
		break;
	}
}

void ModuleNetworkingClient::onSocketDisconnected(SOCKET socket)
{
	state = ClientState::Stopped;
}

