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

#include "GameState.h"

#include <boost/scoped_ptr.hpp>

class ReplayRecorder;

/*! \class LocalGameState
	\brief state for singleplayer game
*/
class LocalGameState : public GameState
{
	public:
		LocalGameState();
		virtual ~LocalGameState();

		// implementation of the State Interface
		virtual void step_impl();
		virtual const char* getStateName() const;

	private:
		bool mWinner;		
		boost::scoped_ptr<ReplayRecorder> mRecorder;
};

