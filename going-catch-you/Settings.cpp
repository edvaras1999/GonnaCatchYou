#include "Settings.h"
#include "Log.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Settings
{
	bool	enabled				= true;
	UInt32	sampleMs			= 500;
	float	radius				= 3000.0f;
	bool	allActors			= false;
	UInt32	statusMs			= 2000;
	float	stoppedSpeed		= 30.0f;
	UInt32	stallMs				= 2000;
	float	stallMinDistance	= 400.0f;
	bool	includeDrawn		= false;
	bool	cast				= true;
	UInt32	castMs				= 1000;
	float	castDistance		= 300.0f;
	UInt32	summaryMs			= 30000;
	bool	jump				= false;
	bool	jumpDryRun			= false;
	float	jumpMaxObstacle		= 110.0f;
	float	jumpTriggerDistance	= 120.0f;
	float	jumpMinPlayerDistance = 200.0f;
	UInt32	jumpStopMs			= 1200;
	UInt32	jumpCooldownMs		= 4000;
	bool	jumpAnimation		= true;
	char	jumpStartEvent[64]	= "JumpDirectionalStart";
	char	jumpEndEvent[64]	= "JumpLand";
	UInt32	detourMs			= 2000;
	float	detourMinSpeed		= 90.0f;
	float	detourAngle			= 60.0f;
	bool	manualKeys			= false;
	int		keyArc				= 0x26;		// VK_UP
	int		keyFreeRun			= 0x28;		// VK_DOWN
	float	manualRange			= 2500.0f;
	bool	explore				= true;
	UInt32	exploreMs			= 3000;
	UInt32	exploreMaxObjects	= 300;
	UInt32	exploreDepth		= 5;
	int		keyDump				= 0x2D;		// VK_INSERT
	bool	dumpNeedsShift		= true;
	UInt32	keepMs				= 5000;
	bool	probe				= false;
	UInt32	probeMs				= 4000;
	UInt32	maxTracks			= 12;

	namespace
	{
		char s_path[MAX_PATH];

		// GetPrivateProfile* looks in the Windows directory when the path is not
		// absolute, so the path is built from the game executable's folder.
		void BuildPath()
		{
			s_path[0] = 0;
			if(!GetModuleFileNameA(NULL, s_path, sizeof(s_path))) return;

			char * slash = strrchr(s_path, '\\');
			if(slash) slash[1] = 0;
			strcat_s(s_path, sizeof(s_path), "Data\\SKSE\\Plugins\\GoingCatchYou.ini");
		}

		int ReadInt(const char * key, int fallback)
		{
			return (int)GetPrivateProfileIntA("General", key, fallback, s_path);
		}

		float ReadFloat(const char * key, float fallback)
		{
			char def[32], text[64];
			sprintf_s(def, sizeof(def), "%g", fallback);
			GetPrivateProfileStringA("General", key, def, text, sizeof(text), s_path);
			return (float)atof(text);
		}
	}

	void Load()
	{
		BuildPath();

		const bool exists = GetFileAttributesA(s_path) != INVALID_FILE_ATTRIBUTES;
		_MESSAGE("settings: %s (%s)", s_path, exists ? "found" : "not found - defaults used");

		enabled				= ReadInt("bEnabled", 1) != 0;
		allActors			= ReadInt("bAllActors", 0) != 0;

		int v;
		v = ReadInt("iSampleMs", 500);		sampleMs	= v < 50   ? 50   : (UInt32)v;
		v = ReadInt("iStatusMs", 2000);		statusMs	= v < 250  ? 250  : (UInt32)v;
		v = ReadInt("iStallMs", 2000);		stallMs		= v < 250  ? 250  : (UInt32)v;
		probe				= ReadInt("bProbe", 0) != 0;
		includeDrawn		= ReadInt("bIncludeWeaponDrawn", 0) != 0;
		jump				= ReadInt("bJump", 0) != 0;
		manualKeys			= ReadInt("bManualKeys", 0) != 0;
		explore				= ReadInt("bExplore", 1) != 0;
		keyDump				= ReadInt("iKeyDump", 0x2D);
		dumpNeedsShift		= ReadInt("bDumpNeedsShift", 1) != 0;
		v = ReadInt("iExploreMs", 3000);		exploreMs		= v < 500  ? 500  : (UInt32)v;
		v = ReadInt("iExploreMaxObjects", 300);	exploreMaxObjects = v < 20 ? 20 : (v > 600 ? 600 : (UInt32)v);
		v = ReadInt("iExploreDepth", 5);		exploreDepth	= v < 2 ? 2 : (v > 7 ? 7 : (UInt32)v);
		keyArc				= ReadInt("iKeyHopArc", 0x26);
		keyFreeRun			= ReadInt("iKeyHopFreeRun", 0x28);
		manualRange			= ReadFloat("fManualRange", 2500.0f);
		detourMinSpeed		= ReadFloat("fDetourMinSpeed", 90.0f);
		detourAngle			= ReadFloat("fDetourAngle", 60.0f);
		if(detourAngle < 20.0f) detourAngle = 20.0f;
		if(detourAngle > 120.0f) detourAngle = 120.0f;
		v = ReadInt("iDetourMs", 2000);		detourMs	= v < 500  ? 500  : (UInt32)v;
		jumpDryRun			= ReadInt("bJumpDryRun", 0) != 0;
		jumpAnimation		= ReadInt("bJumpAnimation", 1) != 0;
		jumpMaxObstacle		= ReadFloat("fJumpMaxObstacle", 110.0f);
		jumpTriggerDistance	= ReadFloat("fJumpTriggerDistance", 120.0f);
		jumpMinPlayerDistance = ReadFloat("fJumpMinPlayerDistance", 200.0f);
		if(jumpMaxObstacle > 110.0f) jumpMaxObstacle = 110.0f;		// the casts do not look higher than 120
		if(jumpMaxObstacle < 20.0f) jumpMaxObstacle = 20.0f;
		GetPrivateProfileStringA("General", "sJumpStartEvent", "JumpDirectionalStart", jumpStartEvent, sizeof(jumpStartEvent), s_path);
		GetPrivateProfileStringA("General", "sJumpEndEvent", "JumpLand", jumpEndEvent, sizeof(jumpEndEvent), s_path);
		v = ReadInt("iJumpStopMs", 1200);		jumpStopMs		= v < 300  ? 300  : (UInt32)v;
		v = ReadInt("iJumpCooldownMs", 4000);	jumpCooldownMs	= v < 1000 ? 1000 : (UInt32)v;
		cast				= ReadInt("bCast", 1) != 0;
		v = ReadInt("iCastMs", 1000);		castMs		= v < 200  ? 200  : (UInt32)v;
		v = ReadInt("iSummaryMs", 30000);	summaryMs	= v < 5000 ? 5000 : (UInt32)v;
		v = ReadInt("iKeepMs", 5000);		keepMs		= v < 0    ? 0    : (UInt32)v;
		v = ReadInt("iProbeMs", 4000);		probeMs		= v < 500  ? 500  : (UInt32)v;
		v = ReadInt("iMaxTracks", 12);		maxTracks	= v < 1    ? 1    : (v > 64 ? 64 : (UInt32)v);

		radius				= ReadFloat("fRadius", 3000.0f);
		castDistance		= ReadFloat("fCastDistance", 300.0f);
		if(castDistance < 100.0f) castDistance = 100.0f;
		stoppedSpeed		= ReadFloat("fStoppedSpeed", 30.0f);
		stallMinDistance	= ReadFloat("fStallMinDistance", 400.0f);
		if(radius < 200.0f) radius = 200.0f;

		_MESSAGE("settings: explorer %d (every %u ms, %u objects, %u deep), dump key %d%s",
				 explore ? 1 : 0, (unsigned int)exploreMs, (unsigned int)exploreMaxObjects, (unsigned int)exploreDepth,
				 keyDump, dumpNeedsShift ? " + Shift" : "");
		_MESSAGE("settings: test keys %d (arc %d, FreeRun %d), detour trigger: moving over %.0f u/s and over %.0f degrees off the line for %u ms",
				 manualKeys ? 1 : 0, keyArc, keyFreeRun, detourMinSpeed, detourAngle, (unsigned int)detourMs);
		_MESSAGE("settings: jump %d (dry run %d), obstacles up to %.0f within %.0f units, not within %.0f of the player, "
				 "after %u ms still, every %u ms at most, animation %d (%s / %s)",
				 jump ? 1 : 0, jumpDryRun ? 1 : 0, jumpMaxObstacle, jumpTriggerDistance, jumpMinPlayerDistance,
				 (unsigned int)jumpStopMs, (unsigned int)jumpCooldownMs, jumpAnimation ? 1 : 0, jumpStartEvent, jumpEndEvent);
		_MESSAGE("settings: enabled %d, sample %u ms, radius %.0f, all actors %d, status %u ms, "
				 "stopped below %.0f u/s for %u ms beyond %.0f units, keep %u ms, probe %d every %u ms, world cast %d every %u ms over %.0f units, weapon-drawn too %d, at most %u watched",
				 enabled ? 1 : 0, (unsigned int)sampleMs, radius, allActors ? 1 : 0,
				 (unsigned int)statusMs, stoppedSpeed, (unsigned int)stallMs,
				 stallMinDistance, (unsigned int)keepMs, probe ? 1 : 0, (unsigned int)probeMs,
				 cast ? 1 : 0, (unsigned int)castMs, castDistance, includeDrawn ? 1 : 0, (unsigned int)maxTracks);
	}
}
