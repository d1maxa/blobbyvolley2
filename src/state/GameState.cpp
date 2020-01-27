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
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
=============================================================================*/

/* header include */
#include "GameState.h"

/* includes */
#include "replays/ReplayRecorder.h"
#include "DuelMatch.h"
#include "SoundManager.h"
#include "IMGUI.h"
#include "Blood.h"
#include "MatchEvents.h"
#include "PhysicWorld.h"
#include "FileWrite.h"

/* implementation */


GameState::GameState(DuelMatch* match) : mMatch(match), mSaveReplay(false), mErrorMessage("")
{
}

GameState::~GameState()
{
	// disable game drawing
	RenderManager::getSingleton().drawGame(false);
}

void GameState::presentGame()
{
	// enable game drawing
	RenderManager::getSingleton().drawGame(true);

	RenderManager& rmanager = RenderManager::getSingleton();
	SoundManager& smanager = SoundManager::getSingleton();

	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		const auto player = PlayerSide(i);
		rmanager.setBlob(i, 
			mMatch->getBlobPosition(player),
			mMatch->getWorld().getBlobState(player),
			mMatch->getPlayerEnabled(player));

		if (mMatch->getPlayerEnabled(player))
		{
			if (mMatch->getPlayer(player).getOscillating())
			{
				rmanager.setBlobColor(i, rmanager.getOscillationColor());
			}
			else
			{
				rmanager.setBlobColor(i, mMatch->getPlayer(player).getStaticColor());
			}
		}
	}
	
	rmanager.setBall(mMatch->getBallPosition(), mMatch->getWorld().getBallRotation());

	auto events = mMatch->getEvents( );
	for(const auto& e : events )
	{
		if( e.event == MatchEvent::BALL_HIT_BLOB )
		{
			smanager.playSound("sounds/bums.wav", e.intensity + BALL_HIT_PLAYER_SOUND_VOLUME);
			/// \todo save that position inside the event
			Vector2 hitPos = mMatch->getBallPosition() + (mMatch->getBlobPosition(e.side) - mMatch->getBallPosition()).normalise().scale(31.5);
			BloodManager::getSingleton().spillBlood(hitPos, e.intensity, 0);
		}

		if( e.event == MatchEvent::PLAYER_ERROR )
			smanager.playSound("sounds/pfiff.wav", ROUND_START_SOUND_VOLUME);
	}
}

void GameState::presentGameUI()
{
	auto& imgui = IMGUI::getSingleton();

	// Scores
	char textBuffer[64];
	snprintf(textBuffer, 8, mMatch->getServingPlayer() == LEFT_SIDE ? "%02d!" : "%02d ", mMatch->getScore(LEFT_SIDE));
	imgui.doText(GEN_ID, Vector2(24, 24), textBuffer);
	snprintf(textBuffer, 8, mMatch->getServingPlayer() == RIGHT_SIDE ? "%02d!" : "%02d ", mMatch->getScore(RIGHT_SIDE));
	imgui.doText(GEN_ID, Vector2(800-24, 24), textBuffer, TF_ALIGN_RIGHT);

	// blob name / time textfields
	if (mMatch->getPlayerEnabled(LEFT_PLAYER_2))
	{		
		imgui.doText(GEN_ID, Vector2(12, 530), mMatch->getPlayer(LEFT_PLAYER).getName());
		imgui.doText(GEN_ID, Vector2(12, 560), mMatch->getPlayer(LEFT_PLAYER_2).getName());
	}
	else
	{
		imgui.doText(GEN_ID, Vector2(12, 550), mMatch->getPlayer(LEFT_PLAYER).getName());
	}

	if (mMatch->getPlayerEnabled(RIGHT_PLAYER_2))
	{
		imgui.doText(GEN_ID, Vector2(422, 530), mMatch->getPlayer(RIGHT_PLAYER).getName());
		imgui.doText(GEN_ID, Vector2(422, 560), mMatch->getPlayer(RIGHT_PLAYER_2).getName());
	}
	else
	{
		imgui.doText(GEN_ID, Vector2(788, 550), mMatch->getPlayer(RIGHT_PLAYER).getName(), TF_ALIGN_RIGHT);
	}

	imgui.doText(GEN_ID, Vector2(400, 24), mMatch->getClock().getTimeString(), TF_ALIGN_CENTER);
}


