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
#include "DuelMatchState.h"

/* includes */
#include "raknet/BitStream.h"

#include "GameConstants.h"
#include "GenericIO.h"


/* implementation */
void DuelMatchState::swapSides()
{
	worldState.swapSides();
	logicState.swapSides();

	for (int i = 0; i < MAX_PLAYERS; i+=2)
	{
		std::swap(playerInput[i].left, playerInput[i].right);
		std::swap(playerInput[i + 1].left, playerInput[i + 1].right);
		std::swap(playerInput[i], playerInput[i + 1]);
	}	
}

USER_SERIALIZER_IMPLEMENTATION_HELPER(DuelMatchState)
{
	io.template generic<PhysicState> (value.worldState);
	io.template generic<GameLogicState> (value.logicState);

	// the template keyword is needed here so the compiler knows generic is
	// a template function and does not complain about <>.	
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		io.template generic<PlayerInput> (value.playerInput[i]);
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//  				info function implementation
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Vector2 DuelMatchState::getBlobPosition(PlayerSide player) const
{
	return worldState.blobPosition[player];
}

float DuelMatchState::getBlobState( PlayerSide player ) const
{
	return worldState.blobState[player];
}

Vector2 DuelMatchState::getBlobVelocity(PlayerSide player) const
{
	return worldState.blobVelocity[player];
}

Vector2 DuelMatchState::getBallPosition() const
{
	return worldState.ballPosition;
}

Vector2 DuelMatchState::getBallVelocity() const
{
	return worldState.ballVelocity;
}

float DuelMatchState::getBallRotation() const
{
	return worldState.ballRotation;
}

PlayerSide DuelMatchState::getServingPlayer() const
{
	return logicState.servingPlayer;
}

PlayerSide DuelMatchState::getWinningPlayer() const
{
	return logicState.winningPlayer;
}

bool DuelMatchState::getBallDown() const
{
	return !logicState.isBallValid;
}

bool DuelMatchState::getBallActive() const
{
	return logicState.isGameRunning;
}

int DuelMatchState::getHitcount(PlayerSide player) const
{
	return logicState.hitCount[player];
}

int DuelMatchState::getScore(PlayerSide player) const
{
	assert( player == LEFT_SIDE || player == RIGHT_SIDE);
	if( player == LEFT_SIDE )
		return logicState.leftScore;
	if( player == RIGHT_SIDE )
		return logicState.rightScore;
	// unreachable
	return -1;
}

