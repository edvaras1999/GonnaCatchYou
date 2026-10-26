#include "Vault.h"
#include "Settings.h"
#include "Engine.h"
#include "Havok.h"
#include "Log.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

namespace Vault
{
	// Feet, knee, waist, head. An obstacle that blocks the first and not the
	// second is something to step or hop over; one that blocks all four is a wall.
	const float kHeights[4] = { 25.0f, 70.0f, 120.0f, 170.0f };

	namespace
	{
		// The layer the player's parkour casts on. The NPC's own collision
		// filter is applied by Havok::RayCast, so it never hits itself.
		const UInt32 kLayer = Havok::kLayer_Transparent;

		struct Solid
		{
			bool	hit;
			float	distance;		// to the nearest solid hit, else the full length
			UInt32	layer;
		};

		// A ray cast that sees through characters: the nearest hit that is not on
		// the character-controller layer.
		Solid CastSolid(const NiPoint3 & start, const NiPoint3 & dir, float length, Actor * caster)
		{
			const Havok::Result r = Havok::RayCast(start, dir, length, kLayer, caster);

			Solid s;
			s.hit = false;
			s.distance = length;
			s.layer = 0;

			const UInt32 n = r.hitCount < (UInt32)Havok::kMaxHits ? r.hitCount : (UInt32)Havok::kMaxHits;
			for(UInt32 i = 0; i < n; ++i)
			{
				if(r.hits[i].layer == Havok::kLayer_CharController) continue;
				s.hit = true;
				s.distance = r.hits[i].distance;
				s.layer = r.hits[i].layer;
				break;
			}
			return s;
		}
	}

	bool Scan(Actor * npc, const NiPoint3 & np, const NiPoint3 & pp, Scene & s)
	{
		memset(&s, 0, sizeof(s));

		const float dx = pp.x - np.x, dy = pp.y - np.y;
		s.hlen = sqrtf(dx * dx + dy * dy);
		if(s.hlen < 1.0f) return false;

		s.dir = NiPoint3(dx / s.hlen, dy / s.hlen, 0.0f);
		s.castLength = s.hlen < Settings::castDistance ? s.hlen : Settings::castDistance;
		s.top = -1;

		for(int i = 0; i < 4; ++i)
		{
			const NiPoint3 start(np.x, np.y, np.z + kHeights[i]);
			const Solid r = CastSolid(start, s.dir, s.castLength, npc);
			s.blocked[i] = r.hit;
			s.dist[i] = r.distance;
			s.layer[i] = r.layer;
			if(r.hit) s.top = i;
		}

		static const float ahead[2] = { 60.0f, 140.0f };
		for(int i = 0; i < 2; ++i)
		{
			const NiPoint3 start(np.x + s.dir.x * ahead[i], np.y + s.dir.y * ahead[i], np.z + 120.0f);
			const NiPoint3 down(0.0f, 0.0f, -1.0f);
			const Solid r = CastSolid(start, down, 700.0f, npc);
			s.groundHit[i] = r.hit;
			s.groundRel[i] = r.hit ? (start.z - r.distance) - np.z : 0.0f;
		}

		const NiPoint3 a(np.x, np.y, np.z + 110.0f);
		const NiPoint3 b(pp.x, pp.y, pp.z + 110.0f);
		const float lx = b.x - a.x, ly = b.y - a.y, lz = b.z - a.z;
		s.sightLen = sqrtf(lx * lx + ly * ly + lz * lz);
		s.sightVisible = true;
		if(s.sightLen >= 1.0f)
		{
			const NiPoint3 d3(lx / s.sightLen, ly / s.sightLen, lz / s.sightLen);
			const Solid r = CastSolid(a, d3, s.sightLen, npc);
			s.sightHit = r.hit;
			s.sightDist = r.distance;
			s.sightLayer = r.layer;
			s.sightVisible = !r.hit;
		}

		return true;
	}

