// Vault.h - what is in front of a hunter, and whether a hop would get it past.
//
// Scan casts into the physics world; Plan turns what was found into a hop for
// Jump.cpp to carry out. Neither changes the game.
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
		bool		sightVisible;		// it got there (a hit on the player's own capsule at the end counts)
	};

	extern const float kHeights[4];

	bool Scan(Actor * npc, const NiPoint3 & np, const NiPoint3 & pp, Scene & out);
	void LogScene(UInt32 id, const Scene & s);

	// A hop over a low obstacle in the way. False, with the reason in `why`,
	// when there is nothing to hop or it would not be safe.
	bool PlanLowObstacle(Actor * npc, const NiPoint3 & np, const NiPoint3 & pp, const Scene & s,
						 Jump::Plan & plan, char * why, size_t whySize);
}
