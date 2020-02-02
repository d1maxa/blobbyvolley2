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
#include "PhysicWorld.h"

/* includes */
#include <limits>
#include <iostream>

#include "raknet/BitStream.h"

#include "GameConstants.h"
#include "MatchEvents.h"

/* implementation */
// Gamefeeling relevant constants:
const float BLOBBY_ANIMATION_SPEED = 0.5;

// helper function for setting FPU precision
inline short set_fpu_single_precision();
void reset_fpu_flags(short flags);

PhysicWorld::PhysicWorld()
	: mBallPosition(Vector2(200, STANDARD_BALL_HEIGHT))
	, mBallRotation(0)
	, mBallAngularVelocity(STANDARD_BALL_ANGULAR_VELOCITY)
	, mLastHitIntensity(0)
	, mCallback([](const MatchEvent& me) {})
{
	mCurrentBlobbyAnimationSpeed[LEFT_PLAYER] = 0.0;
	mCurrentBlobbyAnimationSpeed[RIGHT_PLAYER] = 0.0;

	mBlobState[LEFT_PLAYER] = 0.0;
	mBlobState[RIGHT_PLAYER] = 0.0;

	mBlobPosition[LEFT_PLAYER] = Vector2(200, GROUND_PLANE_HEIGHT);
	mBlobPosition[RIGHT_PLAYER] = Vector2(600, GROUND_PLANE_HEIGHT);

	mPlayerEnabled[LEFT_PLAYER] = true;
	mPlayerEnabled[RIGHT_PLAYER] = true;
}

PhysicWorld::PhysicWorld(bool playerEnabled[MAX_PLAYERS])
	: mBallPosition(Vector2(200, STANDARD_BALL_HEIGHT))
	, mBallRotation(0)
	, mBallAngularVelocity(STANDARD_BALL_ANGULAR_VELOCITY)
	, mLastHitIntensity(0)	
	, mCallback([](const MatchEvent& me) {})
{	
	if (std::rand() % 2)
		mBallPosition.x = 600;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		mPlayerEnabled[i] = playerEnabled[i];

		if (mPlayerEnabled[i])
		{
			mCurrentBlobbyAnimationSpeed[i] = 0.0;
			mBlobState[i] = 0.0;

			mBlobPosition[i] = Vector2(200 + 400 * (i % 2) + 80 * (i / 2) * std::pow(-1, i / 2), GROUND_PLANE_HEIGHT);			
		}
	}
}

PhysicWorld::~PhysicWorld()
{
}

bool PhysicWorld::blobHitGround(PlayerSide player) const
{
	if (player >= LEFT_PLAYER && player < MAX_PLAYERS)
	{
		return (mBlobPosition[player].y >= GROUND_PLANE_HEIGHT);
	}
	else
		return false;
}

bool PhysicWorld::playerTopBallCollision(int player) const
{
	Vector2 blobpos{mBlobPosition[player].x, mBlobPosition[player].y - BLOBBY_UPPER_SPHERE};
	return circleCircleCollision( mBallPosition, BALL_RADIUS,
								blobpos, BLOBBY_UPPER_RADIUS );
}

inline bool PhysicWorld::playerBottomBallCollision(int player) const
{
	Vector2 blobpos{mBlobPosition[player].x, mBlobPosition[player].y + BLOBBY_LOWER_SPHERE};
	return circleCircleCollision( mBallPosition, BALL_RADIUS,
								blobpos, BLOBBY_LOWER_RADIUS );
}

inline bool PhysicWorld::circleCircleCollision(const Vector2& pos1, float rad1, const Vector2& pos2, float rad2)
{
	Vector2 distance = pos1 - pos2;
	float mxdist = rad1 + rad2;
	return distance.lengthSQ() < mxdist * mxdist;
}

bool PhysicWorld::playerTopBottomCollision(int player, int player2) const
{
	Vector2 blobpos{ mBlobPosition[player].x, mBlobPosition[player].y - BLOBBY_UPPER_SPHERE };
	Vector2 blobpos2{ mBlobPosition[player2].x, mBlobPosition[player2].y + BLOBBY_LOWER_SPHERE };
	return circleCircleCollision(blobpos2, BLOBBY_LOWER_RADIUS,
		blobpos, BLOBBY_UPPER_RADIUS);
}