void GameState::displayQueryPrompt(const int height, TextManager::STRING title, const QueryOption& opt1, const QueryOption& opt2, const QueryOption& opt3)
{
	auto& imgui = IMGUI::getSingleton();

	imgui.doOverlay(GEN_ID, Vector2(0, height), Vector2(800, height + 200));
	imgui.doText(GEN_ID, Vector2(400, height + 30), title, TF_ALIGN_CENTER);

	if (imgui.doButton(GEN_ID, Vector2(400 - 60, height + 90), std::get<0>(opt1), TF_ALIGN_RIGHT))
	{
		std::get<1>(opt1)();
	}
	if (imgui.doButton(GEN_ID, Vector2(400 + 60, height + 90), std::get<0>(opt2), TF_ALIGN_LEFT))
	{
		std::get<1>(opt2)();
	}
	if (imgui.doButton(GEN_ID, Vector2(400, height + 150), std::get<0>(opt3), TF_ALIGN_CENTER))
	{
		std::get<1>(opt3)();
	}
}


bool GameState::displaySaveReplayPrompt()
{
	auto& imgui = IMGUI::getSingleton();

	imgui.doCursor();

	imgui.doOverlay(GEN_ID, Vector2(0, 200), Vector2(800, 400));
	imgui.doText(GEN_ID, Vector2(400, 230), TextManager::RP_SAVE_NAME, TF_ALIGN_CENTER);
	static unsigned cpos;
	imgui.doEditbox(GEN_ID, Vector2(400, 285), 18, mFilename, cpos, TF_ALIGN_CENTER);

	bool doSave = false;

	if(imgui.doButton(GEN_ID, Vector2(220, 350), TextManager::LBL_OK))
	{
		if(mFilename != "")
		{
			imgui.resetSelection();
			doSave = true;
		}
	}

	if (imgui.doButton(GEN_ID, Vector2(440, 350), TextManager::LBL_CANCEL))
	{
		mSaveReplay = false;
		imgui.resetSelection();
		doSave = false;
	}
	return doSave;
}

bool GameState::displayErrorMessageBox()
{
	auto& imgui = IMGUI::getSingleton();

	imgui.doCursor();

	imgui.doOverlay(GEN_ID, Vector2(100, 200), Vector2(700, 360));
	size_t split = mErrorMessage.find(':');
	std::string mProblem = mErrorMessage.substr(0, split);
	std::string mInfo = mErrorMessage.substr(split+1);
	imgui.doText(GEN_ID, Vector2(120, 220), mProblem);
	imgui.doText(GEN_ID, Vector2(120, 260), mInfo);
	if(imgui.doButton(GEN_ID, Vector2(330, 320), TextManager::LBL_OK))
	{
		mErrorMessage = "";
		return true;
	}
	return false;
}

bool GameState::displayWinningPlayerScreen(PlayerSide winner)
{
	auto& imgui = IMGUI::getSingleton();

	std::string tmp = mMatch->getPlayer(winner).getName();
	imgui.doOverlay(GEN_ID, Vector2(0, 150), Vector2(800, 450));
	imgui.doImage(GEN_ID, Vector2(190, 250), "gfx/pokal.bmp");
	imgui.doText(GEN_ID, Vector2(500, 210), tmp, TF_ALIGN_CENTER);
	imgui.doText(GEN_ID, Vector2(500, 270), TextManager::GAME_WIN, TF_ALIGN_CENTER);
	imgui.doCursor();

	return false;
}

void GameState::setDefaultReplayName(std::string playerNames[MAX_PLAYERS])
{
	std::string left = playerNames[LEFT_PLAYER];	
	std::string right = playerNames[RIGHT_PLAYER];

	mFilename = left;
	if (mFilename.size() > 7)
		mFilename.resize(7);
	mFilename += " vs ";

	std::string opp = right;
	if (right.size() > 7)
		opp.resize(7);
	mFilename += opp;
	/*
	for (int i = LEFT_PLAYER_2; i < MAX_PLAYERS; ++i)
	{
		if (playerNames[i].size())
		{
			if (i % 2)
				right += " " + playerNames[i];
			else
				left += " " + playerNames[i];
		}		
	}

	mFilename = left + " vs " + right;
	//mFilename += " vs ";
	//mFilename += right;	
	*/
}

void GameState::saveReplay(ReplayRecorder& recorder)
{
	try
	{
		if (mFilename != "")
		{
			std::string repFileName = std::string("replays/") + mFilename + std::string(".bvr");

			std::shared_ptr<FileWrite> savetarget = std::make_shared<FileWrite>(repFileName);
			/// \todo add a check whether we overwrite a file
			recorder.save(savetarget);
			savetarget->close();
			mSaveReplay = false;
		}
	}
	catch( FileLoadException& ex)
	{
		mErrorMessage = std::string("Unable to create file:" + ex.getFileName());
		mSaveReplay = true;	// try again
	}
	catch( FileAlreadyExistsException& ex)
	{
		mErrorMessage = std::string("File already exists!:"+ ex.getFileName());
		mSaveReplay = true;	// try again
	}
	 catch( std::exception& ex)
	{
		mErrorMessage = std::string("Could not save replay: ");
		mSaveReplay = true;	// try again
	}
}