	void LogScene(UInt32 id, const Scene & s)
	{
		char wall[160];
		wall[0] = 0;
		for(int i = 0; i < 4; ++i)
		{
			char piece[40];
			if(s.blocked[i])	sprintf_s(piece, sizeof(piece), " %.0f:%.0f(L%u)", kHeights[i], s.dist[i], (unsigned int)s.layer[i]);
			else				sprintf_s(piece, sizeof(piece), " %.0f:clear", kHeights[i]);
			strcat_s(wall, sizeof(wall), piece);
		}

		char verdict[64];
		if(s.top < 0)		strcpy_s(verdict, sizeof(verdict), "clear ahead");
		else if(s.top == 3)	strcpy_s(verdict, sizeof(verdict), "wall: blocked up to the head");
		else				sprintf_s(verdict, sizeof(verdict), "obstacle, top between %.0f and %.0f", kHeights[s.top], kHeights[s.top + 1]);

		char ground[96];
		ground[0] = 0;
		static const float ahead[2] = { 60.0f, 140.0f };
		for(int i = 0; i < 2; ++i)
		{
			char piece[48];
			if(s.groundHit[i])	sprintf_s(piece, sizeof(piece), " %.0f:%+.0f", ahead[i], s.groundRel[i]);
			else				sprintf_s(piece, sizeof(piece), " %.0f:void", ahead[i]);
			strcat_s(ground, sizeof(ground), piece);
		}

		char sight[80];
		if(s.sightLen < 1.0f)		strcpy_s(sight, sizeof(sight), "adjacent");
		else if(s.sightVisible)		strcpy_s(sight, sizeof(sight), "visible");
		else						sprintf_s(sight, sizeof(sight), "blocked at %.0f of %.0f (L%u)", s.sightDist, s.sightLen, (unsigned int)s.sightLayer);

		_MESSAGE("observer: world %08X toward the player (%.0f units): [%s ] -> %s; ground ahead [%s ]; line of sight: %s",
				 (unsigned int)id, s.hlen, wall, verdict, ground, sight);
	}

	bool Measure(Actor * npc, const NiPoint3 & np, const Scene & s, float & distance, float & height,
				 char * why, size_t whySize)
	{
		why[0] = 0;

		if(s.top < 0)		{ strcpy_s(why, whySize, "nothing in the way"); return false; }
		if(s.top >= 3)		{ strcpy_s(why, whySize, "a wall, blocked up to the head"); return false; }
		if(!s.blocked[0])	{ strcpy_s(why, whySize, "nothing at foot height (something overhead, not a step)"); return false; }

		distance = s.dist[0];

		// How high is it? The cast one step up from the highest blocked one was
		// clear, so a cast straight down from that height, just past the front of
		// the obstacle, lands on its top.
		const float upper = kHeights[s.top + 1];
		const NiPoint3 topStart(np.x + s.dir.x * (distance + 14.0f), np.y + s.dir.y * (distance + 14.0f), np.z + upper);
		const NiPoint3 down(0.0f, 0.0f, -1.0f);
		const Solid top = CastSolid(topStart, down, upper + 60.0f, npc);

		height = upper;		// unknown: assume it is as high as the casts allow
		if(top.hit)
		{
			const float found = (topStart.z - top.distance) - np.z;
			if(found >= 8.0f) height = found;
		}
		return true;
	}

