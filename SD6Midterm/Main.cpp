#define WIN32_LEAN_AND_MEAN
#include <windows.h>
//#include <gl/gl.h>
//#include <gl/glu.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <time.h>
#include <vector>
#include <map>

#include "Primitives/Color3b.hpp"
#include "Systems/Time Utility.hpp"

#include "RemoteUDPClient.hpp"

#pragma comment(lib, "Ws2_32.lib")


struct sockaddr_in g_serverSockAddr;
SOCKET g_Socket;
std::vector<RemoteUDPClient> g_clients;

bool g_gameOver;

void startGame()
{
	initializeTimeUtility();
	g_gameOver = false;

}

void startServer(const char* port)
{
	WSAData myWSAData;
	int WSAResult;
	g_Socket = INVALID_SOCKET;
	u_long fionbioFlag = 1;

	g_serverSockAddr.sin_family = AF_INET;
	g_serverSockAddr.sin_port = htons((unsigned short)atoi(port));
	g_serverSockAddr.sin_addr.s_addr = INADDR_ANY;

	WSAResult = WSAStartup(MAKEWORD(2,2), &myWSAData);
	g_Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	WSAResult = ioctlsocket(g_Socket, FIONBIO, &fionbioFlag);
	WSAResult = bind(g_Socket, (struct sockaddr*)&g_serverSockAddr, sizeof(g_serverSockAddr));
}

CS6Packet receive()
{
	int WSAResult;
	CS6Packet pk;
	pk.packetType = 0;
	RemoteUDPClient currentClient;
	int recvSize = sizeof(currentClient.m_sockAddr);
	bool newClient = true;

	WSAResult = recvfrom(g_Socket, (char*)&pk, sizeof(pk), 0, (sockaddr*)&(currentClient.m_sockAddr), &recvSize);

	if (WSAResult != -1)
	{
		for (unsigned int ii = 0; ii < g_clients.size(); ii++)
		{
			if (currentClient == g_clients[ii])
			{			
				newClient = false;
				double currentTime = getCurrentTimeSeconds();
				//update the time
				g_clients[ii].m_unprocessedPackets.push_back(pk);
			}
		}
		if (newClient)
		{
			std::cout<<"New client connected\n";
			//update the time
			currentClient.m_unprocessedPackets.push_back(pk);
			g_clients.push_back(currentClient);
		}
	}
	else
	{
		WSAResult = WSAGetLastError();
		int BREAKNOW = 0;
	}

	return pk;
}


int main()
{
	startGame();
	startServer("8080");
	while(true)
	{
		//Receive and process packets while there are pending packets
		CS6Packet currentPacket;
		do 
		{
			currentPacket = receive();
		} while (currentPacket.packetType != 0);

		//Collect all gameplay packets we need to send to other players
		std::vector<CS6Packet> positionUpdatePackets;
		for (unsigned int i = 0; i < g_clients.size(); i++)
		{
			//check for timeout

			//else process all their pending packets and put their new position in the queue			
			g_clients[i].processUnprocessedPackets();
			positionUpdatePackets.push_back(g_clients[i].m_unit.packForSend());
			//also check to see if they have declared victory and propagate
		}

		for (unsigned int i = 0; i < g_clients.size(); i++)
		{
			//Generate any relevant non-gameplay packets
			//put all of the gameplay packets and non-acked guaranteed packets in to a vector to send to the player
			g_clients[i].m_pendingPacketsToSend.insert(g_clients[i].m_pendingPacketsToSend.end(), positionUpdatePackets.begin(), positionUpdatePackets.end());
			//and then send it to them
			g_clients[i].sendAllPendingPackets();
		}

		//if the game has ended, reset the game
		if(g_gameOver)
		{

		}

		Sleep(50);
	}

	return 0;
}