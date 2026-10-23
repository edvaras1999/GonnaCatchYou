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
		v = ReadInt("iMaxTracks", 12);		maxTracks	= v < 1    ? 1    : (v > 64 ? 64 : (UInt32)v);

		radius				= ReadFloat("fRadius", 3000.0f);
		stoppedSpeed		= ReadFloat("fStoppedSpeed", 30.0f);
		stallMinDistance	= ReadFloat("fStallMinDistance", 400.0f);
		if(radius < 200.0f) radius = 200.0f;

		_MESSAGE("settings: enabled %d, sample %u ms, radius %.0f, all actors %d, status %u ms, "
				 "stopped below %.0f u/s for %u ms beyond %.0f units, at most %u watched",
				 enabled ? 1 : 0, (unsigned int)sampleMs, radius, allActors ? 1 : 0,
				 (unsigned int)statusMs, stoppedSpeed, (unsigned int)stallMs,
				 stallMinDistance, (unsigned int)maxTracks);
	}
}
