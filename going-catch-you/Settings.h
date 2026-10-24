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
	extern bool		includeDrawn;		// bIncludeWeaponDrawn  also watch NPCs that only have a weapon drawn (the old criterion)
	extern bool		cast;				// bCast           probe the world (obstacles, ledges, line of sight) around stopped hunters
	extern UInt32	castMs;				// iCastMs         at most one probe per NPC this often
	extern float	castDistance;		// fCastDistance   how far ahead the probe looks
	extern UInt32	summaryMs;			// iSummaryMs      how often the probe statistics are written
	extern UInt32	keepMs;				// iKeepMs         how long an NPC that stops qualifying is still kept (hides flicker)
	extern bool		probe;				// bProbe          compare NPCs in combat with NPCs not in combat, field by field
	extern UInt32	probeMs;			// iProbeMs        how often
	extern UInt32	maxTracks;			// iMaxTracks       the most NPCs watched at once

	void Load();
}
