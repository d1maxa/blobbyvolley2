/*=============================================================================
Blobby Volley 2
Copyright (C) 2006 Jonathan Sieber (jonathan_sieber@yahoo.de)
Copyright (C) 2006 Daniel Knobe (daniel-knobe@web.de)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
=============================================================================*/

/* header include */
#include "NetworkGame.h"

/* includes */
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <cassert>

#include "raknet/RakServer.h"
#include "raknet/BitStream.h"
#include "raknet/GetTime.h"


#include "NetworkMessage.h"
#include "replays/ReplayRecorder.h"
#include "FileRead.h"
#include "FileSystem.h"
#include "GenericIO.h"
#include "MatchEvents.h"
#include "PhysicWorld.h"
#include "NetworkPlayer.h"
#include "InputSource.h"

extern int SWLS_GameSteps;

/* implementation */

NetworkGame::NetworkGame(RakServer& server, 
		std::shared_ptr<NetworkPlayer> players[MAX_PLAYERS],
		bool playerEnabled[MAX_PLAYERS],
		bool switchedSide[MAX_PLAYERS],
		std::string rules, int scoreToWin, float speed) :
	mServer(server),	
	mSpeedController(speed),
	mMatch(new DuelMatch(false, rules, playerEnabled, scoreToWin)),	
	mRecorder(new ReplayRecorder()),
	mGameValid(true)
{
	PlayerIdentity playersIds[MAX_PLAYERS];
	std::string playerNames[MAX_PLAYERS];
	Color playerColors[MAX_PLAYERS];

	int playerEnabledBit = 0;
	int swappedPlayerEnabledBit = 0;

	for (int i = 0; i < MAX_PLAYERS; ++i)
	{			
		// check that player don't have an active game
		if(playerEnabled[i])
		{
			mSwitchedSide[i] = switchedSide[i];

			if (players[i]->getGame())
				BOOST_THROW_EXCEPTION(std::runtime_error("Trying to start a game with player already in another game!"));				

			playersIds[i] = players[i]->getIdentity();

			mInputs[i] = std::make_shared<InputSource>();
			mMatch->setInputSource(PlayerSide(i), mInputs[i]);

			mLastTime[i] = -1;
			mPlayers[i] = players[i]->getID();

			playerNames[i] = players[i]->getName();
			playerColors[i] = players[i]->getColor();

			mRulesSent[i] = false;
			mPlayerNames[i] = playerNames[i];

			playerEnabledBit |= 1 << i;
			swappedPlayerEnabledBit |= 1 << getSwappedPlayerIndex(PlayerSide(i));
		}		
	}

	mMatch->setPlayers(playersIds);			

	mRecorder->setPlayerEnabled(playerEnabled);
	mRecorder->setPlayerNames(playerNames);
	mRecorder->setPlayerColors(playerColors);
	mRecorder->setGameSpeed(mSpeedController.getGameSpeed());
	mRecorder->setGameRules(rules);

	// read rulesfile into a string
	int checksum = 0;
	mRulesLength = 0;	

	rules = FileRead::makeLuaFilename( rules );
	FileRead file(std::string("rules/") + rules);
	checksum = file.calcChecksum(0);
	mRulesLength = file.length();
	mRulesString = file.readRawBytes(mRulesLength);

	// writing rules checksum
	RakNet::BitStream stream;
		
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(playerEnabled[i])
		{
			stream.Write((unsigned char)ID_RULES_CHECKSUM);
			stream.Write(checksum);
			stream.Write(mMatch->getScoreToWin());
			if (mSwitchedSide[i])
			{
				stream.Write((unsigned char)getSwappedPlayerIndex(PlayerSide(i)));
				stream.Write(swappedPlayerEnabledBit);
			}
			else
			{
				stream.Write((unsigned char)i);
				stream.Write(playerEnabledBit);
			}

			mServer.Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, mPlayers[i], false);
			stream.Reset();
		}
	}
	
	// game loop
	mGameThread = std::thread(
		[this]()
		{
			while(mGameValid)
			{
				processPackets();
				step();
				SWLS_GameSteps++;
				mSpeedController.update();
			}
		});
}

NetworkGame::~NetworkGame()
{
	mGameValid = false;
	mGameThread.join();
}

void NetworkGame::injectPacket(const packet_ptr& packet)
{
	std::lock_guard<std::mutex> lock(mPacketQueueMutex);
	mPacketQueue.push_back(packet);
}

