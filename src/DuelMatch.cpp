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
#include "DuelMatch.h"

/* includes */
#include <cassert>

#include "DuelMatchState.h"
#include "MatchEvents.h"
#include "PhysicWorld.h"
#include "GenericIO.h"
#include "GameConstants.h"
#include "InputSource.h"
#include "IUserConfigReader.h"

/* implementation */
DuelMatch::DuelMatch(bool remote, std::string rules, bool playerEnabled[MAX_PLAYERS], int score_to_win) :		
		mPaused(false),
		mRemote(remote)
{
	auto config = IUserConfigReader::createUserConfigReader("config.xml");	
	if (score_to_win == 0)
		score_to_win = config->getInteger("scoretowin");
	mLogic = createGameLogic(rules, this, score_to_win);
	mPhysicWorld.reset(new PhysicWorld(playerEnabled));

	for (int i = 0; i < MAX_PLAYERS; i++)
	{		
		if(playerEnabled[i])
			setInputSource((PlayerSide)i, std::make_shared<InputSource>());
	}

	if (!mRemote)
		mPhysicWorld->setEventCallback([this](const MatchEvent& event) { mEvents.push_back(event); });
}

void DuelMatch::setPlayers(PlayerIdentity players[MAX_PLAYERS])
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{		
		if(getPlayerEnabled(PlayerSide(i)))
			mPlayers[i] = players[i];
	}	
}

void DuelMatch::setInputSource(PlayerSide player, std::shared_ptr<InputSource> input)
{
	if (input)
	{
		mInputSources[player] = input;
		mInputSources[player]->setMatch(this);
	}	
}

void DuelMatch::setPlayerEnabled(PlayerSide player, bool enabled)
{	
	mPhysicWorld->setPlayerEnabled(player, enabled);
}

void DuelMatch::reset()
{
	bool playerEnabled[MAX_PLAYERS];
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		playerEnabled[i] = mPhysicWorld->getPlayerEnabled(PlayerSide(i));
	}

	mPhysicWorld.reset(new PhysicWorld(playerEnabled));
	mLogic = mLogic->clone();
}

DuelMatch::~DuelMatch()
{
}

void DuelMatch::setRules(std::string rulesFile, int score_to_win)
{
	if( score_to_win == 0)
		score_to_win = getScoreToWin();
	mLogic = createGameLogic(rulesFile, this, score_to_win);
}


void DuelMatch::step()
{
	// in pause mode, step does nothing
	if(mPaused)
		return;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (getPlayerEnabled(PlayerSide(i)))
		{
			mTransformedInput[i] = mInputSources[i]->updateInput();

			if (!mRemote)
				mTransformedInput[i] = mLogic->transformInput(mTransformedInput[i], (PlayerSide)i);
		}
	}		

	// do steps in physic an logic
	mLogic->step( getState() );
	mPhysicWorld->step( mTransformedInput, mLogic->isBallValid(), mLogic->isGameRunning());

	// check for all hit events

	// process events
	// process all physics events and relay them to logic
	for( const auto& event : mEvents )
	{
		switch( event.event )
		{
		case MatchEvent::BALL_HIT_BLOB:
			mLogic->onBallHitsPlayer( event.side );
			break;
		case MatchEvent::BALL_HIT_GROUND:
			mLogic->onBallHitsGround( event.side );
			// if not valid, reduce velocity
			if(!mLogic->isBallValid())
				mPhysicWorld->setBallVelocity( mPhysicWorld->getBallVelocity().scale(0.6) );
			break;
		case MatchEvent::BALL_HIT_NET:
			mLogic->onBallHitsNet( event.side );
			break;
		case MatchEvent::BALL_HIT_NET_TOP:
			mLogic->onBallHitsNet( NO_SIDE );
			break;
		case MatchEvent::BALL_HIT_WALL:
			mLogic->onBallHitsWall( event.side );
			break;
		default:
			break;
		}
	}

	auto errorside = mLogic->getLastErrorSide();
	if(errorside != NO_PLAYER)
	{
		mEvents.emplace_back( MatchEvent::PLAYER_ERROR, errorside, 0 );
		mPhysicWorld->setBallVelocity( mPhysicWorld->getBallVelocity().scale(0.6) );
	}

	// if the round is finished, we
	// reset BallDown, reset the World
	// to let the player serve
	// and trigger the EVENT_RESET
	if (!mLogic->isBallValid() && canStartRound(mLogic->getServingPlayer()))
	{
		resetBall( mLogic->getServingPlayer() );
		mLogic->onServe();
		mEvents.emplace_back( MatchEvent::RESET_BALL, NO_SIDE, 0 );
	}

	// reset events
	mLastEvents = mEvents;
	mEvents.clear();
}

void DuelMatch::setScore(int left, int right)
{
	mLogic->setScore(LEFT_SIDE, left);
	mLogic->setScore(RIGHT_SIDE, right);
}

void DuelMatch::pause()
{
	mLogic->onPause();
	mPaused = true;
}

void DuelMatch::unpause()
{
	mLogic->onUnPause();
	mPaused = false;
}

