// Jump.h - the first thing in Going Catch You that changes the game.
//
// A hunter that has stopped in front of something low gets hopped over it.
// The hop is not a physical jump: the NPC's physics is switched off, its
// character controller is moved along a short arc one frame at a time, and the
// physics is switched back on where it lands. Those are the three primitives
// the FreeRun NPC project proved on NPCs in game. A vanilla jump animation
// event is sent alongside, for how it looks; it is optional.
//
// Everything here is behind structured exception handlers, and after two
// faults the whole thing switches itself off for the session.

#pragma once

#include "skse/GameReferences.h"
#include "skse/NiTypes.h"

#include <windows.h>

namespace Jump
{
	struct Plan
	{
		NiPoint3	start;			// where the NPC stands, game units
		NiPoint3	land;			// where it comes down
		float		apex;			// how high the arc rises above the straight line from start to land
		float		durationSec;
	};

	// Starts a hop. False if it cannot be started (the feature is off, too many
	// hops are running, this NPC is already in one, or its controller cannot be
	// read). Nothing is changed in the game when it returns false.
	bool Begin(Actor * npc, UInt32 id, const Plan & plan);

	bool	Active(UInt32 id);		// is this NPC in the air right now
	UInt32	ActiveCount();

	// Every frame. Moves each hop along, and writes the aftermath line.
	void OnTick(DWORD now);

	// Drops every hop without touching the game (a load or a new game has made
	// the actors it referred to meaningless).
	void Reset();
}
