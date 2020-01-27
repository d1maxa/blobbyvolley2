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

/* includes */
#include <algorithm>
#include <iostream>
#include <ctime>

#include <boost/scoped_array.hpp>
#include <boost/algorithm/string/classification.hpp>

#include "raknet/RakClient.h"
#include "raknet/RakServer.h"
#include "raknet/PacketEnumerations.h"
#include "raknet/GetTime.h"

#include "NetworkState.h"
#include "NetworkMessage.h"
#include "TextManager.h"
#include "replays/ReplayRecorder.h"
#include "DuelMatch.h"
#include "IMGUI.h"
#include "SoundManager.h"
#include "LocalInputSource.h"
#include "UserConfig.h"
#include "FileExceptions.h"
#include "GenericIO.h"
#include "FileRead.h"
#include "FileWrite.h"
#include "MatchEvents.h"
#include "SpeedController.h"
#include "server/DedicatedServer.h"
#include "LobbyStates.h"
#include "InputManager.h"
#include "NetworkSearchState.h"

// global variable to save the lag
int CURRENT_NETWORK_LAG = -1;
// this global allows the host game thread to be killed
extern std::atomic<bool> gKillHostThread;

/* implementation */
NetworkGameState::NetworkGameState( std::shared_ptr<RakClient> client, bool playerEnabled[MAX_PLAYERS], PlayerSide player, int rule_checksum, int score_to_win)
	: GameState(new DuelMatch(true, DEFAULT_RULES_FILE, playerEnabled, score_to_win))
	, mNetworkState(WAITING_FOR_OPPONENT)
	, mWaitingForReplay(false)
	, mClient(client)
	, mPlayerIndex(player)
	, mWinningPlayer(NO_PLAYER)
	, mSelectedChatmessage(0)
	, mChatCursorPosition(0)
	, mChattext("")
{
	std::shared_ptr<IUserConfigReader> config = IUserConfigReader::createUserConfigReader("config.xml");
	mOwnSide = (PlayerSide)config->getInteger("network_side");
	mUseRemoteColor = config->getBool("use_remote_color");
	mLocalInput.reset(new LocalInputSource(mOwnSide));
	mLocalInput->setMatch(mMatch.get());

	/// \todo why do we need this here?
	RenderManager::getSingleton().redraw();

	// game is not started until two players are connected
	mMatch->pause();

	PlayerIdentity players[MAX_PLAYERS];
	// load/init players
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{		
		if(playerEnabled[i])
		{
			players[i] = config->loadPlayerIdentity(PlayerSide(i), true);
		}
	}

	if(mOwnSide != player)
	{
		//player is in the same team as mOwnSide, swap them
		auto color = players[mOwnSide].getStaticColor();
		players[mOwnSide].setStaticColor(players[player].getStaticColor());
		players[player].setStaticColor(color);

		auto name = players[mOwnSide].getName();
		players[mOwnSide].setName(players[player].getName());
		players[player].setName(name);
	}

	mMatch->setPlayers(players);

	//mRemotePlayer->setName("");

	// check the rules
	int ourChecksum = 0;
	if (rule_checksum != 0)
	{
		try
		{
			FileRead rulesFile(TEMP_RULES_NAME);
			ourChecksum = rulesFile.calcChecksum(0);
			rulesFile.close();
		}
		catch( FileLoadException& ex )
		{
			// file doesn't exist - nothing to do here
		}
	}

	RakNet::BitStream stream2;
	stream2.Write((unsigned char)ID_RULES);
	stream2.Write(bool(rule_checksum != 0 && rule_checksum != ourChecksum));
	mClient->Send(&stream2, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
}

NetworkGameState::~NetworkGameState()
{
	CURRENT_NETWORK_LAG = -1;
	mClient->Disconnect(50);
}

void NetworkGameState::step_impl()
{
	//process received packet from server
	processPacket();
	// does this generate any problems if we pause at the exact moment an event is set ( i.e. the ball hit sound
	// could be played in a loop)?
	presentGame();
	presentGameUI();
	//process current state
	processState();
}

void NetworkGameState::processPacket()
{
	RenderManager* rmanager = &RenderManager::getSingleton();
	SoundManager* sound = &SoundManager::getSingleton();
	const TextManager* tmanager = TextManager::getSingleton();

	packet_ptr packet;
	while (packet = mClient->Receive())
	{
		switch (packet->data[0])
		{
		case ID_GAME_UPDATE:
		{
			RakNet::BitStream stream(packet->data, packet->length, false);
			stream.IgnoreBytes(1);	//ID_GAME_UPDATE
			unsigned timeBack;
			stream.Read(timeBack);
			CURRENT_NETWORK_LAG = SDL_GetTicks() - timeBack;
			DuelMatchState ms;
			/// \todo this is a performance nightmare: we create a new reader for every packet!
			///			there should be a better way to do that
			std::shared_ptr<GenericIn> in = createGenericReader(&stream);
			in->generic<DuelMatchState> (ms);
			// inject network data into game
			mMatch->setState(ms);
			break;
		}

		case ID_GAME_EVENTS:
		{
			RakNet::BitStream stream(packet->data, packet->length, false);
			stream.IgnoreBytes(1);	//ID_GAME_EVENTS
			//printf("Physic packet received. Time: %d\n", ival);
			// read events
			char event = 0;
			for (stream.Read(event); event != 0; stream.Read(event))
			{
				char side;
				float intensity = -1;
				stream.Read(side);
				if (event == MatchEvent::BALL_HIT_BLOB)
					stream.Read(intensity);
				MatchEvent me{ MatchEvent::EventType(event), (PlayerSide)side, intensity };
				mMatch->trigger(me);
			}
			break;
		}
		case ID_WIN_NOTIFICATION:
		{
			RakNet::BitStream stream(packet->data, packet->length, false);
			stream.IgnoreBytes(1);	//ID_WIN_NOTIFICATION
			stream.Read((int&)mWinningPlayer);

			// last point must not be added anymore, because
			// the score is also simulated local so it is already
			// right. under strange circumstances this need not
			// be true, but then the score is set to the correy value
			// by ID_BALL_RESET

			mNetworkState = PLAYER_WON;
			break;
		}
		case ID_OPPONENT_DISCONNECTED:
		{
			// In this state, a leaving opponent would not be very surprising
			if (mNetworkState != PLAYER_WON)
			{
				unsigned char playerIndex;
				unsigned char continueGame;
				RakNet::BitStream stream(packet->data, packet->length, false);
				stream.IgnoreBytes(1);	//ID_OPPONENT_DISCONNECTED
				stream.Read(playerIndex);
				stream.Read(continueGame);

				if ((bool)continueGame)
				{
					mMatch->setPlayerEnabled(PlayerSide(playerIndex), false);

					mNetworkState = PAUSING;
					mMatch->pause();

					appendChat(PlayerSide(playerIndex), " " + tmanager->getString(TextManager::NET_DISCONNECT), false);
				}
				else
					mNetworkState = OPPONENT_DISCONNECTED;
			}
			break;
		}
		case ID_PAUSE:
			if (mNetworkState == PLAYING || mNetworkState == WAITING_FOR_OPPONENT)
			{
				unsigned char playerIndex;
				RakNet::BitStream stream(packet->data, packet->length, false);
				stream.IgnoreBytes(1);	//ID_PAUSE
				stream.Read(playerIndex);

				mNetworkState = PAUSING;
				mMatch->pause();

				appendChat(PlayerSide(playerIndex), " " + tmanager->getString(TextManager::NET_PAUSED_GAME), false);
			}
			break;
		case ID_UNPAUSE:
		{
			unsigned char playerIndex;
			unsigned char continueGame;
			RakNet::BitStream stream(packet->data, packet->length, false);
			stream.IgnoreBytes(1);	//ID_UNPAUSE
			stream.Read(playerIndex);
			stream.Read(continueGame);

			if (mNetworkState == PAUSING)
			{
				if (mPlayerIndex == playerIndex)
				{
					//this user exits from pause
					SDL_StopTextInput();
					mNetworkState = WAITING_FOR_OPPONENT;
				}
				else
				{
					appendChat(PlayerSide(playerIndex), " " + tmanager->getString(TextManager::NET_WAITING_CONTINUE), false);
				}
			}

			if (continueGame)
			{
				mNetworkState = PLAYING;
				mMatch->unpause();
			}
			break;
		}
		case ID_GAME_READY:
		{
			char charName[MAX_NAME_SIZE];
			RakNet::BitStream stream(packet->data, packet->length, false);

			stream.IgnoreBytes(1);	// ignore ID_GAME_READY

			// read gamespeed
			int speed;
			stream.Read(speed);
			SpeedController::getMainInstance()->setGameSpeed(speed);

			std::string playerNames[MAX_PLAYERS];
			playerNames[mPlayerIndex] = mMatch->getPlayer(PlayerSide(mPlayerIndex)).getName();
			//get other players' names and colors
			for (int i = 0; i < MAX_PLAYERS; ++i)
			{
				if (mMatch->getPlayerEnabled(PlayerSide(i)) && i != mPlayerIndex)
				{
					// read playername
					stream.Read(charName, sizeof(charName));

					// ensures that charName is null terminated
					charName[sizeof(charName) - 1] = '\0';

					// read colors
					int temp;
					stream.Read(temp);
					Color ncolor = temp;

					mMatch->getPlayer(PlayerSide(i)).setName(charName);

					// check whether to use remote player color
					if (mUseRemoteColor)
					{
						mMatch->getPlayer(PlayerSide(i)).setStaticColor(ncolor);						
					}

					playerNames[i] = mMatch->getPlayer(PlayerSide(i)).getName();
				}
			}

			setDefaultReplayName(playerNames);

			// Workarround for SDL-Renderer
			// Hides the GUI when networkgame starts
			rmanager->redraw();

			mNetworkState = PLAYING;
			// start game
			mMatch->unpause();

			// game ready whistle
			sound->playSound("sounds/pfiff.wav", ROUND_START_SOUND_VOLUME);
			break;
		}
		case ID_RULES_CHECKSUM:
		{
			assert(0);
			break;
		}
		case ID_RULES:
		{
			RakNet::BitStream stream(packet->data, packet->length, false);

			stream.IgnoreBytes(1);	// ignore ID_RULES

			int rulesLength;
			stream.Read(rulesLength);
			if (rulesLength)
			{
				boost::shared_array<char>  rulesString(new char[rulesLength + 1]);
				stream.Read(rulesString.get(), rulesLength);
				// null terminate
				rulesString[rulesLength] = 0;
				FileWrite rulesFile("rules/" + TEMP_RULES_NAME);
				rulesFile.write(rulesString.get(), rulesLength);
				rulesFile.close();
				mMatch->setRules(TEMP_RULES_NAME);
			}
			else
			{
				// either old server, or we have to use fallback ruleset
				mMatch->setRules(FALLBACK_RULES_NAME);
			}

			break;
		}
		// status messages we don't care about
		case ID_REMOTE_DISCONNECTION_NOTIFICATION:
		case ID_REMOTE_CONNECTION_LOST:
		case ID_SERVER_STATUS:
		case ID_REMOTE_NEW_INCOMING_CONNECTION:
		case ID_REMOTE_EXISTING_CONNECTION:
			break;
		case ID_DISCONNECTION_NOTIFICATION:
		case ID_CONNECTION_LOST:
			if (mNetworkState != PLAYER_WON)
				mNetworkState = DISCONNECTED;
			break;
		case ID_NO_FREE_INCOMING_CONNECTIONS:
			mNetworkState = SERVER_FULL;
			break;
		case ID_CHAT_MESSAGE:
		{
			RakNet::BitStream stream(packet->data, packet->length, false);
			stream.IgnoreBytes(1);	// ID_CHAT_MESSAGE
			// Insert Message in the log and focus the last element
			unsigned char playerIndex;
			stream.Read(playerIndex);
			char message[MAX_MESSAGE_SIZE];
			stream.Read(message, sizeof(message));
			message[sizeof(message) - 1] = '\0';

			// Insert Message in the log and focus the last element
			appendChat(PlayerSide(playerIndex), ": " + (std::string) message, false);
			sound->playSound("sounds/chat.wav", ROUND_START_SOUND_VOLUME);
			break;
		}
		case ID_REPLAY:
		{
			/// \todo we should take more action if server sends replay
			///		even if not requested!
			if (!mWaitingForReplay)
				break;

			RakNet::BitStream stream(packet->data, packet->length, false);
			stream.IgnoreBytes(1);	// ID_REPLAY

			// read stream into a dummy replay recorder
			std::shared_ptr<GenericIn> reader = createGenericReader(&stream);
			ReplayRecorder dummyRec;
			dummyRec.receive(reader);
			// and save that
			saveReplay(dummyRec);

			// mWaitingForReplay will be set to false even if replay could not be saved because
			// the server won't send it again.
			mWaitingForReplay = false;

			break;
		}

		// we never do anything that should cause such a packet to be received!
		case ID_CONNECTION_REQUEST_ACCEPTED:
		case ID_CONNECTION_ATTEMPT_FAILED:
			assert(0);
			break;

		case ID_BLOBBY_SERVER_PRESENT:
		{
			// this should only be called if we use the stay on server option
			RakNet::BitStream stream(packet->data, packet->length, false);
			stream.IgnoreBytes(1);	//ID_BLOBBY_SERVER_PRESENT
			ServerInfo info(stream, mClient->PlayerIDToDottedIP(packet->playerId), packet->playerId.port);

			if (packet->length == ServerInfo::BLOBBY_SERVER_PRESENT_PACKET_SIZE)
			{
				switchState(new LobbyState(info, PreviousState::MAIN));
			}
			break;
		}
		default:
			printf("Received unknown Packet %d\n", packet->data[0]);
			std::cout << packet->data << "\n";
			break;
		}
	}
}

void NetworkGameState::processState()
{
	IMGUI& imgui = IMGUI::getSingleton();
	RenderManager* rmanager = &RenderManager::getSingleton();
	InputManager* imanager = InputManager::getSingleton();
	SoundManager* sound = &SoundManager::getSingleton();

	if (imanager->exit() && mNetworkState != PLAYING && mNetworkState != WAITING_FOR_OPPONENT)
	{
		if (mNetworkState == PAUSING)
		{
			// end pause
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_UNPAUSE);
			mClient->Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
		}
		else
		{
			gKillHostThread = true;
			switchState(new MainMenuState);
		}
	}
	else if (imanager->exit() && mSaveReplay)
	{
		mSaveReplay = false;
		imgui.resetSelection();
	}
	else if (mErrorMessage != "")
	{
		displayErrorMessageBox();
	}
	else if (mSaveReplay)
	{
		if (displaySaveReplayPrompt())
		{
			// request replay from server
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_REPLAY);
			mClient->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);

			mSaveReplay = false;
			mWaitingForReplay = true;
		}
	}
	else if (mWaitingForReplay)
	{
		imgui.doOverlay(GEN_ID, Vector2(150, 200), Vector2(650, 400));
		imgui.doText(GEN_ID, Vector2(190, 220), TextManager::RP_WAIT_REPLAY);
		if (imgui.doButton(GEN_ID, Vector2(440, 330), TextManager::LBL_CANCEL))
		{
			mSaveReplay = false;
			mWaitingForReplay = false;
			imgui.resetSelection();
		}
		imgui.doCursor();
	}
	else switch (mNetworkState)
	{
	case WAITING_FOR_OPPONENT:
	{
		imgui.doOverlay(GEN_ID, Vector2(100.0, 210.0),
			Vector2(700.0, 310.0));
		imgui.doText(GEN_ID, Vector2(150.0, 250.0),
			TextManager::GAME_WAITING);

		if (imanager->exit())
		{
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_PAUSE);
			mClient->Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
		}
		break;
	}
	case OPPONENT_DISCONNECTED:
	{
		imgui.doCursor();
		imgui.doOverlay(GEN_ID, Vector2(100.0, 210.0), Vector2(700.0, 390.0));
		imgui.doText(GEN_ID, Vector2(140.0, 240.0), TextManager::GAME_OPP_LEFT);

		if (imgui.doButton(GEN_ID, Vector2(230.0, 290.0), TextManager::LBL_OK))
		{
			gKillHostThread = true;
			switchState(new MainMenuState);
		}

		if (imgui.doButton(GEN_ID, Vector2(350.0, 290.0), TextManager::RP_SAVE))
		{
			mSaveReplay = true;
			imgui.resetSelection();
		}

		if (imgui.doButton(GEN_ID, Vector2(250.0, 340.0), TextManager::NET_STAY_ON_SERVER))
		{
			// Send a blobby server connection request
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_BLOBBY_SERVER_PRESENT);
			stream.Write(BLOBBY_VERSION_MAJOR);
			stream.Write(BLOBBY_VERSION_MINOR);
			mClient->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);
		}
		break;
	}
	case DISCONNECTED:
	{
		imgui.doCursor();
		imgui.doOverlay(GEN_ID, Vector2(100.0, 210.0),
			Vector2(700.0, 370.0));
		imgui.doText(GEN_ID, Vector2(120.0, 250.0),
			TextManager::NET_DISCONNECT);
		if (imgui.doButton(GEN_ID, Vector2(230.0, 320.0),
			TextManager::LBL_OK))
		{
			gKillHostThread = true;
			switchState(new MainMenuState);
		}
		if (imgui.doButton(GEN_ID, Vector2(350.0, 320.0), TextManager::RP_SAVE))
		{
			mSaveReplay = true;
			imgui.resetSelection();
		}
		break;
	}
	case SERVER_FULL:
	{
		imgui.doCursor();
		imgui.doOverlay(GEN_ID, Vector2(100.0, 210.0), Vector2(700.0, 370.0));
		imgui.doText(GEN_ID, Vector2(200.0, 250.0), TextManager::NET_SERVER_FULL);
		if (imgui.doButton(GEN_ID, Vector2(350.0, 300.0), TextManager::LBL_OK))
		{
			gKillHostThread = true;
			switchState(new MainMenuState);
		}
		break;
	}
	case PLAYING:
	{
		mMatch->step();

		mLocalInput->updateInput();
		PlayerInputAbs input = mLocalInput->getRealInput();

		if (imanager->exit())
		{
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_PAUSE);
			mClient->Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
		}
		RakNet::BitStream stream;
		stream.Write((unsigned char)ID_INPUT_UPDATE);
		stream.Write(SDL_GetTicks());
		input.writeTo(stream);		
		mClient->Send(&stream, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 0);
		break;
	}
	case PLAYER_WON:
	{
		mMatch->updateEvents(); // so the last whistle will be sounded
		displayWinningPlayerScreen(mWinningPlayer);
		if (imgui.doButton(GEN_ID, Vector2(290, 360), TextManager::LBL_OK))
		{
			gKillHostThread = true;
			switchState(new MainMenuState());
		}
		if (imgui.doButton(GEN_ID, Vector2(380, 360), TextManager::RP_SAVE))
		{
			mSaveReplay = true;
			imgui.resetSelection();
		}
		break;
	}
	case PAUSING:
	{
		// Query
		displayQueryPrompt(20,
			TextManager::GAME_PAUSED,
			std::make_tuple(TextManager::LBL_CONTINUE, [&]() {
				RakNet::BitStream stream;
				stream.Write((unsigned char)ID_UNPAUSE);
				mClient->Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
				}),
			std::make_tuple(TextManager::GAME_QUIT, [&]() { gKillHostThread = true; switchState(new MainMenuState); }),
					std::make_tuple(TextManager::RP_SAVE, [&]() { mSaveReplay = true; imgui.resetSelection(); }));

		// Chat
		imgui.doChatbox(GEN_ID, Vector2(10, 240), Vector2(790, 500), mChatlog, mSelectedChatmessage, mChatOrigin);
		if (imgui.doEditbox(GEN_ID, Vector2(30, 510), 30, mChattext, mChatCursorPosition, 0, true))
		{

			// GUI-Hack, so that we can send messages
			if ((imanager->getLastActionKey() == "Return") && (mChattext != ""))
			{
				RakNet::BitStream stream;
				char message[MAX_MESSAGE_SIZE];

				strncpy(message, mChattext.c_str(), sizeof(message));
				stream.Write((unsigned char)ID_CHAT_MESSAGE);
				stream.Write(message, sizeof(message));
				mClient->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);
				appendChat(mPlayerIndex, ": " + mChattext, true);
				mChattext = "";
				mChatCursorPosition = 0;
				sound->playSound("sounds/chat.wav", ROUND_START_SOUND_VOLUME);
			}
		}
		imgui.doCursor();
	}
	}
}

