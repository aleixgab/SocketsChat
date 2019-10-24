#pragma once

// Add as many messages as you need depending on the
// functionalities that you decide to implement.

enum class ClientMessage
{
	Hello,
	SendMsg
};

enum class ServerMessage
{
	NoWelcome = -1,
	Welcome = 0,
	SendMsg,
	LogOut,
	NameChange
};

