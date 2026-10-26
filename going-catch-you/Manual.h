// Manual.h - two keys for trying a move by hand.
//
// The Up arrow asks for a hop of the plugin's own, the Down arrow for FreeRun's
// vault animation. Either one acts on the NPC in combat nearest the player (a
// hunter by preference), ignoring every condition the automatic trigger has, so
// the move itself can be looked at without waiting for a hunter to get stuck.
// The keys are polled, and only while the game window is the foreground one.
#pragma once

#include <windows.h>

namespace Manual
{
	enum Request { kNone = 0, kArc = 1, kFreeRun = 2 };

	void OnTick(DWORD now);

	Request Pending();
	void Clear();
}
