#include "ipc_main.hpp"
#include "ipc.hpp"
#include "afterframes.hpp"

static qboolean OnChange_tas_ipc(cvar_t* var, char* string);
static ipc::IPCServer server;

cvar_t tas_ipc = { "tas_ipc", "0", 0, OnChange_tas_ipc };
cvar_t tas_ipc_port = { "tas_ipc_port", "10001"};
cvar_t tas_ipc_verbose = { "tas_ipc_verbose", "1"};
cvar_t tas_ipc_timeout = { "tas_ipc_timeout", "200"};
constexpr float MAX_TIMEOUT = 20000;


static void IPC_Print(const char* string)
{
	if (tas_ipc_verbose.value != 0)
	{
		char* str = const_cast<char*>(string);
		Con_Printf("IPC: %s", str);
	}
}

static void Cmd_Callback(const nlohmann::json& msg)
{
	if (msg.find("cmd") != msg.end())
	{
		std::string cmd = msg["cmd"];
		AddAfterframes(0, cmd.c_str(), NoFilter);
	}
	else
	{
		IPC_Print("Message contained no cmd field!\n");
	}
}

static void Start_IPC()
{
	if (!ipc::Winsock_Initialized())
	{
		server.InitWinsock();
		server.StartListening(tas_ipc_port.string);
	}
}

static void Shutdown_IPC()
{
	if (ipc::Winsock_Initialized())
	{
		server.CloseConnections();
		ipc::Shutdown_IPC();
	}
}

static qboolean OnChange_tas_ipc(cvar_t* var, char* string)
{
	int a = std::atoi(string);

	if (a != 0) {
		Start_IPC();
		IPC_Print("Started.\n");
	}
	else
	{
		Shutdown_IPC();
		IPC_Print("Shut down.\n");
	}

	return qfalse;
}

void IPC_Init()
{
	if (tas_ipc.value != 0) {
		Start_IPC();
	}
	server.AddCallback("cmd", Cmd_Callback, false);
	server.AddCallback("response", Cmd_Callback, true);
	server.AddPrintFunc(IPC_Print);
}

void IPC_Block_For_Response()
{
	int timeout = static_cast<int>(bound(1, tas_ipc_timeout.value, MAX_TIMEOUT));
	server.BlockForMessages("response", timeout);
}

bool IPC_Active()
{
	return ipc::Winsock_Initialized() && server.ClientConnected();
}

void IPC_Loop()
{
	if (ipc::Winsock_Initialized()) {
		server.Loop();
	}
}

void IPC_Send(const nlohmann::json& msg)
{
	if (IPC_Active()) {
		server.SendMsg(msg);
	}
}