void NetworkGame::broadcastBitstream(const RakNet::BitStream& stream, const RakNet::BitStream& switchedstream)
{
	// checks that stream and switchedstream don't have the same content.
	// this is a common mistake that arises from constructs like:
	//		BitStream stream
	//		... fill common data into stream
	//		BitStream switchedstream
	//		.. fill data depending on side in both streams
	//		broadcastBistream(stream, switchedstream)
	//
	//	here, the internal data of switchedstream is the same as stream so all
	//	changes made with switchedstream are done with stream alike. this was not
	//  the intention of this construct so it should be caught by this assertion.
	/// NEVER USE THIS FUNCTION LIKE broadcastBitstream(str, str), use, broadcastBitstream(str) instead
	/// this function is intended for sending two different streams to the two clients

	assert( &stream != &switchedstream );
	assert( stream.GetData() != switchedstream.GetData() );
	
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(mMatch->getPlayerEnabled(PlayerSide(i)))
		{
			mServer.Send(mSwitchedSide[i]? &switchedstream : &stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, mPlayers[i], false);
		}
	}
}

void NetworkGame::broadcastBitstream(const RakNet::BitStream& stream)
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(mMatch->getPlayerEnabled(PlayerSide(i)))
			mServer.Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, mPlayers[i], false);
	}	
}

void NetworkGame::processPackets()
{
	while (!mPacketQueue.empty())
	{
		packet_ptr packet;
		{
			std::lock_guard<std::mutex> lock(mPacketQueueMutex);
			packet = mPacketQueue.front();
			mPacketQueue.pop_front();
		}

		processPacket( packet );
	}
}

PlayerSide NetworkGame::getPlayerSide(PlayerID playerID) const
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (mMatch->getPlayerEnabled(PlayerSide(i)))
		{
			if (mPlayers[i] == playerID)
			{
				return PlayerSide(i);
			}
		}
	}

	assert(0);
}