void NetworkGameState::appendChat(PlayerSide player, std::string message, bool local)
{
	std::string result = mMatch->getPlayer(player).getName() + message;
	if(result.length() > MAX_MESSAGE_SIZE)
	{
		//need to split
		int spacePos = MAX_MESSAGE_SIZE;
		while(spacePos > 0 && result[spacePos] != ' ')
		{
			spacePos--;
		}
		if (spacePos > 0)
		{
			mChatlog.push_back(result.substr(0, spacePos));
			mChatlog.push_back(result.substr(spacePos + 1, result.length() - spacePos - 1));
			mChatOrigin.push_back(local);
			mChatOrigin.push_back(local);
		}
		else
		{
			mChatlog.push_back(result);
			mChatOrigin.push_back(local);
		}
	}
	else
	{
		mChatlog.push_back(result);
		mChatOrigin.push_back(local);
	}
	mSelectedChatmessage = mChatlog.size() - 1;
}


const char* NetworkGameState::getStateName() const
{
	return "NetworkGameState";
}
// definition of syslog for client hosted games
void syslog(int pri, const char* format, ...)
{
	// do nothing?
}

// debug counters
int SWLS_PacketCount;
int SWLS_Connections;
int SWLS_Games;
int SWLS_GameSteps;
int SWLS_ServerEntered;
