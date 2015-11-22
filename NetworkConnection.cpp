/*
  NetworkConnection: A class for establishing and utilising a fast network connection between two clients utilising a remote server
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

#include "NetworkConnection.h"

NetworkConnection::NetworkConnection()
{
  netMut = SDL_CreateMutex();
  msgMut = SDL_CreateMutex();
  lastPackID = 0;
  sendSize = 0;
  sendCount = 0;
  connectedToInternetServer = false;
  packet = NULL;
  p2p = false;
  hashInterval = 250;
  startTime = SDL_GetTicks();
  pauseTime = 0;
  pingTime = 0;
  resetTime = 0;
  inSync = true;
  serverURL = "";
}

NetworkConnection::~NetworkConnection()
{
  SDLNet_FreePacket(packet);
  SDLNet_UDP_Close(udpSD);
  SDL_DestroyMutex(msgMut);
  SDL_DestroyMutex(netMut);
}


int NetworkConnection::init()
{
  return SDLNet_Init() + 1;
}

int NetworkConnection::setServerURL(char* url)
{
  serverURL = url;
  return 0;
}

int NetworkConnection::connectToInternetServer()
{
  IPaddress ip;

  if (!connectedToInternetServer)
  {
    // obtain address information for server
    if (SDLNet_ResolveHost(&ip, serverURL, 55777) < 0)
    {
      //printf("SDLNet_ResolveHost: %s\n", SDLNet_GetError());
      return 0;
    }

    //set the packet size to maximum to give it a large buffer
    if (!packet)
      packet = SDLNet_AllocPacket(NET_MAX_PACKET_SIZE);

    if (!packet)
    {
      //printf("SDLNet_AllocPacket: %s\n", SDLNet_GetError());
      return 0;
    }

    serverAddress = ip;
    packet->address = ip;

    // create a socket descriptor listening on port 55777
    if (!udpSD)
      udpSD = SDLNet_UDP_Open(55777);
  }
  else
  {
    packet->address = serverAddress;
    p2p = false;
  }

  // send a "connect" message to the server to start a connection
  char buf[4];
  SDLNet_Write32(message_type_connect, buf);
  memcpy(packet->data, buf, 4);
  packet->len = 4;

  if (!SDLNet_UDP_Send(udpSD, -1, packet))
  {
    //printf("SDLNet_UDP_Send: %s\n", SDLNet_GetError());
    return 0;
  }

  //set up timer for time-outs
  Uint32 startTime = SDL_GetTicks();
  Uint32 lastTime = startTime;
  Uint32 currentTime;

  // listen for confirmation packet from server
  while (!SDLNet_UDP_Recv(udpSD, packet))
  {
    currentTime = SDL_GetTicks();

    //resend packet every 500ms if no confirmation recieved
    if (currentTime > lastTime + 500)
    {
      SDLNet_Write32(message_type_connect, buf);
      memcpy(packet->data, buf, 4);
      packet->len = 4;

      if (!SDLNet_UDP_Send(udpSD, -1, packet))
      {
        //printf("SDLNet_UDP_Send: %s\n", SDLNet_GetError());
        return 0;
      }

      lastTime = currentTime;
    }

    // time-out if not recieved within 10s
    if (currentTime > startTime + 10000)
      return 0;
  }

  //printf("Packet recieved: address - %i", packet->address.host);

  connectedToInternetServer = true;
  netFlag = 0;

  //gThreadNet = SDL_CreateThread(netSendRecUDP, NULL, NULL);

  // set-up the send buffer to start a new message starting with send order
  SDLNet_Write32(sendCount + 1, buf);
  memcpy(sendBuff, buf, 4);
  sendSize = 4;

  return 1;
}

int NetworkConnection::startInternetHost()
{
  threadNet = SDL_CreateThread(netStartHost, NULL, this);
  if (!threadNet)
    return 0;

  return 1;
}

int NetworkConnection::connectToHost()
{
  threadNet = SDL_CreateThread(netConnectToHost, NULL, this);
  if (!threadNet)
    return 0;

  return 1;
}

int NetworkConnection::closeConnection()
{

  for (int i = 0; i < 3; i++)
    sendUdpMessage(message_type_quit, serverAddress);
  
  if (p2p)
  {
    for (int i = 0; i < 3; i++)
      sendUdpMessage(message_type_quit, partnerAddress);    
    p2p = false;
  }

  netFlag = -1;
  SDL_WaitThread(threadNet, NULL);
  threadNet = NULL;

  return 1;
}


void NetworkConnection::addToSendBuf(Uint32 data)
{
  if (sendSize >= NET_MAX_PACKET_SIZE)
    return;

  char buf[4];
  SDLNet_Write32(data, buf);
  memcpy(&sendBuff[sendSize], buf, 4);

  //printf("Message Written: %i", data);

  sendSize += 4;
}

void NetworkConnection::sendUdpPacket()
{
  if (!packet)
    return;
  
  if (packet->maxlen < sendSize)
    return;

  memcpy(packet->data, sendBuff, sendSize);
  packet->len = sendSize;

  // copy send buffer and push it to the sentPackets list
  char* buf2 = new char[sendSize];
  memcpy(buf2, sendBuff, sendSize);
  PacketData pd;
  pd.data = buf2;
  pd.size = sendSize;
  sentPackets.push_front(pd);

  SDL_LockMutex(netMut);

  if (p2p)
    packet->address = partnerAddress;
  else
    packet->address = serverAddress;

  SDL_UnlockMutex(netMut);

  // Reset the send buffer by adding the new packet ID at the start
  char buf[4];
  sendCount++;
  SDLNet_Write32(sendCount + 1, buf);
  memcpy(sendBuff, buf, 4);
  sendSize = 4;
}


// Encodes a float as an int by multiplying it by 100000, thereby losing some precision
// and also limiting the size of floats to be encoded
// the size range should suit most purposes though
// TODO: modify code to be lossless using a float standard
int NetworkConnection::encodeFloat(float f)
{
  if (f > 21474.0 || f < -21474.0)
  {
    //printf("encodeFloat error: number too large");
  }
  return (int)(f*100000.0);
}

Uint32 NetworkConnection::readMessage()
{
  Uint32 msg;

  while (!pullMessage(&msg))
  {
    SDL_Delay(10);
  }

  return msg;
}

bool NetworkConnection::pullMessage(Uint32 *msg)
{


  if (messageQueue.empty())
    return false;

  if (SDL_LockMutex(msgMut) == -1)
  {
    //printf("msgMut lock failed");
    return false;
  }

  *msg = messageQueue.front();
  messageQueue.pop();

  SDL_UnlockMutex(msgMut);

  //printf("Message Read: %i", *msg);
  return true;
}

// Decodes a float encoded by encodeFloat
float NetworkConnection::decodeFloat(int u)
{
  return ((float)u) / 100000.0;
}


void NetworkConnection::updateHash(Uint32 hash)
{
  memmove(&this->hash[1], &this->hash, HASH_NUM - 1);
  this->hash[0] = hash;
}

bool NetworkConnection::playersInSync()
{
  return inSync;
}

void NetworkConnection::pause()
{
  //set pause time to max Uint32 value
  pauseTime = (Uint32)-1;
}

void NetworkConnection::pause(Uint32 time)
{
  pauseTime = SDL_GetTicks() + time;
}

void NetworkConnection::unpause()
{
  pauseTime = 0;
}

bool NetworkConnection::paused()
{
  if (pauseTime)
  {
    if (SDL_GetTicks() < pauseTime)
    {
      return true;
    }
    else
    {
      pauseTime = 0;
    }
  }

  return false;
}

float NetworkConnection::getPingTime()
{
  return pingTime;
}

void NetworkConnection::newGame()
{
  startTime = SDL_GetTicks();
}

// Sends a packet with information on number of packs recieved and missing packs
// Also contains hash state information for sync checking
int NetworkConnection::sendCheckPacket(UDPpacket* packet)
{
  char buf[512];
  SDLNet_Write32(65535, buf);
  SDLNet_Write32(SDL_GetTicks() - startTime, &buf[4]);
  SDLNet_Write32(hash[0], &buf[8]);
  SDLNet_Write32(minPackRcvd, &buf[12]);
  SDLNet_Write32(sendCount, &buf[16]);

  int n = 5;

  // Iterate over missingPackList and write the packet IDs to the check message
  for (std::list<Uint32>::iterator it = missingPackList.begin(); it != missingPackList.end(); ++it)
  {
    SDLNet_Write32(*it, &buf[n * 4]);
    n++;
  }

  if (SDL_LockMutex(netMut) == -1)
    return 0;

  memcpy(packet->data, buf, n * 4);
  packet->len = n * 4;

  if (p2p)
    packet->address = partnerAddress;
  else
    packet->address = serverAddress;

  if (!SDLNet_UDP_Send(udpSD, -1, packet))
  {
    //printf("SDLNet_UDP_Send: %s\n", SDLNet_GetError());
    return 0;
  }

  SDL_UnlockMutex(netMut);

  return 1;
}

// The main function for handling incoming messages
int sendRecUDP(void* data)
{
  NetworkConnection *net = (NetworkConnection*)data;
  char buf[512];
  static int id = 123000;
  int thisId = id++;
  UDPpacket *pack, *packetOut;
  Uint32 minPackRvd = 0;

  pack = SDLNet_AllocPacket(512);
  pack->address = net->packet->address;
  packetOut = SDLNet_AllocPacket(512);
  packetOut->address = net->packet->address;

  bool connected = true;
  Uint32 time = SDL_GetTicks();
  Uint32 lastTime = time;
  Uint32 lastCheck = time;
  Uint32 lastTimeServer = time;

  bool acceptLastPack = false;

  int hashFail = 0;

  Uint32 playerTime = 0;
    
  while (net->netFlag >= 0) // Exits when netFlag is set to less than 0
  {
    Uint32 currentTime = SDL_GetTicks();

    int timeLen;
        
    if (connected && currentTime > lastCheck + 2000)
    {
      // No message has been received in the last 2 seconds
      net->pushEvent(nc_event_connectionLost);
      connected = false;
    }

    if (minPackRvd == net->sendCount)
      timeLen = 500;
    else
      timeLen = 200; // Send checks out faster while waiting for missing packets

    // Send regular check packets
    if (currentTime > lastTime + timeLen)
    {
      lastTime = currentTime;

      net->sendCheckPacket(pack);
    }

    // Send packets to server during peer-to-peer connection to maintain connection
    if (net->p2p)
    {
      if (currentTime > lastTimeServer + 30000)
      {
        lastTimeServer = currentTime;
        net->sendUdpMessage(message_type_ping, net->serverAddress);
      }
    }

    // Handel incoming packets
    if (SDLNet_UDP_Recv(net->udpSD, pack))
    {
      lastCheck = currentTime;

      if (!connected)
      {
        connected = true;
        net->pushEvent(nc_event_reconnected);
      }

      if (pack->len >= 4)
      {
        memcpy(buf, pack->data, pack->len);

        bool check = false;

        for (int i = 0; i < pack->len / 4; i++)
        {
          char u[4];
          memcpy(u, &buf[i * 4], 4);

          if (i == 0)
          {
            Uint32 packID = SDLNet_Read32(u);
            //if (packID != message_type_check)
            //  printf("Packet received id:%i length:%i port:%i\n", packID, pack->len, pack->address.port);

            if (packID == message_type_check)
            {
              check = true;
            }
            else if (packID == message_type_quit)
            {
              net->pushEvent(nc_event_playerQuit);
            }
            else if (packID == message_type_ping)
            {
              if (pack->len == 8)
              {
                memcpy(u, &buf[4], 4);
                int t = SDLNet_Read32(u);
                t = SDL_GetTicks() - net->startTime - SDLNet_Read32(u);

                //printf("T: %u", t);

                i++;
                if (net->pingTime == 0)
                {
                  net->pingTime = t;
                }
                else
                {
                  net->pingTime = (net->pingTime * 15 + t) / 16.0;
                  //printf("Pingtime: %f", net->pingTime);
                }
              }
            }
            else if (packID > 10000 || (packID == net->lastPackID && !acceptLastPack))
              break;
            else if (packID <= net->lastPackID) // if packet is out of order
            {
              if (packID == net->lastPackID)
                acceptLastPack = false;

              int size = net->missingPackList.size();

              net->missingPackList.remove(packID);

              if (size == net->missingPackList.size())
                break;
            }
            else
            {
              if (packID > net->lastPackID + 1) // if packet number is larger than expected
              {
                for (int i = net->lastPackID + 1; i < packID; i++)
                {
                  net->missingPackList.push_front(i);
                }
              }

              lastTime = 0;

              net->lastPackID = packID;
              if (net->missingPackList.empty())
                net->minPackRcvd = packID;
              acceptLastPack = false;
            }

          }
          else if (check) // If it is a check packet
          {
            if (i == 1) // Time packet was sent
            {
              playerTime = SDLNet_Read32(u);

              SDLNet_Write32(message_type_ping, buf);
              SDLNet_Write32(playerTime, &buf[4]);

              memcpy(packetOut->data, buf, 8);

              packetOut->len = 8;

              if (net->p2p)
                packetOut->address = net->partnerAddress;
              else
                packetOut->address = net->serverAddress;

              SDL_LockMutex(net->netMut);
              SDLNet_UDP_Send(net->udpSD, -1, packetOut);
              SDL_UnlockMutex(net->netMut);

            }
            else if (i == 2) // State hash
            {
              int hash1 = SDLNet_Read32(u);

              int n = 0;

              //printf("My Hash = %i    Their Hash = %i", net->hash[0], hash1);

              if (n < HASH_NUM && net->pauseTime == 0)
              {
                bool fail = true;

                // Check against previous hashes for this client
                for (n = 0; n < HASH_NUM; n++)
                {
                  if (net->hash[n] == hash1)
                  {
                    fail = false;
                    break;
                  }
                }

                if (fail)
                {
                  hashFail++;
                  if (hashFail > 3)
                  {
                    net->inSync = false;
                  }
                }
                else
                {
                  hashFail = 0;
                  net->inSync = true;
                }
              }
              else
              {
                hashFail = 0;
              }
            }
            else if (i == 3) // All packets up to this number have been received, so are safe to clear
            {
              minPackRvd = SDLNet_Read32(u);              
            }
            else if (i == 4) // The total number of message packets sent
            {
              Uint32 numPacksSent = SDLNet_Read32(u);
              if (numPacksSent > net->lastPackID)
              {
                for (int i = net->lastPackID + 1; i < numPacksSent + 1; i++)
                {
                  net->missingPackList.push_front((Uint32)i);
                }
                net->lastPackID = numPacksSent;
                acceptLastPack = true;
                lastTime = 0;
              }
            }
            else // The rest are IDs of missing packets
            {
              Uint32 missingID = SDLNet_Read32(u);
              
              //Iterate through sentPackets
              for (std::list<PacketData>::iterator it = net->sentPackets.begin(); it != net->sentPackets.end(); ++it)
              {
                PacketData pd = *it;
                char tmp[4];
                memcpy(tmp, pd.data, 4);
                Uint32 id = SDLNet_Read32(tmp);

                if (id == missingID)
                {
                  // Resend missing packets
                  Uint32 size = pd.size;

                  memcpy(packetOut->data, pd.data, size);

                  packetOut->len = size;

                  if (net->p2p)
                    packetOut->address = net->partnerAddress;
                  else
                    packetOut->address = net->serverAddress;

                  SDL_LockMutex(net->netMut);
                  SDLNet_UDP_Send(net->udpSD, -1, packetOut);
                  SDL_UnlockMutex(net->netMut);
                }

                // Erase packets that have definitely been received
                if (id < minPackRvd)
                {
                  net->sentPackets.erase(it);
                  delete(pd.data);
                }
              }
            }
          }
          else // i != 0 and not a check packet
          {
            //char* data = new char[4];						
            Uint32 data = SDLNet_Read32(u);

            //printf("Msg: %i", SDLNet_Read32(u));

            if (SDLNet_Read32(u) == message_type_newGame)
            {
              net->pushEvent(nc_event_newGame);
            }

            SDL_LockMutex(net->msgMut);

            net->messageQueue.push(data);

            SDL_UnlockMutex(net->msgMut);
          }

        }
      }
    }

  }

  SDLNet_FreePacket(packetOut);
  SDLNet_FreePacket(pack);

  return 1;
}

int NetworkConnection::pushEvent(nc_event message)
{

  SDL_Event event;
  SDL_zero(event);
  event.type = SDL_USEREVENT;
  event.user.code = message;
  while (SDL_PushEvent(&event) == -1)
    SDL_Delay(10);
  return 1;
}


// Sends a packet containing a single Uint32 as a message to the reciever
// Uint32 message - the reference number of the message being sent
// IPaddress receiver - the address of the intended recipient
// Returns 1 if the message was sent and 0 on failure
int NetworkConnection::sendUdpMessage(Uint32 message, IPaddress receiver)
{
  if (SDL_LockMutex(netMut) == -1)
    return 0;

  SDLNet_Write32(message, packet->data);

  packet->len = 4;

  packet->address = receiver;

  if (!SDLNet_UDP_Send(udpSD, -1, packet))
  {
    SDL_UnlockMutex(netMut);
    return 0;
  }
  SDL_UnlockMutex(netMut);
  return 1;
}

int netStartHost(void* data)
{
  NetworkConnection *net = (NetworkConnection*)data;

  if (net->connectToInternetServer() == -1)
  {
    //printf("\nFailed to connect to internet server");
    net->pushEvent(nc_event_connectionFailed);
    return -1;
  }

  net->pushEvent(nc_event_connectedToServer);

  net->packet->address = net->serverAddress;

  char buf[4];
  SDLNet_Write32(message_type_startHost, buf);
  memcpy(net->packet->data, buf, 4);

  if (!SDLNet_UDP_Send(net->udpSD, -1, net->packet))
  {
    //printf("SDLNet_UDP_Send: %s\n", SDLNet_GetError());
    return -1;
  }

  Uint32 startTime = SDL_GetTicks();
  Uint32 lastTime = startTime;
  Uint32 currentTime;

  //printf("\nWaiting for host conformation");

  while (true)
  {
    if (SDLNet_UDP_Recv(net->udpSD, net->packet))
    {
      char* msg[4];
      memcpy(msg, net->packet->data, 4);
      if (SDLNet_Read32(msg) == message_type_startHost)
      {
        break;
      }
    }

    currentTime = SDL_GetTicks();
    if (currentTime > lastTime + 500)
    {
      SDLNet_Write32(message_type_startHost, buf);
      memcpy(net->packet->data, buf, 4);
      net->packet->len = 4;

      if (!SDLNet_UDP_Send(net->udpSD, -1, net->packet))
      {
        //printf("SDLNet_UDP_Send: %s\n", SDLNet_GetError());
        net->pushEvent(nc_event_connectionFailed);
        return -1;
      }

      lastTime = currentTime;
    }

    if (currentTime > startTime + 10000)
    {
      net->pushEvent(nc_event_timeOut);
      return -1;
    }
  }

  //printf("\nHost Confirmed");
    
  net->pushEvent(nc_event_hostWaiting);

  lastTime = SDL_GetTicks();

  net->netFlag = 0;

  while (true)
  {
    if (SDLNet_UDP_Recv(net->udpSD, net->packet))
    {
      char msg[4];
      memcpy(msg, net->packet->data, 4);
      //printf("\nMsg Rvd: %i Len:%i", SDLNet_Read32(msg), net->packet->len);
      if (SDLNet_Read32(msg) == message_type_requestHost && net->packet->len == 10)
      {
        break;
      }
    }
    else
    {
      currentTime = SDL_GetTicks();
      if (currentTime > lastTime + 500)
      {
        SDLNet_Write32(message_type_checkHost, buf);
        memcpy(net->packet->data, buf, 4);
        SDLNet_UDP_Send(net->udpSD, -1, net->packet);
        lastTime = currentTime;
      }
      SDL_Delay(1);
    }

    if (net->netFlag < 0)
    {
      //printf("Cancelling host waiting");
      return 0;
    }
  }

  net->attemptPeerToPeer();

  net->pushEvent(nc_event_foundClient);

  net->isHost = true;

  return sendRecUDP(data);
}

int netConnectToHost(void* data)
{
  NetworkConnection *net = (NetworkConnection*)data;

  if (!net->connectedToInternetServer)
  {
    if (net->connectToInternetServer() == -1)
    {
      //printf("\nFailed to connect to internet server");
      net->pushEvent(nc_event_connectionFailed);
      return -1;
    }
  }

  //printf("\nRequesting Host Connection...");
  net->pushEvent(nc_event_connectedToServer);

  net->packet->address = net->serverAddress;

  char buf[4];
  SDLNet_Write32(message_type_requestHost, buf);
  memcpy(net->packet->data, buf, 4);

  //printf("\nSending request packet");
  if (!SDLNet_UDP_Send(net->udpSD, -1, net->packet))
  {
    //printf("SDLNet_UDP_Send: %s\n", SDLNet_GetError());
    return -1;
  }

  Uint32 startTime = SDL_GetTicks();
  Uint32 lastTime = startTime;
  Uint32 currentTime;

  //printf("\nWaiting for reply...");
  while (true)
  {
    if (SDLNet_UDP_Recv(net->udpSD, net->packet))
    {
      char* msg[4];
      memcpy(msg, net->packet->data, 4);

      //printf("\nMessage recieved ID: %i", SDLNet_Read32(msg));

      if (SDLNet_Read32(msg) == message_type_noHost)
      {
        //printf("\nNo Host Found");
        net->pushEvent(nc_event_noHost);
        return 1;
      }
      else if (SDLNet_Read32(msg) == message_type_foundHost)
      {
        //printf("\nHost Connected");
        net->pushEvent(nc_event_foundHost);
        break;
      }
    }

    currentTime = SDL_GetTicks();
    if (currentTime > lastTime + 500)
    {
      SDLNet_Write32(message_type_requestHost, buf);
      memcpy(net->packet->data, buf, 4);
      net->packet->len = 4;

      if (!SDLNet_UDP_Send(net->udpSD, -1, net->packet))
      {
        //printf("SDLNet_UDP_Send: %s\n", SDLNet_GetError());
        net->pushEvent(nc_event_connectionFailed);
        return -1;
      }

      lastTime = currentTime;
    }

    if (currentTime > startTime + 10000)
    {
      net->pushEvent(nc_event_timeOut);
      return -1;
    }
  }

  net->netFlag = 0;

  net->attemptPeerToPeer();

  net->isHost = false;

  return sendRecUDP(data);
}

// Attempts to form a peer-to-peer connection with another client connected through the server
// To do this we use udp hole punching
int NetworkConnection::attemptPeerToPeer()
{
  char* buf[4];

  if (packet->len < 10)
  {
    //printf("attemptPeerToPeer failed: packet too short");
    //printf("Msg: %i", SDLNet_Read32(packet->data));
    return 0;
  }

  //printf("\nAttempting PeerToPeer\n\n");

  SDL_LockMutex(netMut);

  // get the address of target client from packet data
  packet->address.host = SDLNet_Read32(&packet->data[4]);
  packet->address.port = SDLNet_Read16(&packet->data[8]);

  partnerAddress = packet->address;

  // send a "connect" message to the new address
  SDLNet_Write32(message_type_connect, buf);
  memcpy(packet->data, buf, 4);

  SDLNet_UDP_Send(udpSD, -1, packet);
  SDLNet_UDP_Send(udpSD, -1, packet);

  SDL_UnlockMutex(netMut);

  Uint32 startTime = SDL_GetTicks();
  Uint32 lastTime = startTime;

  while (true)
  {
    SDL_LockMutex(netMut);

    if (SDLNet_UDP_Recv(udpSD, packet))
    {
      Uint32 msg = SDLNet_Read32(packet->data);
      //printf("\nMsg: %i\n\n", msg);

      if (msg == message_type_connect)
      {
        //printf("\nHole Punch Message Recieved!\n\n");
        SDLNet_Write32(message_type_ping, buf);
        memcpy(packet->data, buf, 4);

        partnerAddress = packet->address;

        SDLNet_UDP_Send(udpSD, -1, packet);
        startTime += 1000;
      }
      else if (msg == message_type_ping)
      {
        //printf("\nPeer-to-peer connection established\n\n");
        p2p = true;

        packet->address = serverAddress;

        SDLNet_Write32(message_type_holePunched, buf);
        SDLNet_Write32(message_type_holePunched, buf);
        SDLNet_Write32(message_type_holePunched, buf);

        memcpy(packet->data, buf, 4);
        SDLNet_UDP_Send(udpSD, -1, packet);

        packet->address = partnerAddress;

        SDL_UnlockMutex(netMut);
        return 1;
      }
    }

    Uint32 currentTime = SDL_GetTicks();
    if (currentTime > lastTime + 100)
    {
      lastTime = currentTime;

      SDLNet_Write32(message_type_connect, buf);
      memcpy(packet->data, buf, 4);
      packet->len = 4;
      packet->address = partnerAddress;

      if (!SDLNet_UDP_Send(udpSD, -1, packet))
      {
        //printf("\nNo message sent, peer-to-peer connection failed\n\n");
        p2p = false;
        packet->address = serverAddress;
        SDL_UnlockMutex(netMut);
        return 0;
      }
    }

    if (currentTime > startTime + 1000)
    {
      //printf("\nTime out\n\n");
      //printf("\nPeer-to-peer connection failed\n\n");
      p2p = false;
      packet->address = serverAddress;
      SDL_UnlockMutex(netMut);
      return 0;
    }

    SDL_UnlockMutex(netMut);

    SDL_Delay(1);
  }
}