bool PhysicWorld::playerBottomTopCollision(int player, int player2) const
{
	Vector2 blobpos{ mBlobPosition[player].x, mBlobPosition[player].y + BLOBBY_LOWER_SPHERE };
	Vector2 blobpos2{ mBlobPosition[player2].x, mBlobPosition[player2].y - BLOBBY_UPPER_SPHERE };
	return circleCircleCollision(blobpos2, BLOBBY_UPPER_RADIUS,
		blobpos, BLOBBY_LOWER_RADIUS);
}

inline bool PhysicWorld::playerBottomBottomCollision(int player, int player2) const
{
	Vector2 blobpos{ mBlobPosition[player].x, mBlobPosition[player].y + BLOBBY_LOWER_SPHERE };
	Vector2 blobpos2{ mBlobPosition[player2].x, mBlobPosition[player2].y + BLOBBY_LOWER_SPHERE };
	return circleCircleCollision(blobpos2, BLOBBY_LOWER_RADIUS,
		blobpos, BLOBBY_LOWER_RADIUS);
}

float PhysicWorld::getBallRotation() const
{
	return mBallRotation;
}

Vector2 PhysicWorld::getBlobPosition(PlayerSide player) const
{
	return mBlobPosition[player];
}

Vector2 PhysicWorld::getBlobVelocity(PlayerSide player) const
{
	return mBlobVelocity[player];
}

float PhysicWorld::getBlobState(PlayerSide player) const
{
	return mBlobState[player];
}

bool PhysicWorld::getPlayerEnabled(PlayerSide player)
{
	return mPlayerEnabled[player];
}

void PhysicWorld::setPlayerEnabled(PlayerSide player, bool enabled)
{
	mPlayerEnabled[player] = enabled;	
}

// Blobby animation methods
void PhysicWorld::blobbyAnimationStep(PlayerSide player)
{
	if (mBlobState[player] < 0.0)
	{
		mCurrentBlobbyAnimationSpeed[player] = 0;
		mBlobState[player] = 0;
	}

	if (mBlobState[player] >= 4.5)
	{
		mCurrentBlobbyAnimationSpeed[player] = -BLOBBY_ANIMATION_SPEED;
	}

	mBlobState[player] += mCurrentBlobbyAnimationSpeed[player];

	if (mBlobState[player] >= 5)
	{
		mBlobState[player] = 4.99;
	}
}

void PhysicWorld::blobbyStartAnimation(PlayerSide player)
{
	if (mCurrentBlobbyAnimationSpeed[player] == 0)
		mCurrentBlobbyAnimationSpeed[player] = BLOBBY_ANIMATION_SPEED;
}

void PhysicWorld::handleBlob(PlayerSide player, PlayerInput input)
{
	float currentBlobbyGravity = GRAVITATION;

	if (input.up)
	{
		if (blobHitGround(player))
		{
			mBlobVelocity[player].y = BLOBBY_JUMP_ACCELERATION;
			blobbyStartAnimation( player );
		}

		currentBlobbyGravity -= BLOBBY_JUMP_BUFFER;
	}

	if ((input.left || input.right) && blobHitGround(player))
	{
		blobbyStartAnimation(player);
	}

	mBlobVelocity[player].x = (input.right ? BLOBBY_SPEED : 0) -
								(input.left ? BLOBBY_SPEED : 0);

	// compute blobby fall movement (dt = 1)
	// ds = a/2 * dt^2 + v * dt
	mBlobPosition[player] += Vector2(0, 0.5f * currentBlobbyGravity ) + mBlobVelocity[player];
	// dv = a * dt
	mBlobVelocity[player].y += currentBlobbyGravity;

	// Hitting the ground
	if (mBlobPosition[player].y > GROUND_PLANE_HEIGHT)
	{
		if(mBlobVelocity[player].y > 3.5)
		{
			blobbyStartAnimation(player);
		}

		mBlobPosition[player].y = GROUND_PLANE_HEIGHT;
		mBlobVelocity[player].y = 0.0;
	}

	blobbyAnimationStep(player);
}

