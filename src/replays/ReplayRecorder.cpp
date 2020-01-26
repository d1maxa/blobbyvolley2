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
#include "ReplayRecorder.h"

/* includes */
#include <algorithm>
#include <iostream>
#include <sstream>
#include <ctime>

#include <boost/algorithm/string/trim_all.hpp>

#include "tinyxml/tinyxml.h"

#include "raknet/BitStream.h"

#include <SDL2/SDL.h>

#include "Global.h"
#include "ReplayDefs.h"
#include "IReplayLoader.h"
#include "PhysicState.h"
#include "GenericIO.h"
#include "FileRead.h"
#include "FileWrite.h"
#include "base64.h"
#include "UserConfig.h"

/* implementation */
VersionMismatchException::VersionMismatchException(const std::string& filename, uint8_t major, uint8_t minor)
{
	std::stringstream errorstr;

	errorstr << "Error: Outdated replay file: " << filename <<
		std::endl << "expected version: " << (int)REPLAY_FILE_VERSION_MAJOR << "."
				<< (int)REPLAY_FILE_VERSION_MINOR <<
		std::endl << "got: " << (int)major << "." << (int)minor << " instead!" << std::endl;
	error = errorstr.str();
}

VersionMismatchException::~VersionMismatchException() throw()
{
}

const char* VersionMismatchException::what() const throw()
{
	return error.c_str();
}



ReplayRecorder::ReplayRecorder()
{
	mGameSpeed = -1;
}

ReplayRecorder::~ReplayRecorder()
{
}
template<class T>
void writeAttribute(FileWrite& file, const char* name, const T& value)
{
	std::stringstream stream;
	stream << "\t<var name=\"" << name << "\" value=\"" << value << "\" />\n";
	file.write( stream.str() );
}

void ReplayRecorder::save( std::shared_ptr<FileWrite> file) const
{
	constexpr const char* xmlHeader = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n<replay>\n";
	constexpr const char* xmlFooter = "</replay>\n\n";

	file->write(xmlHeader);

	char writeBuffer[256];
	int charsWritten = snprintf(writeBuffer, 256,
			"\t<version major=\"%i\" minor=\"%i\"/>\n",
			REPLAY_FILE_VERSION_MAJOR, REPLAY_FILE_VERSION_MINOR);
	file->write(writeBuffer, charsWritten);

	writeAttribute(*file, "game_speed", mGameSpeed);
	writeAttribute(*file, "game_length", mSaveData.size());
	writeAttribute(*file, "game_duration", mSaveData.size() / (mGameSpeed * mBytesPerStep));
	writeAttribute(*file, "game_date", std::time(0));

	writeAttribute(*file, "score_left", mEndScore[LEFT_SIDE]);
	writeAttribute(*file, "score_right", mEndScore[RIGHT_SIDE]);

	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(mPlayerEnabled[i])
		{
			auto prefix = UserConfig::getPlayerPrefix(PlayerSide(i));

			writeAttribute(*file, ("name_" + prefix).c_str(), mPlayerNames[i]);
			/// \todo would be nice if we could write the actual colors instead of integers
			writeAttribute(*file, ("color_" + prefix).c_str(), mPlayerColors[i].toInt());
		}
	}		
	
	// write the game rules
	file->write("\t<rules>\n");
	std::string coded;
	TiXmlBase::EncodeString(mGameRules, &coded);
	file->write(coded);
	file->write("\n\t</rules>\n");


	// now comes the actual replay data
	file->write("\t<input>\n");
	std::string binary = encode(mSaveData, 80);
	file->write(binary);
	file->write("\n\t</input>\n");

	// finally, write the save points
	// first, convert them into a POD
	file->write("\t<states>\n");
	RakNet::BitStream stream;
	auto convert = createGenericWriter(&stream);
	convert->generic<std::vector<ReplaySavePoint> > (mSavePoints);

	binary = encode((char*)stream.GetData(), (char*)stream.GetData() + stream.GetNumberOfBytesUsed(), 80);
	file->write(binary);
	file->write("\n\t</states>\n");

	file->write(xmlFooter);
	file->close();
}
void ReplayRecorder::send(std::shared_ptr<GenericOut> target) const
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(mPlayerEnabled[i])
		{
			target->string(mPlayerNames[i]);
			target->generic<Color> (mPlayerColors[i]);
		}
	}
	
	target->uint32( mGameSpeed );
	target->uint32( mEndScore[LEFT_SIDE] );
	target->uint32( mEndScore[RIGHT_SIDE] );

	target->string(mGameRules);

	target->generic<std::vector<unsigned char> >(mSaveData);
	target->generic<std::vector<ReplaySavePoint> > (mSavePoints);
}

