// Settings.h - Data\SKSE\Plugins\GoingCatchYou.ini
#pragma once

#include "skse/GameTypes.h"

namespace Settings
{
	extern bool		enabled;			// bEnabled
	extern UInt32	sampleMs;			// iSampleMs        how often the world is sampled
	extern float	radius;				// fRadius          only NPCs this close to the player are watched
	extern bool		allActors;			// bAllActors       also watch NPCs that have no weapon drawn (calibration)
	extern UInt32	statusMs;			// iStatusMs        how often each watched NPC gets a status line
	extern float	stoppedSpeed;		// fStoppedSpeed    own speed (units/s, from its positions) below which an NPC counts as stopped
	extern UInt32	stallMs;			// iStallMs         how long it has to stay stopped before it is reported
	extern float	stallMinDistance;	// fStallMinDistance  an NPC nearer than this to the player is not "stuck far away"
	extern UInt32	maxTracks;			// iMaxTracks       the most NPCs watched at once

	void Load();
}
