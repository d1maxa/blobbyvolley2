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

#pragma once

#include <list>
#include <mutex>
#include <thread>
#include <memory>

#include <boost/shared_array.hpp>

#include "Global.h"
#include "raknet/NetworkTypes.h"
#include "raknet/BitStream.h"
#include "SpeedController.h"
#include "DuelMatch.h"
#include "BlobbyDebug.h"

class RakServer;
class ReplayRecorder;
class NetworkPlayer;

typedef std::list<packet_ptr> PacketQueue;

class NetworkGame : public ObjectCounter<NetworkGame>
{
	public:
		// The given server is used to send messages to the client, received
		// messages have to bo injected manually in this class.
		// The PlayerID parameters are the IDs of the participating players.
		// The IDs are assumed to be on the same side as they are named.
		// If both players want to be on the same side, switchedSide
		// decides which player is switched.
		/// \exception Throws FileLoadException, if the desired rules file could not be loaded
		///	\exception Throws std::runtime_error, if \p leftPlayer or \p rightPlayer are already assigned to a game.
		NetworkGame(RakServer& server,
			std::shared_ptr<NetworkPlayer> players[MAX_PLAYERS],
			bool playerEnabled[MAX_PLAYERS],
			bool switchedSide[MAX_PLAYERS], 
			std::string rules, int scoreToWin, float speed);

		~NetworkGame();

		void injectPacket(const packet_ptr& packet);

		/// It returns whether both clients are still connected.
		bool isGameValid() const;

		// This function processes the queued network packets,
		// makes a physic step, checks the rules and broadcasts
		// the current state and outstanding messages to the clients.
		void step();

		/// This function processes all queued network packets.
		void processPackets();

		// game info
		/// gets network IDs of players
		PlayerID getPlayerID( PlayerSide side ) const;
		/// gets array index of player by PlayerID
		PlayerSide getPlayerSide(PlayerID playerID) const;
		/// get game name
		std::string getGameName() const;

	private:
		void broadcastBitstream(const RakNet::BitStream& stream, const RakNet::BitStream& switchedstream);
		void broadcastBitstream(const RakNet::BitStream& stream);
		void broadcastPhysicState(const DuelMatchState& state) const;
		void broadcastGameEvents() const;
		void writeEventToStream(RakNet::BitStream& stream, MatchEvent e, bool switchSides ) const;
		bool isGameStarted();
		bool isGameUnpaused();

		PlayerSide getSwappedPlayerIndex(PlayerSide index) const;

		// process a single packet
		void processPacket( const packet_ptr& packet );

		RakServer& mServer;
				
		PlayerID mPlayers[MAX_PLAYERS];
		bool mSwitchedSide[MAX_PLAYERS];
		std::string mPlayerNames[MAX_PLAYERS];

		PacketQueue mPacketQueue;
		std::mutex mPacketQueueMutex;

		const std::unique_ptr<DuelMatch> mMatch;
		SpeedController mSpeedController;
			
		std::shared_ptr<InputSource> mInputs[MAX_PLAYERS];
				
		unsigned mLastTime[MAX_PLAYERS];

		std::thread mGameThread;

		const std::unique_ptr<ReplayRecorder> mRecorder;

		bool mGameValid;

		bool mRulesSent[MAX_PLAYERS];		
		bool mPaused[MAX_PLAYERS];
		int mRulesLength;
		boost::shared_array<char> mRulesString;
};

