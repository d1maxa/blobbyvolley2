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
#include "InputDevice.h"

/* includes */
#include <algorithm>

#include "RenderManager.h"
#include "InputManager.h"

// ********************************************************************************************************
// 		Interface Definition
// ********************************************************************************************************

/*! \class MouseInputDevice
	\brief Ingame mouse control
*/
class MouseInputDevice : public InputDevice
{
	private:
		PlayerSide mPlayer;
		int mJumpButton;
		int mMarkerX;
		bool mDelay; // The pressed button of the mainmenu must be ignored
		float mSensitivity;

	public:
		virtual ~MouseInputDevice();
		MouseInputDevice(PlayerSide player, int jumpbutton, float sensitivity);
		virtual PlayerInputAbs transferInput();
};

// ********************************************************************************************************
// 		Class Implementation
// ********************************************************************************************************

// -------------------------------------------------------------------------------------------------
//		Creator Function
// -------------------------------------------------------------------------------------------------

InputDevice* createMouseInput(PlayerSide player, int jumpbutton, float s)
{
	float maxFactor = 3;
	// mapping  [0:1]  -> [-1:1] 	: se = (s - 0.5) * 2
	// mapping  [-1:1] -> [1/m, m]
	//		exp(a) = m
	//		exp(-a) = 1/m
	//		exp(0) = 1
	//    => a =  ln(m)
	//	sensitivity = exp(ln(m) * se)
	float se = (s - 0.5) * 2;
	float sensitivity = std::exp(std::log(maxFactor) * se);

	return new MouseInputDevice(player, jumpbutton, sensitivity);
}

// -------------------------------------------------------------------------------------------------
// 		Keyboard Input Device
// -------------------------------------------------------------------------------------------------
MouseInputDevice::MouseInputDevice(PlayerSide player, int jumpbutton, float sensitivity)
	: InputDevice(), mPlayer(player), mJumpButton(jumpbutton), mMarkerX(0), mSensitivity( sensitivity )
{
	if (SDL_GetMouseState(NULL, NULL))
		mDelay = true;
	else
		mDelay = false;

	assert(InputManager::getSingleton()->isMouseCaptured() == false);
	InputManager::getSingleton()->captureMouse( true );
}

MouseInputDevice::~MouseInputDevice()
{
	assert(InputManager::getSingleton()->isMouseCaptured() == true);
	InputManager::getSingleton()->captureMouse( false );
}

PlayerInputAbs MouseInputDevice::transferInput()
{
	PlayerInputAbs input = PlayerInputAbs();

	int deltaX;
	int mouseState = SDL_GetRelativeMouseState(&deltaX, NULL);

	if (mouseState == 0)
		mDelay = false;

	if((mouseState & SDL_BUTTON(mJumpButton)) && !mDelay)
		input.setJump( true );

	const int playerOffset = (mPlayer % 2) ? 600 : 200;
	mMarkerX += deltaX * mSensitivity;
	mMarkerX = std::max(-200, std::min(200, mMarkerX));

	input.setTarget( mMarkerX + playerOffset, mPlayer );

	RenderManager::getSingleton().setMouseMarker(mMarkerX + playerOffset);

	return input;
}
