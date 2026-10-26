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
	extern bool		jump;				// bJump               make stopped hunters hop over low obstacles
	extern bool		jumpDryRun;			// bJumpDryRun         plan and log the hop, but do not do it
	extern float	jumpMaxObstacle;	// fJumpMaxObstacle    the highest obstacle it will hop, in units
	extern float	jumpTriggerDistance;// fJumpTriggerDistance  the obstacle has to be this close to the NPC
	extern float	jumpMinPlayerDistance;// fJumpMinPlayerDistance  a hunter nearer than this to the player does not hop
	extern UInt32	jumpStopMs;			// iJumpStopMs         how long it has to stand still first
	extern UInt32	jumpCooldownMs;		// iJumpCooldownMs     between two attempts for the same NPC
	extern bool		jumpAnimation;		// bJumpAnimation      send the jump animation events
	extern char		jumpStartEvent[64];	// sJumpStartEvent
	extern char		jumpEndEvent[64];	// sJumpEndEvent
	extern UInt32	detourMs;			// iDetourMs           how long a hunter has to keep going the wrong way before it counts as a detour
	extern float	detourMinSpeed;		// fDetourMinSpeed    only a hunter moving faster than this can be detouring (units/s)
	extern float	detourAngle;		// fDetourAngle       degrees off the straight line to the player that make a detour
	extern bool		manualKeys;			// bManualKeys         the test keys
	extern int		keyArc;				// iKeyHopArc          virtual-key code: the plugin's own hop
	extern int		keyFreeRun;			// iKeyHopFreeRun      virtual-key code: FreeRun's vault animation
	extern float	manualRange;		// fManualRange        the test keys act on the NPC in combat nearest the player, within this
	extern UInt32	keepMs;				// iKeepMs         how long an NPC that stops qualifying is still kept (hides flicker)
	extern bool		probe;				// bProbe          compare NPCs in combat with NPCs not in combat, field by field
	extern UInt32	probeMs;			// iProbeMs        how often
	extern UInt32	maxTracks;			// iMaxTracks       the most NPCs watched at once

	void Load();
}