/// this function processes a single packet received for this network game
void NetworkGame::processPacket( const packet_ptr& packet )
{
	switch(packet->data[0])
	{
		case ID_CONNECTION_LOST:
		case ID_DISCONNECTION_NOTIFICATION:
		{
			auto player = getPlayerSide(packet->playerId);

			RakNet::BitStream stream;
			RakNet::BitStream switchStream;						

			stream.Write((unsigned char)ID_OPPONENT_DISCONNECTED);
			stream.Write((unsigned char)player);

			switchStream.Write((unsigned char)ID_OPPONENT_DISCONNECTED);
			switchStream.Write((unsigned char)getSwappedPlayerIndex(player));
			
			auto noPlayersInTeam = true;
			mMatch->setPlayerEnabled(player, false);			
			for (int i = player % 2; i < MAX_PLAYERS; i+=2)
			{
				if (mMatch->getPlayerEnabled(PlayerSide(i)))
				{
					noPlayersInTeam = false;
					break;
				}
			}
			//cannot continue without players in one team
			if (noPlayersInTeam)
				mGameValid = false;

			stream.Write((unsigned char)mGameValid);
			switchStream.Write((unsigned char)mGameValid);
			broadcastBitstream(stream, switchStream);
			mMatch->pause();

			break;
		}

		case ID_INPUT_UPDATE:
		{
			unsigned time;
			RakNet::BitStream stream(packet->data, packet->length, false);

			// ignore ID_INPUT_UPDATE
			stream.IgnoreBytes(1);
			stream.Read(time);
			PlayerInputAbs newInput(stream);

			auto player = getPlayerSide(packet->playerId);
			if (mSwitchedSide[player])
				newInput.swapSides();
			newInput.setPlayer(player);
			mInputs[player]->setInput(newInput);
			mLastTime[player] = time;					

			break;
		}

		case ID_PAUSE:
		{
			auto player = getPlayerSide(packet->playerId);
			RakNet::BitStream stream;
			RakNet::BitStream switchStream;

			stream.Write((unsigned char)ID_PAUSE);
			stream.Write((unsigned char)player);

			switchStream.Write((unsigned char)ID_PAUSE);
			switchStream.Write((unsigned char)getSwappedPlayerIndex(player));

			broadcastBitstream(stream, switchStream);
			mMatch->pause();

			for (int i = 0; i < MAX_PLAYERS; ++i)
			{
				mPaused[i] = true;
			}
			break;
		}

		case ID_UNPAUSE:
		{
			auto player = getPlayerSide(packet->playerId);						
			RakNet::BitStream stream;
			RakNet::BitStream switchStream;

			stream.Write((unsigned char)ID_UNPAUSE);
			stream.Write((unsigned char)player);			

			switchStream.Write((unsigned char)ID_UNPAUSE);
			switchStream.Write((unsigned char)getSwappedPlayerIndex(player));

			mPaused[player] = false;
			auto unpaused = isGameUnpaused();
			
			stream.Write((unsigned char)unpaused);
			switchStream.Write((unsigned char)unpaused);

			broadcastBitstream(stream, switchStream);
						
			if (unpaused)
				mMatch->unpause();
			
			break;
		}

		case ID_CHAT_MESSAGE:
		{
			RakNet::BitStream stream(packet->data, packet->length, false);

			stream.IgnoreBytes(1); // ID_CHAT_MESSAGE
			char message[MAX_MESSAGE_SIZE];
			/// \todo we need to acertain that this package contains at least 31 bytes!
			///			otherwise, we send just uninitialized memory to the client
			///			thats no real security problem but i think we should address
			///			this nonetheless
			stream.Read(message, sizeof(message));

			auto player = getPlayerSide(packet->playerId);
			RakNet::BitStream stream2;
			RakNet::BitStream switchStream;

			stream2.Write((unsigned char)ID_CHAT_MESSAGE);
			stream2.Write((unsigned char)player);
			stream2.Write(message, sizeof(message));

			switchStream.Write((unsigned char)ID_CHAT_MESSAGE);
			switchStream.Write((unsigned char)getSwappedPlayerIndex(player));
			switchStream.Write(message, sizeof(message));

			for (int i = 0; i < MAX_PLAYERS; ++i)
			{
				if(mMatch->getPlayerEnabled(PlayerSide(i)) && i != player)
				{
					mServer.Send(mSwitchedSide[i]? &switchStream : &stream2, LOW_PRIORITY, RELIABLE_ORDERED, 0, mPlayers[i], false);
				}
			}						

			break;
		}

		case ID_REPLAY:
		{
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_REPLAY);
			std::shared_ptr<GenericOut> out = createGenericWriter( &stream );
			mRecorder->send( out );
			assert( stream.GetData()[0] == ID_REPLAY );

			mServer.Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0, packet->playerId, false);

			break;
		}

		case ID_RULES:
		{
			std::shared_ptr<RakNet::BitStream> stream = std::make_shared<RakNet::BitStream>(packet->data,
					packet->length, false);
			bool needRules;
			stream->IgnoreBytes(1);
			stream->Read(needRules);
							
			mRulesSent[getPlayerSide(packet->playerId)] = true;

			if (needRules)
			{
				stream = std::make_shared<RakNet::BitStream>();
				stream->Write((unsigned char)ID_RULES);
				stream->Write( mRulesLength );
				stream->Write( mRulesString.get(), mRulesLength);
				assert( stream->GetData()[0] == ID_RULES );

				mServer.Send(stream.get(), HIGH_PRIORITY, RELIABLE_ORDERED, 0, packet->playerId, false);
			}

			if (isGameStarted())
			{
				// buffer for playernames
				char name[MAX_NAME_SIZE];
				stream = std::make_shared<RakNet::BitStream>();

				for (int i = 0; i < MAX_PLAYERS; ++i)
				{
					if(mMatch->getPlayerEnabled(PlayerSide(i)))
					{						
						stream->Write((unsigned char)ID_GAME_READY);
						stream->Write((int)mSpeedController.getGameSpeed());

						if(mSwitchedSide[i])
						{
							for (int j = RIGHT_PLAYER; j < MAX_PLAYERS; j+=2)
							{
								if(i != j && mMatch->getPlayerEnabled(PlayerSide(j)))
								{
									strncpy(name, mMatch->getPlayer(PlayerSide(j)).getName().c_str(), sizeof(name));
									stream->Write(name, sizeof(name));
									stream->Write(mMatch->getPlayer(PlayerSide(j)).getStaticColor().toInt());								
								}

								auto opp = j - 1;
								if (i != opp && mMatch->getPlayerEnabled(PlayerSide(opp)))
								{
									strncpy(name, mMatch->getPlayer(PlayerSide(opp)).getName().c_str(), sizeof(name));
									stream->Write(name, sizeof(name));
									stream->Write(mMatch->getPlayer(PlayerSide(opp)).getStaticColor().toInt());
								}
							}							
						}
						else
						{
							for (int j = 0; j < MAX_PLAYERS; ++j)
							{
								if (i != j && mMatch->getPlayerEnabled(PlayerSide(j)))
								{
									strncpy(name, mMatch->getPlayer(PlayerSide(j)).getName().c_str(), sizeof(name));
									stream->Write(name, sizeof(name));
									stream->Write(mMatch->getPlayer(PlayerSide(j)).getStaticColor().toInt());
								}
							}
						}

						mServer.Send(stream.get(), HIGH_PRIORITY, RELIABLE_ORDERED, 0, mPlayers[i], false);
						stream->Reset();
					}
				}				
			}

			break;
		}

		default:
			printf("unknown packet %d received\n",
				int(packet->data[0]));
			break;
	}
}