bool PhysicWorld::handleBlobbyBallCollision(PlayerSide player)
{
	Vector2 circlepos = mBlobPosition[player];
	// check for impact
	if(playerBottomBallCollision(player))
	{
		circlepos.y += BLOBBY_LOWER_SPHERE;
	}
	else if(playerTopBallCollision(player))
	{
		circlepos.y -= BLOBBY_UPPER_SPHERE;
	} else
	{	// no impact!
		return false;
	}

	// ok, if we get here, there actually was a collision

	// calculate hit intensity
	mLastHitIntensity = Vector2(mBallVelocity, mBlobVelocity[player]).length() / 25.0;
	mLastHitIntensity = mLastHitIntensity > 1.0 ? 1.0 : mLastHitIntensity;

	// set ball velocity
	mBallVelocity = -Vector2(mBallPosition, circlepos);
	mBallVelocity = mBallVelocity.normalise();
	mBallVelocity = mBallVelocity.scale(BALL_COLLISION_VELOCITY);
	mBallPosition += mBallVelocity;
	return true;
}

bool PhysicWorld::handleBlobbiesCollision(PlayerSide player, PlayerSide player2)
{
	/*
	Vector2 circlepos = mBlobPosition[player];
	Vector2 circlepos2 = mBlobPosition[player2];
	// check for impact
	if (playerBottomBottomCollision(player, player2))
	{		
		circlepos.y += BLOBBY_LOWER_SPHERE;
		circlepos2.y += BLOBBY_LOWER_SPHERE;		
	}
	else if (playerTopBottomCollision(player, player2))
	{		
		circlepos.y -= BLOBBY_UPPER_SPHERE;
		circlepos2.y += BLOBBY_LOWER_SPHERE;
	}
	else if (playerBottomTopCollision(player, player2))
	{		
		circlepos.y += BLOBBY_LOWER_SPHERE;
		circlepos2.y -= BLOBBY_UPPER_SPHERE;
	}
	else
	{	// no impact!
		return false;
	}*/

	if (!playerBottomBottomCollision(player, player2) &&
		!playerTopBottomCollision(player, player2) &&
		!playerBottomTopCollision(player, player2))
		// no impact!
		return false;

	// ok, if we get here, there actually was a collision

	// calculate hit intensity
	/*
	auto hitIntensity = Vector2(mBlobVelocity[player2], mBlobVelocity[player]).length() / 25.0;	
	//mLastHitIntensity = Vector2(mBallVelocity, mBlobVelocity[player]).length() / 25.0;
	hitIntensity = hitIntensity > 1.0 ? 1.0 : hitIntensity;
	*/

	// set ball velocity
	/*
	mBallVelocity = -Vector2(mBallPosition, circlepos);
	mBallVelocity = mBallVelocity.normalise();
	mBallVelocity = mBallVelocity.scale(BALL_COLLISION_VELOCITY);
	mBallPosition += mBallVelocity;	*/	
	
	mBlobVelocity[player2] = -Vector2(mBlobPosition[player2], mBlobPosition[player]);
	mBlobVelocity[player2] = mBlobVelocity[player2].normalise();
	mBlobVelocity[player2] = mBlobVelocity[player2].scale(BLOBBY_COLLISION_VELOCITY);
		
	mBlobVelocity[player] = -Vector2(mBlobPosition[player], mBlobPosition[player2]);
	mBlobVelocity[player] = mBlobVelocity[player].normalise();
	mBlobVelocity[player] = mBlobVelocity[player].scale(BLOBBY_COLLISION_VELOCITY);

	mBlobPosition[player2] += mBlobVelocity[player2];
	mBlobPosition[player] += mBlobVelocity[player];
	
	return true;
}

