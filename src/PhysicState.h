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

#include "Global.h"
#include "Vector.h"
#include "GenericIOFwd.h"

namespace RakNet
{
	class BitStream;
}

struct PhysicState
{
	Vector2 blobPosition[MAX_PLAYERS];
	Vector2 blobVelocity[MAX_PLAYERS];
	float   blobState[MAX_PLAYERS];
	bool playerEnabled[MAX_PLAYERS];

	Vector2 ballPosition;
	Vector2 ballVelocity;
	float   ballRotation;
	float   ballAngularVelocity;

	void swapSides();
};
