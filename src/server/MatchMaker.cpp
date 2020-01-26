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
#include "MatchMaker.h"

/* includes */
#include <iostream>
#include <algorithm>
#include <cassert>

#include "GenericIO.h"
#include "NetworkMessage.h"
#include "NetworkGame.h"
#include "NetworkPlayer.h"

// - - - - - - - - - - - - - - - - - -
// 			player management
// - - - - - - - - - - - - - - - - - -

unsigned MatchMaker::openGame( PlayerID creator, int speed, int rules, int points )
{
	if(!mAllowNewGames)
	{
		std::cerr << "creation of new games is currently disabled, sorry " << creator << "\n";
		return -1;
	}

	auto plid = mPlayerMap.find(creator);
	if(plid == mPlayerMap.end())
	{
		std::cerr << "Invalid player " << creator << " tried to create a game\n";
		return -1;
	}
	auto create_pl = plid->second;
		
	std::vector<PlayerID> connected = { creator };

	OpenGame newgame{ creator, create_pl->getName() + "'s game" , speed, rules, points, connected, connected, std::vector<PlayerID>(0) };	

	// if creator already has an open game, delete that
	removePlayerFromAllGames( creator );

	// ok, now creator is not in any other game anymore, therefore, we can add the new game
	return addGame( std::move(newgame) );
}

unsigned MatchMaker::addGame( OpenGame game )
{
	// find a free ID counter
	// normally, the loop should only run once, but we try to be on the safe side here
	while(mOpenGames.find(mIDCounter) != mOpenGames.end())
	{
		++mIDCounter;
	};

	mOpenGames[mIDCounter] = game;

	// broadcast presence of new game
	for( auto& player : mPlayerMap )
	{
		sendOpenGameList(player.first);
	}

	broadcastOpenGameList();
	broadcastOpenGameStatus(mIDCounter);
	return mIDCounter;
}

void MatchMaker::removeGame( unsigned id )
{
	auto g = mOpenGames.find(id);
	assert( g != mOpenGames.end() );

	RakNet::BitStream stream;
	stream.Write( (unsigned char)ID_LOBBY );
	stream.Write( (unsigned char)LobbyPacketType::REMOVED_FROM_GAME );
	stream.Write( id );
		
	// get the list of connected players and send them a notification	
	for( auto player : g->second.connected)
	{		
		// send disconnect message to all
		mSendPacket( stream, player );
	}

	// now remove the game itself
	mOpenGames.erase(g);

	broadcastOpenGameList();
}

void MatchMaker::removePlayerFromAllGames( PlayerID player )
{
	// has that player opened a game, if yes, remove
	auto game = std::find_if(mOpenGames.begin(), mOpenGames.end(), [player](const std::pair<unsigned, OpenGame>& g) { return g.second.creator == player; } );
	if( game != mOpenGames.end())
	{
		// this should normally not happen!
		// let us remove the old game
		removeGame( game->first );
	}

	// if the player is currently trying to join another game, remove him/her from there,too
	for( auto& p : mOpenGames )
	{
		for( auto& w : p.second.connected )
		{
			if(	w == player )
			{
				// we break hereafter, so changing the container during iteration is not a problem
				removePlayerFromGame( p.first, w );
				break;
			}
		}
	}
}

void MatchMaker::removePlayerFromGame( unsigned game, PlayerID player )
{
	RakNet::BitStream stream;
	stream.Write( (unsigned char)ID_LOBBY );
	stream.Write( (unsigned char)LobbyPacketType::REMOVED_FROM_GAME );
	stream.Write( game );

	auto g = mOpenGames.find( game );
	assert( g != mOpenGames.end() );

	auto p = std::find(g->second.connected.begin(), g->second.connected.end(), player);
	assert( p != g->second.connected.end() );

	g->second.connected.erase(p);
	
	p = std::find(g->second.firstTeam.begin(), g->second.firstTeam.end(), player);
	if (p != g->second.firstTeam.end())
		g->second.firstTeam.erase(p);
	
	p = std::find(g->second.secondTeam.begin(), g->second.secondTeam.end(), player);
	if (p != g->second.secondTeam.end())
		g->second.secondTeam.erase(p);

	mSendPacket( stream, player );

	broadcastOpenGameStatus(game);
}

void MatchMaker::addPlayer( PlayerID id, std::shared_ptr<NetworkPlayer> player )
{
	assert( mPlayerMap.find(id) == mPlayerMap.end() );
	mPlayerMap[id] = player;

	// greet the player with the list of all games
	sendOpenGameList( id );
}

void MatchMaker::removePlayer( PlayerID id )
{
	removePlayerFromAllGames( id );

	// removing an id that is not in mPlayerMap is a valid use case.
	// It happens when a player enters a game [removed from waiting players] and then disconnects [removed again]
	mPlayerMap.erase( id );
}