void PhysicWorld::step(PlayerInput inputs[MAX_PLAYERS], bool isBallValid, bool isGameRunning)
{
	// Determistic IEEE 754 floating point computations
	short fpf = set_fpu_single_precision();

	// Compute independent actions
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (mPlayerEnabled[i])
			handleBlob((PlayerSide)i, inputs[i]);
	}	

	// Move ball when game is running
	if (isGameRunning)
	{
		// dt = 1 !!
		// move ball ds = a/2 * dt^2 + v * dt
		mBallPosition += Vector2(0, 0.5f * BALL_GRAVITATION) + mBallVelocity;
		// dv = a*dt
		mBallVelocity.y += BALL_GRAVITATION;
	}

	// Collision detection
	if (isBallValid)
	{
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			if (mPlayerEnabled[i] &&
				handleBlobbyBallCollision((PlayerSide)i))
				mCallback(MatchEvent{ MatchEvent::BALL_HIT_BLOB, (PlayerSide)i, mLastHitIntensity });
		}

		for (int i = 0; i < MAX_PLAYERS - 2; i++)
		{
			for (int j = i + 2; j < MAX_PLAYERS; j += 2)
			{
				if (mPlayerEnabled[i] && mPlayerEnabled[j] & handleBlobbiesCollision(PlayerSide(i), PlayerSide(j)))
				{

				}
			}
		}
	}

	handleBallWorldCollisions();	

	// Collision between blobby and the net
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (!mPlayerEnabled[i])
			continue;

		// Collision with the net
		if (i % 2)
		{
			//right side
			if (mBlobPosition[i].x - BLOBBY_LOWER_RADIUS < NET_POSITION_X + NET_RADIUS)
				mBlobPosition[i].x = NET_POSITION_X + NET_RADIUS + BLOBBY_LOWER_RADIUS;
		}
		else 
		{	
			//left side
			if (mBlobPosition[i].x + BLOBBY_LOWER_RADIUS > NET_POSITION_X - NET_RADIUS)
				mBlobPosition[i].x = NET_POSITION_X - NET_RADIUS - BLOBBY_LOWER_RADIUS;
		}		
	}	

	// Collision between blobby and the border
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (!mPlayerEnabled[i])
			continue;

		if (i % 2)
		{
			if (mBlobPosition[i].x > RIGHT_PLANE)
				mBlobPosition[i].x = RIGHT_PLANE;
		}
		else
		{
			if (mBlobPosition[i].x < LEFT_PLANE)
				mBlobPosition[i].x = LEFT_PLANE;
		}
	}	

	// Velocity Integration
	if (!isGameRunning)
		mBallRotation -= mBallAngularVelocity;
	else if (mBallVelocity.x > 0.0)
		mBallRotation += mBallAngularVelocity * (mBallVelocity.length() / 6);
	else
		mBallRotation -= mBallAngularVelocity * (mBallVelocity.length() / 6);

	// Overflow-Protection
	if (mBallRotation <= 0)
		mBallRotation = 6.25 + mBallRotation;
	else if (mBallRotation >= 6.25)
		mBallRotation = mBallRotation - 6.25;


	reset_fpu_flags(fpf);
}

void PhysicWorld::handleBallWorldCollisions()
{
	// Ball to ground Collision
	if (mBallPosition.y + BALL_RADIUS > GROUND_PLANE_HEIGHT_MAX)
	{
		mBallVelocity = mBallVelocity.reflectY();
		mBallVelocity = mBallVelocity.scale(0.95);
		mBallPosition.y = GROUND_PLANE_HEIGHT_MAX - BALL_RADIUS;
		mCallback( MatchEvent{MatchEvent::BALL_HIT_GROUND, mBallPosition.x > NET_POSITION_X ? RIGHT_SIDE : LEFT_SIDE, 0} );
	}

	// Border Collision
	if (mBallPosition.x - BALL_RADIUS <= LEFT_PLANE && mBallVelocity.x < 0.0)
	{
		mBallVelocity = mBallVelocity.reflectX();
		// set the ball's position
		mBallPosition.x = LEFT_PLANE + BALL_RADIUS;
		mCallback( MatchEvent{MatchEvent::BALL_HIT_WALL, LEFT_SIDE, 0} );
	}
	else if (mBallPosition.x + BALL_RADIUS >= RIGHT_PLANE && mBallVelocity.x > 0.0)
	{
		mBallVelocity = mBallVelocity.reflectX();
		// set the ball's position
		mBallPosition.x = RIGHT_PLANE - BALL_RADIUS;
		mCallback( MatchEvent{MatchEvent::BALL_HIT_WALL, RIGHT_SIDE, 0} );
	}
	else if (mBallPosition.y > NET_SPHERE_POSITION &&
			fabs(mBallPosition.x - NET_POSITION_X) < BALL_RADIUS + NET_RADIUS)
	{
		bool right = mBallPosition.x - NET_POSITION_X > 0;
		mBallVelocity = mBallVelocity.reflectX();
		// set the ball's position so that it touches the net
		mBallPosition.x = NET_POSITION_X + (right ? (BALL_RADIUS + NET_RADIUS) : (-BALL_RADIUS - NET_RADIUS));

		mCallback( MatchEvent{MatchEvent::BALL_HIT_NET, right ? RIGHT_SIDE : LEFT_SIDE, 0} );
	}
	else
	{
		// Net Collisions

		float ballNetDistance = Vector2(mBallPosition, Vector2(NET_POSITION_X, NET_SPHERE_POSITION)).length();

		if (ballNetDistance < NET_RADIUS + BALL_RADIUS)
		{
			// calculate
			Vector2 normal = Vector2(mBallPosition,	Vector2(NET_POSITION_X, NET_SPHERE_POSITION)).normalise();

			// normal component of kinetic energy
			float perp_ekin = normal.dotProduct(mBallVelocity);
			perp_ekin *= perp_ekin;
			// parallel component of kinetic energy
			float para_ekin = mBallVelocity.length() * mBallVelocity.length() - perp_ekin;

			// the normal component is damped stronger than the parallel component
			// the values are ~ 0.85 and ca. 0.95, because speed is sqrt(ekin)
			perp_ekin *= 0.7;
			para_ekin *= 0.9;

			float nspeed = sqrt(perp_ekin + para_ekin);

			mBallVelocity = Vector2(mBallVelocity.reflect(normal).normalise().scale(nspeed));

			// pushes the ball out of the net
			mBallPosition = (Vector2(NET_POSITION_X, NET_SPHERE_POSITION) - normal * (NET_RADIUS + BALL_RADIUS));

			mCallback( MatchEvent{MatchEvent::BALL_HIT_NET_TOP, NO_SIDE, 0} );
		}
		// mBallVelocity = mBallVelocity.reflect( Vector2( mBallPosition, Vector2 (NET_POSITION_X, temp) ).normalise()).scale(0.75);
	}
}
Vector2 PhysicWorld::getBallPosition() const
{
	return mBallPosition;
}

