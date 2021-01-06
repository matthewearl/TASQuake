#include "ipc.hpp"

// Stub definitions so that stuff compiles... 
//
// TODO:  Implement this properly

void CloseSocket(int& socket) {
}

static bool DataAvailable(int&socket) {
	return false;
}

ipc::IPCServer::IPCServer()
{
}

void ipc::IPCServer::InitWinsock()
{
}

void ipc::IPCServer::AddPrintFunc(PrintFunc func)
{
}

void ipc::IPCServer::StartListening(const char* port)
{
}

void ipc::IPCServer::CloseConnections()
{
}

void ipc::IPCServer::Loop()
{
}

bool ipc::IPCServer::BlockForMessages(const std::string& type, int timeoutMsec)
{
    return false;
}

void ipc::IPCServer::AddCallback(std::string type, MsgCallback callback, bool blocking)
{
}

void ipc::IPCServer::SendMsg(const nlohmann::json& msg)
{
}

bool ipc::IPCServer::ClientConnected()
{
	return false;
}

ipc::IPCServer::~IPCServer()
{
}

void ipc::IPCServer::ReadMessages()
{
}

void ipc::IPCServer::CheckForConnections()
{
}

void ipc::IPCServer::DispatchMessages()
{
}

void ipc::IPCServer::DispatchMessages(const std::string& type)
{
}

void ipc::Print(const char* msg, ...)
{
}

void ipc::Shutdown_IPC()
{
}

bool ipc::Winsock_Initialized()
{
    return false;
}