void MatchMaker::joinGame(PlayerID player, unsigned gameID)
{
	// remove player from all other games
	removePlayerFromAllGames( player );

	// check that player and game exist
	auto pl = mPlayerMap.find( player );
	if( pl == mPlayerMap.end())
	{
		std::cerr << "player " << player << "does no longer exist but tried to join game " << gameID << "\n";
		return;
	}

	auto g = mOpenGames.find(gameID);
	if( g == mOpenGames.end() )
	{
		std::cerr << "player "<< pl->second->getName() << " [" << player << "] tried to join game " << gameID << " which does not exits (anymore?)\n";
		sendOpenGameList( player ); // send the updated game list to that player
		return;
	}

	// now we can add the player to the game
	g->second.connected.push_back(player);
	if (g->second.firstTeam.size() <= g->second.secondTeam.size())
		g->second.firstTeam.push_back(player);		
	else
		g->second.secondTeam.push_back(player);

	broadcastOpenGameStatus(gameID);
}

void MatchMaker::changePlayerTeam(PlayerID player)
{	
	for (auto& p : mOpenGames)
	{
		if (std::find(p.second.connected.begin(), p.second.connected.end(), player) != p.second.connected.end())
		{
			auto pos = std::find(p.second.firstTeam.begin(), p.second.firstTeam.end(), player);
			if (pos != p.second.firstTeam.end())
			{
				p.second.firstTeam.erase(pos);
				p.second.secondTeam.push_back(player);
			}
			else
			{
				pos = std::find(p.second.secondTeam.begin(), p.second.secondTeam.end(), player);
				if (pos != p.second.secondTeam.end())
					p.second.secondTeam.erase(pos);
				p.second.firstTeam.push_back(player);
			}

			broadcastOpenGameStatus(p.first);

			return;
		}		
	}

	std::cerr << "player " << player << " tried to change a team, but there is no game with him!\n";
}

// start a game
void MatchMaker::startGame(PlayerID host)
{
	// find game of host
	auto game = std::find_if(mOpenGames.begin(), mOpenGames.end(),
							[host](const std::pair<unsigned, OpenGame>& g) { return g.second.creator == host; } );
	if( game == mOpenGames.end() )
	{
		std::cerr << "Trying to start game of player " << host << ", but no such game was found.\n";
		return;
	}

	std::shared_ptr<NetworkPlayer> players[MAX_PLAYERS];
	bool switchSide[MAX_PLAYERS];
	bool playerEnabled[MAX_PLAYERS];

	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		switchSide[i] = false;
		playerEnabled[i] = false;
	}

	//Left side
	auto num = 0;
	for (auto player : game->second.firstTeam)
	{
		auto found = mPlayerMap.find(player);
		assert(found != mPlayerMap.end());
		assert(num < MAX_PLAYERS);
		players[num] = found->second;
		switchSide[num] = found->second->getDesiredSide() == RIGHT_SIDE;
		playerEnabled[num] = true;
		num += 2;		
	}

	//Right side
	num = 1;
	for (auto player : game->second.secondTeam)
	{
		auto found = mPlayerMap.find(player);
		assert(found != mPlayerMap.end());
		assert(num < MAX_PLAYERS);
		players[num] = found->second;
		switchSide[num] = found->second->getDesiredSide() == LEFT_SIDE;
		playerEnabled[num] = true;
		num += 2;
	}
	
	// ok, all tests passed, the request seems valid. we can start the game and remove both players			
	mCreateGame(players, playerEnabled, switchSide,
				mPossibleGameRules.at(game->second.rules).file,
				game->second.points,
				mPossibleGameSpeeds.at(game->second.speed) );

	// remove players from available player list
	for(auto player : game->second.connected)
		removePlayer(player);	
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MatchMaker::receiveLobbyPacket( PlayerID player, RakNet::BitStream& stream )
{
	unsigned char byte;
	auto reader = createGenericReader(&stream);
	reader->byte(byte);
	reader->byte(byte);
	LobbyPacketType type = LobbyPacketType(byte);

	if( type == LobbyPacketType::OPEN_GAME )
	{
		unsigned speed, score, rules;
		reader->uint32(speed);
		reader->uint32(score);
		reader->uint32(rules);

		openGame( player, speed, rules, score);
		return;
	} 
	else if( type == LobbyPacketType::JOIN_GAME )
	{
		unsigned id;
		reader->uint32(id);
		joinGame(player, id);

		return;
	} 
	else if( type == LobbyPacketType::LEAVE_GAME )
	{
		// normally, a player should only be in one game.
		removePlayerFromAllGames( player );

		return;
	}
	else if (type == LobbyPacketType::CHANGE_TEAM)
	{		
		changePlayerTeam(player);
		return;
	}	
	else if ( type == LobbyPacketType::START_GAME )
	{
		// try to set up the game:
		startGame(player);
	}
}