void ReplayRecorder::receive(std::shared_ptr<GenericIn> source)
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (mPlayerEnabled[i])
		{
			source->string(mPlayerNames[i]);
			source->generic<Color> (mPlayerColors[i]);			
		}
	}	

	source->uint32( mGameSpeed );
	source->uint32( mEndScore[LEFT_SIDE] );
	source->uint32( mEndScore[RIGHT_SIDE] );

	source->string(mGameRules);

	source->generic<std::vector<unsigned char> >(mSaveData);
	source->generic<std::vector<ReplaySavePoint> > (mSavePoints);
}

void ReplayRecorder::record(const DuelMatchState& state)
{
	// save the state every REPLAY_SAVEPOINT_PERIOD frames
	// or when something interesting occurs
	if(mSaveData.size() % (REPLAY_SAVEPOINT_PERIOD * mBytesPerStep) == 0 ||
		mEndScore[LEFT_SIDE] != state.logicState.leftScore ||
		mEndScore[RIGHT_SIDE] != state.logicState.rightScore)
	{
		ReplaySavePoint sp;
		sp.state = state;
		sp.step = mSaveData.size();
		mSavePoints.push_back(sp);
	}
		
	// we save this 1 here just for compatibility
	// set highest bit to 1
	unsigned char packet = 1 << 7;
	auto newPacket = true;

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (mPlayerEnabled[i])
		{
			if (newPacket)
			{			
				packet = 1 << 7;
				packet |= (state.playerInput[i].getAll() & 7) << 3;
				newPacket = false;
			}
			else
			{
				packet |= (state.playerInput[i].getAll() & 7);
				mSaveData.push_back(packet);			
				newPacket = true;
			}		
		}
	}

	if(!newPacket)
	{
		mSaveData.push_back(packet);
	}

	// update the score
	mEndScore[LEFT_SIDE] = state.logicState.leftScore;
	mEndScore[RIGHT_SIDE] = state.logicState.rightScore;
}

void ReplayRecorder::setPlayerEnabled(bool playerEnabled[MAX_PLAYERS])
{
	auto playersCount = 0;
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		mPlayerEnabled[i] = playerEnabled[i];
		if (playerEnabled[i])
			playersCount++;
	}
	mBytesPerStep = (playersCount + 1) / 2;
}

void ReplayRecorder::setPlayerNames(std::string playerNames[MAX_PLAYERS])
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		mPlayerNames[i] = playerNames[i];
	}	
}

void ReplayRecorder::setPlayerColors(Color colors[MAX_PLAYERS])
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		mPlayerColors[i] = colors[i];
	}	
}

void ReplayRecorder::setGameSpeed(int fps)
{
	mGameSpeed = fps;
}

void ReplayRecorder::setGameRules( std::string rules )
{
	FileRead file(FileRead::makeLuaFilename("rules/"+rules));
	mGameRules.resize( file.length() );
	file.readRawBytes(&*mGameRules.begin(), file.length());
	boost::algorithm::trim_all(mGameRules);
}

void ReplayRecorder::finalize(unsigned int left, unsigned int right)
{
	mEndScore[LEFT_SIDE] = left;
	mEndScore[RIGHT_SIDE] = right;

	// fill with one second of do nothing
	for(int i = 0; i < 75 * mBytesPerStep; ++i)
	{
		unsigned char packet = 0;
		mSaveData.push_back(packet);
	}
}