PlayerSide DuelMatch::winningPlayer() const
{
	return mLogic->getWinningPlayer();
}

//todo ???
int DuelMatch::getHitcount(PlayerSide player) const
{
	if (player == LEFT_SIDE)
		return mLogic->getTouches(LEFT_SIDE);
	else if (player == RIGHT_SIDE)
		return mLogic->getTouches(RIGHT_SIDE);
	else
		return 0;
}

int DuelMatch::getScore(PlayerSide player) const
{
	return mLogic->getScore(player);
}

int DuelMatch::getTouches(PlayerSide player) const
{
	return mLogic->getTouches(player);
}

int DuelMatch::getPlayersCountInTeam(PlayerSide player) const
{
	int count = 0;
	for (int i = player % 2; i < MAX_PLAYERS; i+=2)
	{
		if (getPlayerEnabled(PlayerSide(i)))
			count++;
	}
	return count;
}

int DuelMatch::getScoreToWin() const
{
	return mLogic->getScoreToWin();
}

bool DuelMatch::getBallDown() const
{
	return !mLogic->isBallValid();
}

bool DuelMatch::getBallActive() const
{
	return mLogic->isGameRunning();
}

bool DuelMatch::getBlobJump(PlayerSide player) const
{
	return !mPhysicWorld->blobHitGround(player);
}

Vector2 DuelMatch::getBlobPosition(PlayerSide player) const
{
	if (player >= LEFT_PLAYER && player < MAX_PLAYERS)
		return mPhysicWorld->getBlobPosition(player);
	else
		return Vector2(0.0, 0.0);
}

Vector2 DuelMatch::getBlobVelocity(PlayerSide player) const
{
	if (player >= LEFT_PLAYER && player < MAX_PLAYERS)
		return mPhysicWorld->getBlobVelocity(player);
	else
		return Vector2(0.0, 0.0);
}

Vector2 DuelMatch::getBallPosition() const
{
	return mPhysicWorld->getBallPosition();
}

Vector2 DuelMatch::getBallVelocity() const
{
	return mPhysicWorld->getBallVelocity();
}

PlayerSide DuelMatch::getServingPlayer() const
{	// NO_PLAYER hack was moved into ScriptedInpurSource.cpp
	return mLogic->getServingPlayer();
}

void DuelMatch::setState(const DuelMatchState& state)
{
	mPhysicWorld->setState(state.worldState);
	mLogic->setState(state.logicState);
	
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (getPlayerEnabled(PlayerSide(i)))
		{
			mTransformedInput[i] = state.playerInput[i];
			mInputSources[i]->setInput(mTransformedInput[i]);
		}
	}	
}

void DuelMatch::trigger( const MatchEvent& event )
{
	mEvents.push_back( event );
}

DuelMatchState DuelMatch::getState() const
{
	DuelMatchState state;
	state.worldState = mPhysicWorld->getState();
	state.logicState = mLogic->getState();

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if(getPlayerEnabled(PlayerSide(i)))
			state.playerInput[i] = mTransformedInput[i];
	}

	return state;
}

bool DuelMatch::getPlayerEnabled(PlayerSide player) const
{
	return mPhysicWorld->getPlayerEnabled(player);
}

void DuelMatch::setServingPlayer(PlayerSide side)
{
	mLogic->setServingPlayer( side );
	resetBall( side );
	mLogic->onServe( );
}

const Clock& DuelMatch::getClock() const
{
	return mLogic->getClock();
}

Clock& DuelMatch::getClock()
{
	return mLogic->getClock();
}

std::shared_ptr<InputSource> DuelMatch::getInputSource(PlayerSide player) const
{
	return mInputSources[player];
}

void DuelMatch::resetBall(PlayerSide side) const
{
	if (side == LEFT_SIDE)
		mPhysicWorld->setBallPosition( Vector2(200, STANDARD_BALL_HEIGHT) );
	else if (side == RIGHT_SIDE)
		mPhysicWorld->setBallPosition( Vector2(600, STANDARD_BALL_HEIGHT) );
	else
		mPhysicWorld->setBallPosition( Vector2(400, 450) );

	mPhysicWorld->setBallVelocity( Vector2(0, 0) );
	mPhysicWorld->setBallAngularVelocity( (side == RIGHT_PLAYER ? -1 : 1) * STANDARD_BALL_ANGULAR_VELOCITY );
}

bool DuelMatch::canStartRound(PlayerSide servingPlayer) const
{
	Vector2 ballVelocity = mPhysicWorld->getBallVelocity();
	return (mPhysicWorld->blobHitGround(servingPlayer) && ballVelocity.y < 1.5 &&
				ballVelocity.y > -1.5 && mPhysicWorld->getBallPosition().y > 430);
}

PlayerIdentity DuelMatch::getPlayer(PlayerSide player) const
{
	return mPlayers[player];
}

PlayerIdentity& DuelMatch::getPlayer(PlayerSide player)
{
	return mPlayers[player];
}

void DuelMatch::updateEvents()
{
	/// \todo more economical with a swap?
	mLastEvents = mEvents;
	mEvents.clear();
}

