/*
  GameServer: A program that runs a game server to connect hosts and clients and relay message
  Copyright (C) 2015 Joshua Collins <joshwithguitar@gmail.com>

  This software is provided 'as-is', without any express or implied
  warranty.In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions :

  1. The origin of this software must not be misrepresented; you must not
  claim that you wrote the original software. If you use this software
  in a product, an acknowledgment in the product documentation would be
  appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
  misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
  */

/*
  Run on a server to match clients using the NetworkConnection class.
  Requires the SDL 2 and SDL_net 2.0 libraries, they can be found at:
  https://www.libsdl.org/ and https://www.libsdl.org/projects/SDL_net/
  */

#include <SDL.h>
#include <SDL_net.h>
#include <iostream>
#include <math.h>

enum message_type {
  message_type_ping = 60000,
  message_type_connect,
  message_type_requestHost,
  message_type_startHost,
  message_type_checkHost,
  message_type_foundHost,
  message_type_noHost,
  message_type_holePunched,
  message_type_quit,
  message_type_systemState,
  message_type_newGame,

  message_type_check = 65535
};

enum client_status {
  client_status_inGame,
  client_status_hostWaiting,
  client_status_free,
  client_status_holePunching
};

// A class representing a connected client
class Client
{
public:
  IPaddress ip;
  UDPsocket sd;
  Client *partner;
  UDPpacket *packet;
  Uint32 msgTime;
  int status;

  Client()
  {
    status = client_status_free;
    partner = NULL;
  }

  int sendMessage(Uint32 msg)
  {
    char* buf[4];
    if (!packet)
      return 0;

    SDLNet_Write32(msg, buf);
    memcpy(packet->data, buf, 4);
    packet->address = ip;
    packet->len = 4;
    return SDLNet_UDP_Send(sd, -1, packet);
  }

  int sendPacket(UDPpacket* pkt)
  {
    pkt->address = ip;
    return SDLNet_UDP_Send(sd, -1, pkt);
  }
};

#define MAX_CLIENTS 10000
#define MAX_HOSTS_WAITING 100

int hostsWaiting;
Client* waiting[MAX_HOSTS_WAITING];
UDPpacket *packet;

// Reset a client to a free state
void resetClient(Client *cl)
{
  if (cl->status == client_status_hostWaiting) // if they were waiting as a host remove them from the waiting list
  {
    for (int i; i < hostsWaiting; i++)
    {
      if (cl == waiting[i])
      {
        memmove(waiting + i, waiting + i + 1, sizeof(Client*)*(hostsWaiting - i));
      }
    }
  }

  cl->status = client_status_free;

  if (cl->partner)
  {
    if (cl->partner->partner == cl)
    {
      SDLNet_Write32(message_type_quit, packet->data);      
      packet->len = 4;

      for (int i; i < 3; i++)
        cl->partner->sendPacket(packet);

      cl->partner->status = client_status_free;
      cl->partner->partner = NULL;
    }
    cl->partner = NULL;
  }
}