	bool PlanLowObstacle(Actor * npc, const NiPoint3 & np, const NiPoint3 & pp, const Scene & s,
						 float triggerDistance, float maxHeight,
						 Jump::Plan & plan, char * why, size_t whySize)
	{
		float d0 = 0.0f, height = 0.0f;
		if(!Measure(npc, np, s, d0, height, why, whySize)) return false;

		if(d0 > triggerDistance)
		{
			sprintf_s(why, whySize, "the obstacle is %.0f ahead, farther than %.0f", d0, triggerDistance);
			return false;
		}
		if(height > maxHeight)
		{
			sprintf_s(why, whySize, "too high: about %.0f, the limit is %.0f", height, maxHeight);
			return false;
		}

		// Where it comes down: past the obstacle, on ground at about the NPC's own
		// level, with nothing overhead and a clear corridor at the height of the
		// arc. Not past the player.
		static const float past[3] = { 70.0f, 100.0f, 140.0f };
		const NiPoint3 down(0.0f, 0.0f, -1.0f);
		bool found = false;
		float landDist = 0.0f, landZ = 0.0f;
		for(int i = 0; i < 3 && !found; ++i)
		{
			const float d = d0 + past[i];
			if(d + 60.0f > s.hlen) continue;

			const NiPoint3 p(np.x + s.dir.x * d, np.y + s.dir.y * d, 0.0f);

			const NiPoint3 gs(p.x, p.y, np.z + height + 120.0f);
			const Solid g = CastSolid(gs, down, 400.0f, npc);
			if(!g.hit) continue;
			const float gz = gs.z - g.distance;
			if(fabsf(gz - np.z) > 45.0f) continue;

			const NiPoint3 up(0.0f, 0.0f, 1.0f);
			const NiPoint3 os(p.x, p.y, gz + 10.0f);
			if(CastSolid(os, up, 160.0f, npc).hit) continue;

			const NiPoint3 cs(np.x, np.y, np.z + height + 50.0f);
			if(CastSolid(cs, s.dir, d, npc).hit) continue;

			found = true;
			landDist = d;
			landZ = gz;
		}
		if(!found) { strcpy_s(why, whySize, "no safe place to land past it"); return false; }

		// The arc has to clear the top by a margin where the obstacle is, which is
		// d0 of the way along; the parabola is highest in the middle, so where the
		// obstacle is off-centre it has to be raised a little to compensate.
		const float sObs = d0 / landDist;
		const float shape = 4.0f * sObs * (1.0f - sObs);
		const float chordAtObstacle = (landZ - np.z) * sObs;
		float apex = (height + 30.0f - chordAtObstacle) / (shape > 0.5f ? shape : 0.5f);
		if(apex < 45.0f) apex = 45.0f;
		if(apex > 170.0f) apex = 170.0f;

		float seconds = landDist / 380.0f + 0.15f;
		if(seconds < 0.5f) seconds = 0.5f;
		if(seconds > 1.2f) seconds = 1.2f;

		plan.start = np;
		plan.land = NiPoint3(np.x + s.dir.x * landDist, np.y + s.dir.y * landDist, landZ);
		plan.apex = apex;
		plan.durationSec = seconds;

		sprintf_s(why, whySize, "obstacle %.0f ahead, about %.0f high; lands %.0f ahead (ground %+.0f); arc %.0f over %.2f s",
				  d0, height, landDist, landZ - np.z, apex, seconds);
		return true;
	}

	bool PlanPlainHop(Actor * npc, const NiPoint3 & np, const Scene & s,
					  Jump::Plan & plan, char * why, size_t whySize)
	{
		why[0] = 0;

		const float d = 160.0f;
		const NiPoint3 gs(np.x + s.dir.x * d, np.y + s.dir.y * d, np.z + 120.0f);
		const NiPoint3 down(0.0f, 0.0f, -1.0f);
		const Solid g = CastSolid(gs, down, 400.0f, npc);
		if(!g.hit) { strcpy_s(why, whySize, "no ground 160 units ahead"); return false; }

		const float gz = gs.z - g.distance;
		if(fabsf(gz - np.z) > 80.0f)
		{
			sprintf_s(why, whySize, "the ground 160 ahead is %+.0f from the feet, too different", gz - np.z);
			return false;
		}

		plan.start = np;
		plan.land = NiPoint3(gs.x, gs.y, gz);
		plan.apex = 60.0f;
		plan.durationSec = 0.7f;

		sprintf_s(why, whySize, "plain hop of %.0f units toward the player (ground %+.0f), arc %.0f over %.2f s", d, gz - np.z, plan.apex, plan.durationSec);
		return true;
	}
}
