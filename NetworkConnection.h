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

/*
  Pairs with a server running "gameServer.cpp" to establish fast network connections between clients for use with online games.

  The connection is maintained using UDP and packet order is not guarenteed to be maintained.
  Packets will all eventually arrive.

  The game server matches hosts and clients as they come and there is currently no mechanism to match with a specified host or client.

  Requires the SDL 2 and SDL_net 2.0 libraries, they can be found at https://www.libsdl.org/ and https://www.libsdl.org/projects/SDL_net/

  Made to be run with a client utilising SDL event handling, SDL_Init() must be run from the client code for it to work.

  This code is currently under developement, use at your own risk.
  */

#pragma once

#include "List.h"
#include "SDL.h"
#include "SDL_net.h"
#include "string.h"
#include <queue>
#include <list>

#define NET_MAX_PACKET_SIZE 512
#define HASH_NUM 5

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

enum nc_event {
  nc_event_connectedToServer,
  nc_event_hostWaiting,
  nc_event_foundClient,
  nc_event_noHost,
  nc_event_foundHost,
  nc_event_connectionFailed,
  nc_event_timeOut,
  nc_event_newGame,
  nc_event_playerQuit,
  nc_event_connectionLost,
  nc_event_reconnected
};

struct PacketData {
  char* data;
  int size;
};

class NetworkConnection
{
public:
  NetworkConnection();
  ~NetworkConnection();


  // Itialising and Starting Connections //

  // Itialises SDL_Net
  // Returns 1 on success, 0 on errors.
  int init();

  // Sets the url of the server
  // Parameters:
  // url - a string representing the target server url
  // Returns 1 on success, 0 on errors.
  int setServerURL(char* url);

  // Attempts to form a connection with the internet server with the url provided by setServerURL
  // Returns 1 on success, 0 on errors.
  int connectToInternetServer();

  // Starts a new thread to open a host connection and waits for a client to connect with.
  // Pushes SDL events to communicate:
  //	  connectedToServer - connection to server established, will now ask for a host position
  //	  connectionFailed  - attempt to connect failed
  //    timeOut           - connection timed out
  //    hostWaiting       - host confirmed on server, waiting on client
  //    foundClient       - will now start a connection with client
  //    returns 1 if thread creation is successful, 0 if it fails
  int startInternetHost();

  // Starts a new thread to check for waiting hosts and connect with one as a client.
  // Pushes SDL events to communicate:
  //	  connectedToServer - connection to server established, will now request a host
  //	  connectionFailed  - attempt to connect failed
  //    timeOut           - connection timed out
  //    noHost            - no host waiting on server
  //    foundHost         - a host has been found and a connection will be started
  //    returns 1 if thread creation is successful, 0 if it fails
  int connectToHost();


  // Closing the Connection

  // Closes the connection
  // Call at the end of each connection
  int closeConnection();


  // Sending Messages

  // Adds data to the send buffer to form a packet to be sent as a message
  // To send floats format the data using encodeFloat
  // Parameters:
  // data - a 32 bit data chunk	
  void addToSendBuf(Uint32 data);

  // Sends the data buffer as a packet to the connected host or client and empties the send buffer
  void sendUdpPacket();

  // Encodes a float to be sent and decoded by the receiver
  // In the current implementation there is a small loss of precision and a max range of +-21474.0
  // Parameters:
  // f - the float to be encoded
  // Returns 1 on success, 0 on errors.
  int encodeFloat(float f);

  // Reading Messages

  // Reads the next 32 bits from the message queue. If the queue is emtpy it will wait until the message arrives
  // The data is removed from the queue
  // Returns: 32 bit data chunk as a Uint32
  Uint32 readMessage();

  // Checks to see if a message is avaiable and copy it to msg if it is, popping the message from the queue
  // Parameters:
  // msg - Uint32 that will be assigned the message data if it is available
  // Returns true if message found, false if not
  bool pullMessage(Uint32 *msg);

  // Decodes a float encoded by encodeFloat
  // Parameters:
  // data - the data to be decoded
  // Returns the decoded float
  float decodeFloat(int data);


  // Game Pausing and Synchronisation

  // Updates the game state hash in order to check if the players are in sync with each other
  // Parameters:
  // hash - hash of the state to be compared
  void updateHash(Uint32 hash);

  // Returns true if the players are in sync with each other, false if not
  // Requires state hashes to be sent with updateHash to function
  bool playersInSync();

  // Sets the state to paused
  // Set when you pause the game, use unpause to stop
  void pause();

  // Pause for the given time
  // Use paused to test whether it is still paused
  // Parameters:
  // time - the length of time in ms that the game is to be paused
  void pause(Uint32 time);

  // Cancels a paused state
  void unpause();

  // Returns true if the state is paused, false if not
  bool paused();

  // Returns the average return time for roundtrip messages in milliseconds
  float getPingTime();

  // call when a new game is started sync timers
  void newGame();

  // True when currently running as host
  bool isHost;

  // Time in ms between hash checks
  int hashInterval;

private:

  //Network connection stuff
  UDPsocket udpSD;
  UDPpacket *packet;
  IPaddress serverAddress;
  IPaddress partnerAddress;

  //threading
  int netFlag;
  SDL_mutex *netMut;
  SDL_mutex *msgMut;
  SDL_Thread *threadNet;

  // Message and packet lists
  std::queue<Uint32> messageQueue;
  std::list<Uint32> missingPackList;
  std::list<PacketData> sentPackets;

  Uint32 lastPackID;
  Uint32 minPackRcvd;

  char sendBuff[NET_MAX_PACKET_SIZE];
  Uint32 sendSize;
  Uint32 sendCount;

  bool connectedToInternetServer;
  bool p2p;

  Uint32 hash[HASH_NUM];
  Uint32 startTime;
  float pingTime;
  Uint32 resetTime;
  bool inSync;
  Uint32 pauseTime;

  char* serverURL;

  int pushEvent(nc_event message);

  int sendUdpMessage(Uint32 message, IPaddress receiver);

  friend int netStartHost(void*);
  friend int netConnectToHost(void*);

  int sendCheckPacket(UDPpacket *packet);
  friend int sendRecUDP(void*);

  int attemptPeerToPeer();
};