void MatchMaker::sendOpenGameList( PlayerID recipient )
{
	RakNet::BitStream stream;
	stream.Write( (unsigned char)ID_LOBBY );
	stream.Write( (unsigned char)LobbyPacketType::SERVER_STATUS );

	/// \todo pre allocate those for performance. does this matter? probably not
	std::vector<unsigned int> dGameIDs;
	std::vector<std::string> dGameNames;
	std::vector<unsigned char> dGameSpeed;
	std::vector<unsigned char> dGameRules;
	std::vector<unsigned char> dGameScores;

	// put all possible game rules and game speeds into the packet
	auto out = createGenericWriter(&stream);
	out->uint32(mPlayerMap.size());									// waiting player count
	out->generic<std::vector<unsigned int>>( mPossibleGameSpeeds );
	std::vector<std::string> rule_names;
	std::vector<std::string> rule_authors;
	for( const auto& r : mPossibleGameRules)
	{
		rule_names.push_back(r.name);
		rule_authors.push_back(r.author);
	}
	out->generic<std::vector<std::string>>( rule_names );
	out->generic<std::vector<std::string>>( rule_authors );

	// built games vectors
	for( const auto& game : mOpenGames)
	{
		// for now, only allow connection to empty game
		dGameIDs.push_back( game.first );
		dGameNames.push_back( game.second.name );
		dGameSpeed.push_back( game.second.speed );
		dGameRules.push_back( game.second.rules );
		dGameScores.push_back( game.second.points );
	}

	out->generic<std::vector<unsigned int>>( dGameIDs );
	out->generic<std::vector<std::string>>( dGameNames );
	out->generic<std::vector<unsigned char>>( dGameSpeed );
	out->generic<std::vector<unsigned char>>( dGameRules );
	out->generic<std::vector<unsigned char>>( dGameScores );

	// send the packet
	mSendPacket( stream, recipient );
}


void MatchMaker::broadcastOpenGameStatus( unsigned gameID )
{
	auto g = mOpenGames.find(gameID);
	if( g == mOpenGames.end() )
	{
		std::cerr << "invalid game " << gameID << "\n";
		return;
	}

	// send notification packets
	RakNet::BitStream stream;
	stream.Write( (unsigned char)ID_LOBBY );
	stream.Write( (unsigned char)LobbyPacketType::GAME_STATUS );
	auto out = createGenericWriter(&stream);
	out->uint32( gameID );
	out->generic<PlayerID>(g->second.creator);
	out->string(g->second.name);
	out->uint32(g->second.speed);
	out->uint32(g->second.rules);
	out->uint32(g->second.points);

	out->generic<std::vector<PlayerID>>(g->second.firstTeam);
	out->generic<std::vector<PlayerID>>(g->second.secondTeam);
		
	std::vector<std::string> plnames;
	for( auto& pid : g->second.firstTeam )
	{
		auto player = mPlayerMap.find(pid);
		if( player == mPlayerMap.end() )
			plnames.push_back("INVALID!");
		else
		{
			plnames.push_back( player->second->getName() );
		}
	}
	out->generic<std::vector<std::string>>( plnames );

	plnames.clear();
	for (auto& pid : g->second.secondTeam)
	{
		auto player = mPlayerMap.find(pid);
		if (player == mPlayerMap.end())
			plnames.push_back("INVALID!");
		else
		{
			plnames.push_back(player->second->getName());
		}
	}
	out->generic<std::vector<std::string>>(plnames);

	// send to all players	
	for(auto p: g->second.connected)
		mSendPacket(stream, p );
}

void MatchMaker::broadcastOpenGameList()
{
	for(auto& p : mPlayerMap)
	{
		auto player = p.first;
		bool ingame = false;
		for(auto& g : mOpenGames)
		{			
			if(std::find(g.second.connected.begin(), g.second.connected.end(), player) != g.second.connected.end())
			{
				ingame = true;
				break;
			}
		}

		if(!ingame)
		{
			sendOpenGameList(player);
		}
	}
}


// configure settings
void MatchMaker::addGameSpeedOption( int speed )
{
	mPossibleGameSpeeds.push_back( speed );
}

void MatchMaker::addRuleOption( const std::string& file )
{
	auto gamelogic = createGameLogic(file, nullptr, 1);
	/// \todo check rule validity and load author and description
	mPossibleGameRules.emplace_back(Rule{file, gamelogic->getTitle(), gamelogic->getAuthor(), ""});
}

unsigned MatchMaker::getOpenGamesCount() const
{
	return mOpenGames.size();
}

std::vector<unsigned> MatchMaker::getOpenGameIDs() const
{
	std::vector<unsigned> gameids;
	for(const auto& v : mOpenGames )
	{
		gameids.push_back( v.first );
	}
	return gameids;
}

void MatchMaker::setAllowNewGames( bool allow )
{
	mAllowNewGames = allow;
}
