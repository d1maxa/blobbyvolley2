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
#include "PhysicState.h"

/* includes */
#include "GameConstants.h"
#include "GenericIO.h"

USER_SERIALIZER_IMPLEMENTATION_HELPER(PhysicState)
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		io.number(value.blobPosition[i].x);
		io.number(value.blobPosition[i].y);

		io.number(value.blobVelocity[i].x);
		io.number(value.blobVelocity[i].y);

		io.number(value.blobState[i]);		
	}	

	io.number( value.ballPosition.x );
	io.number( value.ballPosition.y );

	io.number( value.ballVelocity.x );
	io.number( value.ballVelocity.y );

	io.number( value.ballRotation );
	io.number( value.ballAngularVelocity );
}

void PhysicState::swapSides()
{
	for (int i = 0; i < MAX_PLAYERS; i+=2)
	{
		blobPosition[i].x = RIGHT_PLANE - blobPosition[i].x;
		blobPosition[i + 1].x = RIGHT_PLANE - blobPosition[i + 1].x;
		blobVelocity[i].x = -blobVelocity[i].x;
		blobVelocity[i + 1].x = -blobVelocity[i + 1].x;

		std::swap(blobPosition[i], blobPosition[i + 1]);
		std::swap(blobVelocity[i], blobVelocity[i + 1]);
		std::swap(blobState[i], blobState[i + 1]);
	}

	ballPosition.x = RIGHT_PLANE - ballPosition.x;
	ballVelocity.x = -ballVelocity.x;
	ballAngularVelocity = -ballAngularVelocity;
	ballRotation = 2*M_PI - ballRotation;
}
