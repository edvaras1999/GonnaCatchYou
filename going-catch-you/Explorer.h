// Explorer.h - finding out how an NPC's movement and combat AI are laid out in memory.
//
// Nothing here changes the game. For a hunter (and, for comparison, for a few
// NPCs that are not in combat and for the player) it walks the objects the
// actor leads to, names every one of them from the executable's own RTTI, looks
// for what could be a path (runs of positions) and records which fields change
// when the NPC moves, stops, or goes the wrong way. A full dump of every hunter
// can be asked for with a key.
//
// The executable is Skyrim LE 1.9.32.0.8, which carries RTTI for every class.
// A class name is read out of an object from its first word (the table), the
// word before the table (the locator) and the locator's type descriptor.
#pragma once

#include "Engine.h"

#include "skse/GameReferences.h"
#include "skse/NiTypes.h"

namespace Explorer
{
	enum Role { kIdle = 0, kHunter = 1, kPlayer = 2 };

	// What the NPC was doing when it was looked at, for the correlations.
	enum Motion { kMotionUnknown = 0, kMoving = 1, kStopped = 2 };
	enum Approach { kApproachUnknown = 0, kApproaching = 1, kGoingElsewhere = 2 };

	// The class name of the object at `address`, or NULL if it is not an object
	// with a table the RTTI can name. The text is the type descriptor's own,
	// ".?AVName@@", inside the executable.
	const char * ClassName(UInt32 address);

	// One look at an NPC. `detailed` writes a line for every object and the path
	// candidates with their nodes, to the log, as a dump.
	void Explore(Actor * npc, UInt32 id, const Engine::Chain & chain, int role,
				 const NiPoint3 & npcPos, const NiPoint3 & playerPos,
				 int motion, int approach, bool detailed);

	// The differential and the correlations so far, to the log.
	void Summary(UInt32 playerPointer);

	UInt32 Samples(int role);
	void Reset();
}
