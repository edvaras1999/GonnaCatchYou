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

	namespace
	{
		// Ground an NPC can be put down on: static geometry, animated static,
		// terrain, ground. Anything else (clutter, props, trees, water, triggers)
		// is not trusted to hold a landing.
		bool Walkable(UInt32 layer)
		{
			return layer == 1 || layer == 2 || layer == 13 || layer == 17;
		}

		// A ballistic arc from the start to a point `dh` away horizontally and `dz`
		// higher, rising `extra` above the higher of the two. Returns the vertical
		// speed, the time of flight and the horizontal speed.
		void Ballistic(float dz, float dh, float extra, float g, float & vz0, float & T, float & vh)
		{
			const float apex = (dz > 0.0f ? dz : 0.0f) + extra;
			vz0 = sqrtf(2.0f * g * apex);
			const float up = vz0 / g;
			float fall = apex - dz;
			if(fall < 0.0f) fall = 0.0f;
			const float down = sqrtf(2.0f * fall / g);
			T = up + down;
			vh = T > 0.01f ? dh / T : 0.0f;
		}

		// Is anything in the way of the arc? Cast along it at the feet, the torso and
		// the head. The feet are left out of the first two and last two segments,
		// where they are close to the ground and the ground is not "in the way".
		bool ArcBlocked(Actor * npc, const NiPoint3 & np, float ux, float uy, float vh, float vz0, float g, float T)
		{
			static const float heights[3] = { 25.0f, 70.0f, 140.0f };
			const int steps = 12;
			for(int h = 0; h < 3; ++h)
			{
				NiPoint3 prev(np.x, np.y, np.z + heights[h]);
				for(int i = 1; i <= steps; ++i)
				{
					const float t = T * (float)i / (float)steps;
					const NiPoint3 p(np.x + ux * vh * t, np.y + uy * vh * t, np.z + vz0 * t - 0.5f * g * t * t + heights[h]);
					const bool skip = (h == 0) && (i <= 2 || i > steps - 2);
					const float sx = p.x - prev.x, sy = p.y - prev.y, sz = p.z - prev.z;
					const float len = sqrtf(sx * sx + sy * sy + sz * sz);
					if(!skip && len > 1.0f)
					{
						const NiPoint3 d(sx / len, sy / len, sz / len);
						if(CastSolid(prev, d, len, npc).hit) return true;
					}
					prev = p;
				}
			}
			return false;
		}
	}

	bool PlanLeap(Actor * npc, const NiPoint3 & np, const NiPoint3 & pp, const NiPoint3 * crowd, UInt32 crowdCount,
				  Jump::Plan & plan, char * why, size_t whySize)
	{
		why[0] = 0;

		const float dx = pp.x - np.x, dy = pp.y - np.y;
		const float hlen = sqrtf(dx * dx + dy * dy);
		if(hlen < 1.0f) { strcpy_s(why, whySize, "on top of the player"); return false; }

		const float dirx = dx / hlen, diry = dy / hlen;
		const float sidex = -diry, sidey = dirx;

		// How far along the line to the player a landing is tried: first where it
		// would be `leapLandNear` short of him, then a little farther (a platform
		// he stands on may start only there), then nearer the NPC. Never closer to
		// the player than 90, never farther than a leap goes.
		float dMax = hlen - 90.0f;
		if(dMax > Settings::leapMaxDistance) dMax = Settings::leapMaxDistance;
		float d0 = hlen - Settings::leapLandNear;
		if(d0 > Settings::leapMaxDistance) d0 = Settings::leapMaxDistance;

		float distances[6];
		int nDistances = 0;
		const float offsets[6] = { 0.0f, 50.0f, 100.0f, -100.0f, -200.0f, -300.0f };
		for(int i = 0; i < 6; ++i)
		{
			const float d = d0 + offsets[i];
			if(d >= 120.0f && d <= dMax) distances[nDistances++] = d;
		}
		if(nDistances == 0)
		{
			sprintf_s(why, whySize, "the player is %.0f away, already within reach", hlen);
			return false;
		}

		UInt32 noGround = 0, badLayer = 0, notLevel = 0, outOfReach = 0, notFlat = 0, overhead = 0, crowded = 0, tooNear = 0, tooFast = 0, blocked = 0;
		UInt32 firstBadLayer = 0;

		static const float lateral[3] = { 0.0f, 80.0f, -80.0f };
		const NiPoint3 down(0.0f, 0.0f, -1.0f);
		const NiPoint3 up(0.0f, 0.0f, 1.0f);

		for(int b = 0; b < nDistances; ++b)
		{
			const float d = distances[b];

			for(int l = 0; l < 3; ++l)
			{
				const float lx = np.x + dirx * d + sidex * lateral[l];
				const float ly = np.y + diry * d + sidey * lateral[l];

				// the ground there: the first solid thing down from above both of them
				const float startZ = (np.z > pp.z ? np.z : pp.z) + 180.0f;
				const Solid g = CastSolid(NiPoint3(lx, ly, startZ), down, 1200.0f, npc);
				if(!g.hit) { ++noGround; continue; }
				if(!Walkable(g.layer)) { ++badLayer; if(!firstBadLayer) firstBadLayer = g.layer; continue; }
				const float gz = startZ - g.distance;

				if(fabsf(gz - pp.z) > 120.0f) { ++notLevel; continue; }
				const float rise = gz - np.z;
				if(rise > Settings::leapMaxRise || -rise > Settings::leapMaxDrop) { ++outOfReach; continue; }

				// close to the player, but not on him
				const float px = lx - pp.x, py = ly - pp.y;
				if(sqrtf(px * px + py * py) < 90.0f) { ++tooNear; continue; }

				// flat: four more casts around it come down at the same height
				static const float around[4][2] = { { 35.0f, 0.0f }, { -35.0f, 0.0f }, { 0.0f, 35.0f }, { 0.0f, -35.0f } };
				bool flat = true;
				for(int k = 0; k < 4 && flat; ++k)
				{
					const Solid f = CastSolid(NiPoint3(lx + around[k][0], ly + around[k][1], gz + 60.0f), down, 160.0f, npc);
					if(!f.hit || !Walkable(f.layer) || fabsf((gz + 60.0f - f.distance) - gz) > 25.0f) flat = false;
				}
				if(!flat) { ++notFlat; continue; }

				if(CastSolid(NiPoint3(lx, ly, gz + 10.0f), up, 190.0f, npc).hit) { ++overhead; continue; }

				bool busy = false;
				for(UInt32 c = 0; c < crowdCount && !busy; ++c)
				{
					const float cx = crowd[c].x - lx, cy = crowd[c].y - ly;
					if(sqrtf(cx * cx + cy * cy) < 110.0f && fabsf(crowd[c].z - gz) < 150.0f) busy = true;
				}
				if(busy) { ++crowded; continue; }

				// the arc: raised until nothing is in its way
				const float ex = lx - np.x, ey = ly - np.y;
				const float dh = sqrtf(ex * ex + ey * ey);
				const float ux = ex / dh, uy = ey / dh;
				const float dz = gz - np.z;

				static const float extras[4] = { 55.0f, 110.0f, 170.0f, 240.0f };
				for(int e = 0; e < 4; ++e)
				{
					float vz0, T, vh;
					Ballistic(dz, dh, extras[e], Settings::leapGravity, vz0, T, vh);
					if(vh > Settings::leapMaxSpeed || T > 1.9f) { ++tooFast; continue; }
					if(ArcBlocked(npc, np, ux, uy, vh, vz0, Settings::leapGravity, T)) { ++blocked; continue; }

					plan.start = np;
					plan.land = NiPoint3(lx, ly, gz);
					plan.dir = NiPoint3(ux, uy, 0.0f);
					plan.horizontalSpeed = vh;
					plan.verticalSpeed = vz0;
					plan.gravity = Settings::leapGravity;
					plan.durationSec = T;

					sprintf_s(why, whySize, "lands %.0f ahead (%+.0f sideways), %.0f from the player, ground %+.0f from the feet; "
							  "%.0f u/s along, %.0f u/s up, %.2f s",
							  dh, lateral[l], sqrtf(px * px + py * py), dz, vh, vz0, T);
					return true;
				}
			}
		}

		sprintf_s(why, whySize, "no safe landing: no ground %u, ground of another kind %u (first layer %u), not level with the player %u, "
				  "out of reach %u, too near the player %u, not flat %u, overhead %u, crowded %u, too fast %u, arc blocked %u",
				  (unsigned int)noGround, (unsigned int)badLayer, (unsigned int)firstBadLayer, (unsigned int)notLevel,
				  (unsigned int)outOfReach, (unsigned int)tooNear, (unsigned int)notFlat, (unsigned int)overhead,
				  (unsigned int)crowded, (unsigned int)tooFast, (unsigned int)blocked);
		return false;
	}
}
