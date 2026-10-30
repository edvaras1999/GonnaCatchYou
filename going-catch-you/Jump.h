// Jump.h - the part of Going Catch You that moves an NPC through the air.
//
// A hunter that is not getting any closer is sent on a leap toward the player.
// The leap is a ballistic arc worked out in advance (Vault.cpp), and carried out
// the way FluidMotion carries out its flights: every frame the controller is
// put where the arc says, its state is set to "in the air" so the animation
// graph plays the fall pose, its velocity is set to the arc's, and its fall
// start height is kept at the current height so the landing does no damage.
// When the arc ends the NPC is put on the ground at the landing point and left
// to the game. For a short while after that it is watched, and if it has gone
// through the floor it is put back where it started.
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
		NiPoint3	dir;			// unit vector, flat, from start toward land
		float		horizontalSpeed;	// units per second along dir
		float		verticalSpeed;		// units per second up at the start
		float		gravity;			// units per second squared
		float		durationSec;
	};

	// Starts a leap. False if it cannot be started (the feature is off, too many
	// are running, this NPC is already in one, or its controller cannot be
	// read). Nothing is changed in the game when it returns false.
	bool Begin(Actor * npc, UInt32 id, const Plan & plan);

	bool	Active(UInt32 id);		// is this NPC in the air right now
	UInt32	ActiveCount();

	// Every frame. Moves each leap along, watches the landings, and writes the
	// aftermath line.
	void OnTick(DWORD now);

	// Drops every leap without touching the game (a load or a new game has made
	// the actors it referred to meaningless).
	void Reset();
}