bool NetworkGame::isGameValid() const
{
	return mGameValid;
}

bool NetworkGame::isGameStarted()
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (mMatch->getPlayerEnabled(PlayerSide(i)) && !mRulesSent[i])
			return false;
	}
	return true;	
}

bool NetworkGame::isGameUnpaused()
{	
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (mMatch->getPlayerEnabled(PlayerSide(i)) && mPaused[i])
			return false;
	}
	return true;
}

void NetworkGame::step()
{
	if (!isGameStarted())
		return;

	// don't record the pauses
	if(!mMatch->isPaused())
	{
		mRecorder->record(mMatch->getState());

		mMatch->step();

		broadcastGameEvents();

		PlayerSide winning = mMatch->winningPlayer();
		if (winning != NO_SIDE)
		{
			// if someone has won, the game is paused
			mMatch->pause();
			mRecorder->record(mMatch->getState());
			mRecorder->finalize( mMatch->getScore(LEFT_SIDE), mMatch->getScore(RIGHT_SIDE) );

			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_WIN_NOTIFICATION);
			stream.Write(winning);

			RakNet::BitStream switchStream;
			switchStream.Write((unsigned char)ID_WIN_NOTIFICATION);
			switchStream.Write(winning == LEFT_SIDE ? RIGHT_SIDE : LEFT_SIDE);

			broadcastBitstream(stream, switchStream);
		}

		broadcastPhysicState(mMatch->getState());
	}
}

void NetworkGame::broadcastPhysicState(const DuelMatchState& state) const
{
	auto ms = state;	// modifiable copy
	auto swappedMs = state; //swapped copy
	swappedMs.swapSides();

	RakNet::BitStream stream;
	std::shared_ptr<GenericOut> out;
		
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(mMatch->getPlayerEnabled(PlayerSide(i)))
		{
			stream.Write((unsigned char)ID_GAME_UPDATE);
			stream.Write(mLastTime[i]);

			/// \todo this required dynamic memory allocation! not good!
			out = createGenericWriter(&stream);			

			out->generic<DuelMatchState> (mSwitchedSide[i] ? swappedMs : ms);
			mServer.Send(&stream, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 0, mPlayers[i], false);

			// reset state and stream
			stream.Reset();
		}
	}	
}

PlayerSide NetworkGame::getSwappedPlayerIndex(PlayerSide index) const
{
	PlayerSide player;
	if (index % 2)
		player = PlayerSide(index - 1);
	else
		player = PlayerSide(index + 1);

	return player;
}

// helper function that writes a single event to bit stream in a space efficient way.
void NetworkGame::writeEventToStream(RakNet::BitStream& stream, MatchEvent e, bool switchSides ) const
{
	stream.Write((unsigned char)e.event);
	if (switchSides)	
		stream.Write((unsigned char)getSwappedPlayerIndex(e.side));	
	else
		stream.Write((unsigned char)e.side);
	if( e.event == MatchEvent::BALL_HIT_BLOB )
		stream.Write( e.intensity );
}

void NetworkGame::broadcastGameEvents() const
{
	RakNet::BitStream stream;

	auto events = mMatch->getEvents();
	// send the events
	if( events.empty() )
		return;

	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(mMatch->getPlayerEnabled(PlayerSide(i)))
		{
			stream.Write((unsigned char)ID_GAME_EVENTS);
			for (auto& e : events)
				writeEventToStream(stream, e, mSwitchedSide[i]);
			stream.Write((char)0);
			mServer.Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0, mPlayers[i], false);

			stream.Reset();
		}
	}	
}

PlayerID NetworkGame::getPlayerID(PlayerSide side) const
{
	assert(side >= LEFT_PLAYER && side < MAX_PLAYERS);
	return mPlayers[side];	
}

std::string NetworkGame::getGameName() const
{
	std::string left;
	std::string right;

	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (mMatch->getPlayerEnabled(PlayerSide(i)))
		{
			if (i % 2)
			{
				if (right.size() > 0)
					right += ", ";
				right += mPlayerNames[i];
			}
			else
			{
				if (left.size() > 0)
					left += ", ";
				left += mPlayerNames[i];
			}
		}
	}

	return left + " vs " + right;
}

