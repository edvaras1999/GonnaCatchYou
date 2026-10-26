// FreeRun.h - borrowing FreeRun's vault animation for an NPC.
//
// FreeRun LE's behaviour patch speaks a small protocol through the animation
// graph, and the FreeRun NPC project showed it works on NPCs: write the move's
// type into SkyParkourLedge, tell the engine the animation is in charge of the
// movement (bAnimationDriven), and send the SkyParkour event. The patch answers
// by raising SkyParkourOngoing for as long as the move lasts.
//
// That is all this does, for one move, a vault over something waist high. It
// does not need FreeRun's DLL, only its behaviour patch in the NPC's graph, and
// it says so in the log when the graph does not react. Nothing here is a port
// of the FreeRun NPC module, which also walked the NPC to a start mark and
// corrected where it landed; that is the part that glitched, and it is not here.
#pragma once

#include "skse/GameReferences.h"

#include <windows.h>

namespace FreeRun
{
	// The vault animation is authored for obstacles between these heights.
	enum { kVaultMinHeight = 42, kVaultMaxHeight = 120 };

	// The NPC has to be standing close to the obstacle, facing it.
	enum { kMaxObstacleDistance = 110 };

	// Starts a vault. False if it cannot (the obstacle is the wrong height or too
	// far, another move is running, or the NPC cannot be read). Nothing is
	// changed in the game when it returns false.
	bool Begin(Actor * npc, UInt32 id, float obstacleDistance, float obstacleHeight);

	bool Active(UInt32 id);

	// Every frame.
	void OnTick(DWORD now);
	void Reset();
}