int main(int argc, char **argv)
{
  IPaddress ip, c1Address, c2Address;
  UDPsocket sd, c1Socket, c2Socket;  
  Client* client[MAX_CLIENTS];
  int clientNum = 0;

  printf("Games Server: (C) Joshua Collins 2015\n");

  if (SDL_Init(SDL_INIT_TIMER) != 0){
    printf("SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }

  if (SDLNet_Init() < 0)
  {
    printf("Failed to intit SDL_net!");
    return 16;
  }

  sd = SDLNet_UDP_Open(55777);

  if (SDLNet_ResolveHost(&ip, NULL, 55777) < 0)
  {
    printf("Error SDLNet_ResolveHost: %s\n", SDLNet_GetError());
    return 4;
  }

  packet = SDLNet_AllocPacket(512);
  packet->address = ip;

  bool quit = false;

  hostsWaiting = 0;
  
  c1Address.port = 0;
  c2Address.port = 0;

  while (!quit)
  {    
    if (SDLNet_UDP_Recv(sd, packet))
    {
      // Packet received
      char buf[4];
      memcpy(buf, packet->data, 4);

      Uint32 packID = SDLNet_Read32(buf);

      if (packID != message_type_check)
      {
        printf("Packet received");
        printf("\tHost: %i ", packet->address.host);
        printf("\tPort: %i ", packet->address.port);
        printf("\tMessage ID: %i\n", packID);
      }

      Client* cl = NULL;

      // Match packet address to existing client
      //TODO: speed up search for larger numbers of clients
      for (int i = 0; i < clientNum; i++)
      {
        //printf("Checking Client %i...\n", client[i]->ip.host);
        //printf("Address: %i  Port: %i\n", client[i]->ip.port);
        if (packet->address.host == client[i]->ip.host && packet->address.port == client[i]->ip.port)
        {
          printf("Packet address matched to client %i\n", i);
          cl = client[i];
        }
      }

      if (packID == message_type_connect)
      {

        if (cl == NULL)
        {
          if (clientNum < MAX_CLIENTS)
          {
            client[clientNum] = new Client;
            cl = client[clientNum];
            clientNum++;
            cl->ip = packet->address;
            cl->sd = sd;
            cl->packet = packet;
            cl->msgTime = SDL_GetTicks();
            printf("\nNew Client Connected address: %i port: %i\n\n", cl->ip.host, cl->ip.port);
            SDLNet_UDP_Send(sd, -1, packet);
          }
          else
            printf("Max clients reached, rejecting new client");
        }
        else
        {
          // If client already exists reset them
          printf("\n!!Resetting client!!\n\n");
          SDLNet_UDP_Send(sd, -1, packet);
          resetClient(cl);
        }
      }
      else
      {
        if (cl == NULL)
          printf("\n!!!Packet address does not match client\n\n");
        else
        {
          if (packID == message_type_quit)
          {
            resetClient(cl);            
          }
          else if (packID == message_type_startHost && cl->status == client_status_free)
          {
            if (hostsWaiting < MAX_HOSTS_WAITING)
            {
              cl->sendMessage(message_type_startHost);
              printf("\nSending host confirmation to client %i\n\n", cl->ip.port);
              waiting[hostsWaiting] = cl;
              cl->status = client_status_hostWaiting;
              hostsWaiting++;
            }
          }
          else if (packID == message_type_checkHost)
          {
            if (cl->partner != NULL) // If parnter assigned but not received
            {
              SDLNet_Write32(message_type_requestHost, buf);
              SDLNet_Write32(cl->partner->ip.host, &buf[4]);
              SDLNet_Write16(cl->partner->ip.port, &buf[8]);
              memcpy(packet->data, buf, 10);
              packet->len = 10;
              cl->sendPacket(packet);
              printf("sending re-confirmation to host\n\n");
            }
          }
          else if (packID == message_type_requestHost)
          {
            if (cl->status = client_status_free)
            {
              Client* host = NULL;

              // Look for a waiting host to match with client
              while (hostsWaiting > 0)
              {
                host = waiting[0];
                hostsWaiting--;
                if (hostsWaiting > 0)
                  memmove(waiting, waiting + 1, sizeof(Client*)*hostsWaiting);
                                
                if (host != NULL && host->status == client_status_hostWaiting)
                {
                  break;
                }
                else
                {
                  host = NULL;
                }
              }

              if (host == NULL)
              {
                cl->sendMessage(message_type_noHost);
                printf("\nSending No Host to client %i\n\n", client);
              }
              else
              {
                char buf[10];

                // Pair client to host
                cl->partner = host;
                host->partner = cl;
                cl->status = client_status_inGame;
                host->status = client_status_inGame;

                // Send confirmation of client to host
                SDLNet_Write32(message_type_requestHost, buf);
                // Send clients address for an attempt at peer-to-peer
                SDLNet_Write32(cl->ip.host, &buf[4]); 
                SDLNet_Write16(cl->ip.port, &buf[8]);
                memcpy(packet->data, buf, 10);
                packet->len = 10;
                host->sendPacket(packet);
                printf("\nSending confirmation to host\n\n");

                // Send confirmation of host to client
                SDLNet_Write32(message_type_foundHost, buf);
                // Send hosts address for an attempt at peer-to-peer
                SDLNet_Write32(cl->partner->ip.host, &buf[4]);
                SDLNet_Write16(cl->partner->ip.port, &buf[8]);
                memcpy(packet->data, buf, 10);
                packet->len = 10;
                cl->sendPacket(packet);
                printf("\nSending found host\n\n");
              }
            }
            else if (cl->partner != NULL) // If parnter assigned but not received
            {
              SDLNet_Write32(message_type_foundHost, buf);
              SDLNet_Write32(cl->partner->ip.host, &buf[4]);
              SDLNet_Write16(cl->partner->ip.port, &buf[8]);
              memcpy(packet->data, buf, 10);
              packet->len = 10;
              cl->sendPacket(packet);
              printf("sending re-confirmation to client\n\n");
            }
          }
          else if (packID < 10000 || packID == message_type_check)
          {
            // Relay packets to partner
            if (cl->partner != NULL)
            {
              cl->partner->sendPacket(packet);
              printf("\nRelaying packet\n\n");
            }
          }
        }
      }
    }
    else
    {
      Uint32 t = SDL_GetTicks();

      for (int i = 0; i < clientNum; i++)
      {
        if (t - client[i]->msgTime > 600000)
        {
          printf("\nDeleting stale client\n\n");
          delete(client[i]);
          memmove(client + i, client + i + 1, sizeof(Client*)*(clientNum - i));
          clientNum--;
        }
      }

      SDL_Delay(1); // Run a delay to limit CPU time
    }
  }

  for (int i = 0; i < clientNum; i++)
  {
    delete(client[i]);
  }
  
  SDLNet_FreePacket(packet);
  SDLNet_Quit();
  SDL_Quit();

  return 0;
}
