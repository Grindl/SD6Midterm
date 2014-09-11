#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
//keep track of who IT is
Color3b g_itColor;
double g_gameStartTime;

void startGame()
{
	initializeTimeUtility();
	g_gameOver = false;
	g_itColor = Color3b(0,0,0);
	g_gameStartTime = getCurrentTimeSeconds();
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
				g_clients[ii].m_lastReceivedPacketTime = currentTime;
				//only process packets that are from the current game
				if (currentTime > g_gameStartTime)
				{
					g_clients[ii].m_unprocessedPackets.push_back(pk);
				}
			}
		}
		if (newClient)
		{
			std::cout<<"New client connected\n";
			double currentTime = getCurrentTimeSeconds();
			currentClient.m_lastReceivedPacketTime = currentTime;
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

		Vector2f itLocation = Vector2f(0,0);
		//Collect all gameplay packets we need to send to other players
		std::vector<CS6Packet> positionUpdatePackets;
		for (unsigned int i = 0; i < g_clients.size(); i++)
		{
			//check for timeout
			if (g_clients[i].hasTimedOut())
			{
				g_clients.erase(g_clients.begin()+i);
				i--;
			}
			//else process all their pending packets and put their new position in the queue			
			g_clients[i].processUnprocessedPackets();
			positionUpdatePackets.push_back(g_clients[i].m_unit.packForSend());
			//TODO also check to see if they have declared victory and propagate
			//and finally, grab the location of IT
			if (Color3b(g_clients[i].m_unit.m_color) == g_itColor && !g_gameOver)
			{
				itLocation = g_clients[i].m_unit.m_position;
			}
		}

		Color3b victimColor;
		Color3b newItColor;
		for (unsigned int victimIndex = 0; victimIndex < g_clients.size(); victimIndex++)
		{
			//loop through all victims to see if they are touching IT
			victimColor = Color3b(g_clients[victimIndex].m_unit.m_color);
			if (g_clients[victimIndex].m_unit.m_position.distanceSquared(itLocation) < 100.f && !(victimColor == g_itColor) && !g_gameOver)
			{
				std::cout<<"Game Over!\n";
				g_gameOver = true;
				newItColor = victimColor;
				//if so, put in a victory packet
				CS6Packet vicPacket;
				vicPacket.packetType = TYPE_Victory;
				memcpy(vicPacket.playerColorAndID, &g_itColor, sizeof(g_itColor));
				memcpy(vicPacket.data.victorious.taggedPlayerColorAndID, &victimColor, sizeof(victimColor));
				memcpy(vicPacket.data.victorious.winningPlayerColorAndID, &g_itColor, sizeof(g_itColor));
				positionUpdatePackets.push_back(vicPacket);
				//TODO should this be guaranteed delivery?
			}
			
		}

		for (unsigned int i = 0; i < g_clients.size(); i++)
		{
			//TODO Generate any relevant non-gameplay packets
			if (g_gameOver)
			{
				//TODO generate reset packets
				CS6Packet resetPkt;
				resetPkt.packetType = TYPE_GameStart;
				Color3b cleanedColor = Color3b(g_clients[i].m_unit.m_color);
				memcpy(resetPkt.playerColorAndID, &cleanedColor, sizeof(cleanedColor));
				memcpy(resetPkt.data.reset.playerColorAndID, &cleanedColor, sizeof(cleanedColor));
				memcpy(resetPkt.data.reset.itPlayerColorAndID, &newItColor, sizeof(newItColor));
				g_clients[i].m_unit.m_position = Vector2f (rand()%500, rand()%500);
				resetPkt.data.reset.playerXPosition = g_clients[i].m_unit.m_position.x;
				resetPkt.data.reset.playerYPosition = g_clients[i].m_unit.m_position.y;
				g_clients[i].m_nonAckedGuaranteedPackets.push_back(resetPkt);
				
			}
			//put all of the gameplay packets and non-acked guaranteed packets in to a vector to send to the player
			g_clients[i].m_pendingPacketsToSend.insert(g_clients[i].m_pendingPacketsToSend.end(), positionUpdatePackets.begin(), positionUpdatePackets.end());
			g_clients[i].m_pendingPacketsToSend.insert(g_clients[i].m_pendingPacketsToSend.end(), g_clients[i].m_nonAckedGuaranteedPackets.begin(), g_clients[i].m_nonAckedGuaranteedPackets.end());
			//and then send it to them
			g_clients[i].sendAllPendingPackets();
		}

		//if the game has ended, reset the game
		if(g_gameOver)
		{
			g_itColor = newItColor;
			g_gameOver = false;
			g_gameStartTime = getCurrentTimeSeconds() + 2.0; //a two second wait between rounds
		}

		Sleep(50);
	}

	return 0;
}