void PhysicWorld::setBallPosition( Vector2 newPosition )
{
	/// \todo should we check here if this new position is valid, i.e. not inside walls etc.
	mBallPosition = newPosition;
}


Vector2 PhysicWorld::getBallVelocity() const
{
	return mBallVelocity;
}

void PhysicWorld::setBallVelocity( Vector2 newVelocity )
{
	mBallVelocity = newVelocity;
}

void PhysicWorld::setBallAngularVelocity( float angvel )
{
	mBallAngularVelocity = angvel;
}

PhysicState PhysicWorld::getState() const
{
	PhysicState st;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (mPlayerEnabled[i])
		{
			st.blobPosition[i] = mBlobPosition[i];
			st.blobVelocity[i] = mBlobVelocity[i];
			st.blobState[i] = mBlobState[i];
			st.playerEnabled[i] = mPlayerEnabled[i];
		}
	}	

	st.ballPosition = mBallPosition;
	st.ballVelocity = mBallVelocity;
	st.ballRotation = mBallRotation;
	st.ballAngularVelocity = mBallAngularVelocity;
	return st;
}

void PhysicWorld::setState(const PhysicState& ps)
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (mPlayerEnabled[i])
		{
			mBlobPosition[i] = ps.blobPosition[i];
			mBlobVelocity[i] = ps.blobVelocity[i];
			mBlobState[i] = ps.blobState[i];
			mPlayerEnabled[i] = ps.playerEnabled[i];
		}
	}

	mBallPosition = ps.ballPosition;
	mBallVelocity = ps.ballVelocity;
	mBallRotation = ps.ballRotation;
	mBallAngularVelocity = ps.ballAngularVelocity;
}

void PhysicWorld::setEventCallback( event_callback_fn cb )
{
	mCallback = cb;
}

inline short set_fpu_single_precision()
{
	short fl = 0;
	#if defined(i386) || defined(__x86_64) // We need to set a precision for diverse x86 hardware
	#if defined(__GNUC__)
		volatile short cw;
		asm volatile ("fstcw %0" : "=m"(cw));
		fl = cw;
		cw = cw & 0xfcff;
		asm volatile ("fldcw %0" :: "m"(cw));
	#elif defined(_MSC_VER)
		short cw;
		asm fstcw cw;
		fl = cw;
		cw = cw & 0xfcff;
		asm fldcw cw;
	#endif
	#else
	#warning FPU precision may not conform to IEEE 754
	#endif
	return fl;
}

void reset_fpu_flags(short flags)
{
	#if defined(i386) || defined(__x86_64) // We need to set a precision for diverse x86 hardware
	#if defined(__GNUC__)
		asm volatile ("fldcw %0" :: "m"(flags));
	#elif defined(_MSC_VER)
		asm fldcw flags;
	#endif
	#else
	#warning FPU precision may not conform to IEEE 754
	#endif
}
