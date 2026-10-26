// Vault.h - what is in front of a hunter, and whether a hop would get it past.
//
// Scan casts into the physics world; Measure and the Plan functions turn what
// was found into a hop for Jump.cpp (or a FreeRun vault for FreeRun.cpp) to
// carry out. None of them changes the game.
//
// Characters are never obstacles here. Version 0.3.0 counted a hit on a
// character's capsule (layer 30) like any other, and reported the player's own
// body, a couple of hundred units away, as a wall.
#pragma once

#include "Jump.h"

#include "skse/GameReferences.h"
#include "skse/NiTypes.h"

namespace Vault
{
	struct Scene
	{
		float		hlen;				// horizontal distance to the player
		NiPoint3	dir;				// unit vector toward the player, flat
		float		castLength;			// how far the casts toward the player reach

		// Casts toward the player at feet, knee, waist and head height.
		bool		blocked[4];
		float		dist[4];
		UInt32		layer[4];
		int			top;				// the highest cast that was blocked, or -1

		// The ground 60 and 140 units ahead, relative to the NPC's feet.
		bool		groundHit[2];
		float		groundRel[2];

		// A cast from the NPC's chest to the player's.
		float		sightLen;
		bool		sightHit;
		float		sightDist;
		UInt32		sightLayer;
		bool		sightVisible;
	};

	extern const float kHeights[4];

	bool Scan(Actor * npc, const NiPoint3 & np, const NiPoint3 & pp, Scene & out);
	void LogScene(UInt32 id, const Scene & s);

	// How far ahead the obstacle is and how high its top is, for an obstacle the
	// scene says is lower than a wall. False, with the reason, otherwise.
	bool Measure(Actor * npc, const NiPoint3 & np, const Scene & s, float & distance, float & height,
				 char * why, size_t whySize);

	// A hop over a low obstacle in the way, for Jump.cpp. The obstacle has to be
	// within `triggerDistance` and no higher than `maxHeight`.
	bool PlanLowObstacle(Actor * npc, const NiPoint3 & np, const NiPoint3 & pp, const Scene & s,
						 float triggerDistance, float maxHeight,
						 Jump::Plan & plan, char * why, size_t whySize);

	// For the manual test: a hop of about 160 units toward the player over
	// whatever is there, whether or not there is an obstacle.
	bool PlanPlainHop(Actor * npc, const NiPoint3 & np, const Scene & s,
					  Jump::Plan & plan, char * why, size_t whySize);
}
