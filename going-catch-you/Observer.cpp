// Observer.cpp - Going Catch You, step 1: everything the mod has to READ.
//
// WHAT IT DOES
//
// Several times a second it walks the references in the player's cell and, for
// every NPC that is in combat, keeps a short history and writes it to
// GoingCatchYou.log. It changes nothing in the game: not a position, not a
// flag, not an animation.
//
// It exists to settle, before any movement code is written, every question the
// mod will need an answer to:
//
//   WHO HUNTS THE PLAYER    the engine's own IsInCombat (see Engine.h), which
//                           works for people, creatures and horses alike. A
//                           comparison of NPCs in combat against NPCs not in
//                           combat, field by field, then points at the field
//                           where the engine keeps the combat target.
//   WHO IS STUCK            the NPC's own speed, worked out from its positions
//                           (the engine's speed and movement flags proved
//                           unreliable on this edition), and whether it is
//                           standing still far from the player.
//   WHAT STOPS THEM         for a stopped hunter, casts into the physics world:
//                           how high the obstacle in front of it is, whether
//                           the ground ahead drops away, whether the player
//                           can be seen. These are the numbers a jump needs.
//   WHAT THE CONTROLLER SAYS  velocity, fall time and support state, read from
//                           the character controller and checked against the
//                           speed worked out from positions.
//   HOW MUCH IT COSTS       ticks per second and time per sample.
//
// WHAT IT DOES NOT SEE
//
// Only the references of the cell the player is standing in. In an exterior
// that is one cell of the grid; an enemy in the next cell over is invisible.
//
// SAFETY
//
// Every pointer taken from the game is checked before it is followed, and the
// whole sample runs inside a structured exception handler. A fault costs one
// sample, not the game; after too many the observer switches itself off.

#include "Observer.h"
#include "Settings.h"
#include "Engine.h"
#include "Havok.h"
#include "Log.h"

#include "skse/GameForms.h"
#include "skse/GameReferences.h"

#include <windows.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <map>
#include <vector>

namespace
{
	enum { kMaxScan = 8192 };			// references looked at per sample, at most
	enum { kPerfLogMs = 5000 };			// how often the cell summary and the cost line are written
	enum { kMaxFaults = 20 };
	enum { kMaxCastsPerSample = 2 };
	enum { kIdleProbesPerRound = 3 };
	enum { kMaxIdleCandidates = 8 };
	enum { kMaxProbeKeys = 60000 };
	enum { kIdleProbeCap = 40 };			// NPCs out of combat sampled in all; enough to compare against

	// The player's reference handle, as the engine stores it wherever something
	// targets the player. If the comparison below finds a field whose value is
	// this, that field is the combat target.
	const UInt32 kPlayerHandle = 0x00100000;

	// Layer the world casts are made on: the one the player's parkour uses.
	const UInt32 kCastLayer = Havok::kLayer_Transparent;

	struct Track
	{
		UInt32	id;
		UInt32	baseId;
		DWORD	firstMs;
		DWORD	lastMs;
		DWORD	lastSeenMs;
		DWORD	lastStatusMs;
		DWORD	lastProbeMs;
		DWORD	lastCastMs;
		DWORD	stallSinceMs;
		float	lastDist;
		float	closing;			// smoothed, units per second, positive = getting closer
		float	vel;				// own horizontal speed from its positions, smoothed, units per second
		float	velZ;				// own vertical speed, smoothed
		NiPoint3 lastPos;
		float	minDist;
		bool	primed;				// closing and speed have a first real measurement
		UInt32	stalls;
		bool	seen;
		bool	stalled;
	};

	std::vector<Track>	s_tracks;

	DWORD	s_lastSample	= 0;
	NiPoint3 s_playerPos;
	DWORD	s_playerMs		= 0;
	float	s_playerVel		= 0.0f;
	UInt32	s_playerCell	= 0;
	UInt32	s_faults		= 0;
	bool	s_dead			= false;
	bool	s_firstTick		= false;
	bool	s_fullWarned	= false;
	bool	s_sampled		= false;
	bool	s_notReadyLogged = false;
	DWORD	s_firstTickMs	= 0;

	// Cost bookkeeping.
	LARGE_INTEGER	s_qpcFreq;
	DWORD	s_perfWindowMs	= 0;
	UInt32	s_perfTicks		= 0;
	UInt32	s_perfSamples	= 0;
	double	s_perfSumUs		= 0.0;
	double	s_perfMaxUs		= 0.0;

	// Probe statistics.
	struct ProbeCell
	{
		UInt32	combatValue;	// the value the first NPC in combat had in this field
		UInt32	combatSeen;
		UInt32	combatEq;		// ... and how many NPCs in combat had the same value
		UInt32	idleValue;		// the value the first NPC NOT in combat had
		UInt32	idleSeen;
		UInt32	idleEq;			// ... and how many NPCs NOT in combat had that same value
		bool	combatSet;
		bool	idleSet;
	};

	std::map<UInt32, ProbeCell>	s_probeMap;
	UInt32	s_probeCombatSamples	= 0;
	UInt32	s_probeIdleSamples		= 0;
	DWORD	s_lastIdleProbeMs		= 0;
	DWORD	s_lastProbeSummaryMs	= 0;
	UInt32	s_idleCursor			= 0;

	const char * StateName(UInt32 s)
	{
		switch(s)
		{
			case Engine::Controller::kState_OnGround:	return "ground";
			case Engine::Controller::kState_Jumping:	return "jumping";
			case Engine::Controller::kState_InAir:		return "air";
			case Engine::Controller::kState_Climbing:	return "climbing";
			case Engine::Controller::kState_Flying:		return "flying";
			case Engine::Controller::kState_Swimming:	return "swimming";
		}
		return "?";
	}

	// AttackState 7..15 is the bow cycle on this edition too: a tracked archer
	// walked 8, 9, 10, 11, 12, 13 in order while standing still 2000 units away.
	bool IsRanged(UInt32 attack)
	{
		return attack >= 7 && attack <= 15;
	}

	Track * FindTrack(UInt32 id)
	{
		for(size_t i = 0; i < s_tracks.size(); ++i)
			if(s_tracks[i].id == id) return &s_tracks[i];
		return NULL;
	}

	// ------------------------------------------------------------- the probe
	//
	// For every NPC in combat, and for a few NPCs that are not, the words of the
	// actor, of its AI process and of the process's other links are recorded,
	// and so are the words of the objects those point to (a combat controller,
	// say). A field where every NPC in combat holds the same value, and no NPC
	// out of combat does, is where the engine keeps the combat target - and if
	// that value is the player's handle, the target is the player. Nothing is
	// written to the game.

	bool PlausiblePointer(UInt32 v)
	{
		return v >= 0x00100000 && v < 0xFFFF0000 && !(v & 3);
	}

	void ProbeRecord(UInt32 key, UInt32 value, bool combat)
	{
		std::map<UInt32, ProbeCell>::iterator it = s_probeMap.find(key);
		if(it == s_probeMap.end())
		{
			if(s_probeMap.size() >= kMaxProbeKeys) return;
			ProbeCell fresh;
			memset(&fresh, 0, sizeof(fresh));
			it = s_probeMap.insert(std::make_pair(key, fresh)).first;
		}

		ProbeCell & c = it->second;
		if(combat)
		{
			if(!c.combatSet) { c.combatSet = true; c.combatValue = value; }
			++c.combatSeen;
			if(c.combatValue == value) ++c.combatEq;
		}
		else
		{
			if(!c.idleSet) { c.idleSet = true; c.idleValue = value; }
			++c.idleSeen;
			if(c.idleValue == value) ++c.idleEq;
		}
	}

	// key = block (8 bits) | offset in the block (12) | offset in the object it
	// points to (12), or 0xFFF for the word itself.
	void ProbeBlock(const void * base, UInt32 size, UInt32 tag, bool combat)
	{
		if(!Engine::Readable(base, size)) return;

		const UInt32 * w = (const UInt32 *)base;
		const UInt32 count = size / 4;
		for(UInt32 i = 0; i < count; ++i)
		{
			const UInt32 v = w[i];
			const UInt32 outer = (tag << 24) | ((i * 4) << 12);
			ProbeRecord(outer | 0xFFF, v, combat);

			if(!PlausiblePointer(v) || !Engine::Readable((const void *)v, 0x40)) continue;
			const UInt32 * q = (const UInt32 *)v;
			for(UInt32 j = 0; j < 16; ++j)
				ProbeRecord(outer | (j * 4), q[j], combat);
		}
	}

	// a = the actor, p = its AI process, m = the middle process, h = the
	// process's third link. Sizes are guesses; a field past the real end of a
	// block shows up as noise, and the comparison sorts that out.
	void ProbeNpc(Actor * npc, const Engine::Chain & chain, bool combat)
	{
		ProbeBlock(npc, 0x1A0, 1, combat);
		ProbeBlock(chain.process, 0x300, 2, combat);
		ProbeBlock(chain.middle, 0x300, 3, combat);
		void * high = Engine::Link(chain.process, 0x08);
		if(high) ProbeBlock(high, 0x300, 4, combat);

		if(combat) ++s_probeCombatSamples; else ++s_probeIdleSamples;
	}

	struct Row
	{
		UInt32		key;
		float		score;
		ProbeCell	c;
	};

	bool RowBetter(const Row & a, const Row & b) { return a.score > b.score; }

	void ProbeSummary(UInt32 playerPtr)
	{
		// A field qualifies when the NPCs in combat agree on one value there (at
		// least 70% of them) and it is not 0, and either
		//   - that value is the player's handle or pointer: a direct hit, whatever
		//     the NPCs out of combat hold; or
		//   - the NPCs out of combat do not hold it (at most 20% of them).
		// The second rule is approximate: out of combat, only the first value
		// seen and how often it repeats are kept, so "do not hold it" means "hold
		// some other value, or a value that varies".
		std::vector<Row> rows;
		for(std::map<UInt32, ProbeCell>::const_iterator it = s_probeMap.begin(); it != s_probeMap.end(); ++it)
		{
			const ProbeCell & c = it->second;
			if(c.combatSeen < 5 || c.combatValue == 0) continue;

			const float inCombat = (float)c.combatEq / (float)c.combatSeen;
			if(inCombat < 0.7f) continue;

			const bool direct = c.combatValue == kPlayerHandle || c.combatValue == playerPtr;
			float outOfCombat = 0.0f;
			if(c.idleSeen > 0 && c.idleValue == c.combatValue)
				outOfCombat = (float)c.idleEq / (float)c.idleSeen;

			Row row;
			row.key = it->first;
			if(direct)
				row.score = 2.0f + inCombat;
			else
			{
				if(c.idleSeen < 5 || outOfCombat > 0.2f) continue;
				row.score = inCombat - outOfCombat;
			}
			row.c = c;
			rows.push_back(row);
		}

		if(rows.empty())
		{
			_MESSAGE("observer: probe: %u NPCs in combat and %u not in combat sampled, %u fields compared; "
					 "no field separates them yet (it needs at least 5 NPCs in combat)",
					 (unsigned int)s_probeCombatSamples, (unsigned int)s_probeIdleSamples,
					 (unsigned int)s_probeMap.size());
			return;
		}

		std::sort(rows.begin(), rows.end(), RowBetter);
		_MESSAGE("observer: probe: %u in combat / %u not in combat sampled; fields that tell them apart (best first):",
				 (unsigned int)s_probeCombatSamples, (unsigned int)s_probeIdleSamples);

		for(size_t i = 0; i < rows.size() && i < 12; ++i)
		{
			const Row & r = rows[i];
			const UInt32 tag = r.key >> 24;
			const UInt32 outer = (r.key >> 12) & 0xFFF;
			const UInt32 inner = r.key & 0xFFF;

			char where[40];
			if(inner == 0xFFF)
				sprintf_s(where, sizeof(where), "%c+%X", "?apmh"[tag < 5 ? tag : 0], (unsigned int)outer);
			else
				sprintf_s(where, sizeof(where), "%c+%X>+%X", "?apmh"[tag < 5 ? tag : 0], (unsigned int)outer, (unsigned int)inner);

			const char * what = "";
			if(r.c.combatValue == kPlayerHandle)		what = "  <- the player's handle";
			else if(r.c.combatValue == playerPtr)		what = "  <- the player's pointer";

			char idle[64];
			if(r.c.idleSeen == 0)
				strcpy_s(idle, sizeof(idle), "not in combat: field not present");
			else
				sprintf_s(idle, sizeof(idle), "not in combat: %08X in %u/%u", (unsigned int)r.c.idleValue,
						  (unsigned int)r.c.idleEq, (unsigned int)r.c.idleSeen);

			_MESSAGE("observer:   %-14s = %08X  in combat %u/%u, %s%s",
					 where, (unsigned int)r.c.combatValue,
					 (unsigned int)r.c.combatEq, (unsigned int)r.c.combatSeen, idle, what);
		}
	}

	// ----------------------------------------------------------- the world
	//
	// For a hunter that has stopped: what is in its way. Casts toward the
	// player at four heights, two casts down ahead of it, and one from its
	// chest to the player's. All of them on the layer the player's parkour
	// uses, with the NPC's own collision filter so it never hits itself.

	void CastWorld(Actor * npc, UInt32 id, const NiPoint3 & np, const NiPoint3 & pp)
	{
		const float dx = pp.x - np.x, dy = pp.y - np.y;
		const float hlen = sqrtf(dx * dx + dy * dy);
		if(hlen < 1.0f) return;

		const NiPoint3 dir(dx / hlen, dy / hlen, 0.0f);
		const float length = hlen < Settings::castDistance ? hlen : Settings::castDistance;

		// Feet, knee, waist, head. An obstacle that blocks the first two and not
		// the others is something to vault; one that blocks all four is a wall.
		static const float heights[4] = { 25.0f, 70.0f, 120.0f, 170.0f };
		bool blocked[4];
		char wall[160];
		wall[0] = 0;
		int top = -1;

		for(int i = 0; i < 4; ++i)
		{
			const NiPoint3 start(np.x, np.y, np.z + heights[i]);
			const Havok::Result r = Havok::RayCast(start, dir, length, kCastLayer, npc);
			blocked[i] = r.didHit;
			if(r.didHit) top = i;

			char piece[40];
			if(r.didHit)	sprintf_s(piece, sizeof(piece), " %.0f:%.0f(L%u)", heights[i], r.distance, (unsigned int)r.layer);
			else			sprintf_s(piece, sizeof(piece), " %.0f:clear", heights[i]);
			strcat_s(wall, sizeof(wall), piece);
		}

		char verdict[64];
		if(top < 0)			strcpy_s(verdict, sizeof(verdict), "clear ahead");
		else if(top == 3)	strcpy_s(verdict, sizeof(verdict), "wall: blocked up to the head");
		else				sprintf_s(verdict, sizeof(verdict), "obstacle, top between %.0f and %.0f", heights[top], heights[top + 1]);

		// Ground ahead, relative to the NPC's feet: negative is a drop.
		char ground[96];
		ground[0] = 0;
		static const float ahead[2] = { 60.0f, 140.0f };
		for(int i = 0; i < 2; ++i)
		{
			const NiPoint3 start(np.x + dir.x * ahead[i], np.y + dir.y * ahead[i], np.z + 120.0f);
			const NiPoint3 down(0.0f, 0.0f, -1.0f);
			const Havok::Result r = Havok::RayCast(start, down, 700.0f, kCastLayer, npc);

			char piece[48];
			if(r.didHit)	sprintf_s(piece, sizeof(piece), " %.0f:%+.0f", ahead[i], (start.z - r.distance) - np.z);
			else			sprintf_s(piece, sizeof(piece), " %.0f:void", ahead[i]);
			strcat_s(ground, sizeof(ground), piece);
		}

		// Can it see the player? Chest to chest.
		const NiPoint3 a(np.x, np.y, np.z + 110.0f);
		const NiPoint3 b(pp.x, pp.y, pp.z + 110.0f);
		const float lx = b.x - a.x, ly = b.y - a.y, lz = b.z - a.z;
		const float llen = sqrtf(lx * lx + ly * ly + lz * lz);
		char sight[64];
		if(llen < 1.0f)
			strcpy_s(sight, sizeof(sight), "adjacent");
		else
		{
			const NiPoint3 d3(lx / llen, ly / llen, lz / llen);
			const Havok::Result r = Havok::RayCast(a, d3, llen, kCastLayer, npc);
			if(r.didHit)	sprintf_s(sight, sizeof(sight), "blocked at %.0f of %.0f (L%u)", r.distance, llen, (unsigned int)r.layer);
			else			strcpy_s(sight, sizeof(sight), "visible");
		}

		_MESSAGE("observer: world %08X toward the player (%.0f units): [%s ] -> %s; ground ahead [%s ]; line of sight: %s",
				 (unsigned int)id, hlen, wall, verdict, ground, sight);
	}

	// -------------------------------------------------- isolated risky reads
	//
	// Three things the sample does go deep into the game: the controller's
	// velocity, a world cast, and the probe. Each runs behind its own handler,
	// so a fault in one costs that one reading, not the whole sample - and
	// after three faults that one feature is switched off for the session. (In
	// version 0.2.0 a single fault in the velocity read killed every sample and
	// the plugin gave up after 20.)

	bool	s_ctrlOk	= true;
	bool	s_castOk	= true;
	bool	s_probeOk	= true;
	UInt32	s_ctrlFaults	= 0;
	UInt32	s_castFaults	= 0;
	UInt32	s_probeFaults	= 0;

	// What the character controller reports: velocity, fall time, support state.
	struct CtrlRead
	{
		float	v[4];
		float	fall;
		UInt32	support;
	};

	bool GuardedControllerRead(void * ctrl, CtrlRead * out, bool * faulted)
	{
		*faulted = false;
		__try
		{
			if(!Engine::ControllerGetLinearVelocity(ctrl, out->v)) return false;
			out->fall = Engine::ControllerFallTime(ctrl);
			out->support = Engine::ControllerSupport(ctrl);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			*faulted = true;
			return false;
		}
	}

	bool GuardedCastWorld(Actor * npc, UInt32 id, const NiPoint3 & np, const NiPoint3 & pp)
	{
		__try
		{
			CastWorld(npc, id, np, pp);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool GuardedProbeNpc(Actor * npc, const Engine::Chain & chain, bool combat)
	{
		__try
		{
			ProbeNpc(npc, chain, combat);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	// Counts a fault and switches the feature off when it keeps happening.
	void NoteFault(const char * what, UInt32 & faults, bool & ok)
	{
		++faults;
		_ERROR("observer: %s faulted (%u so far); skipped it", what, (unsigned int)faults);
		if(faults >= 3)
		{
			ok = false;
			_ERROR("observer: %s switched off for the rest of the session", what);
		}
	}

	// ----------------------------------------------------------- the sample

	// One sample of the world. Kept out of line, and free of the structured
	// exception handler, because a function that has one may not also hold
	// objects that need unwinding.
	__declspec(noinline) void SampleOnce(Actor * player, DWORD now)
	{
		const NiPoint3 pp = Engine::Position(player);

		TESObjectCELL * cell = Engine::ParentCell(player);
		if(!cell || !Engine::Readable(cell, 0x80))
			return;

		// The player's own speed, so "it is not getting closer" can be told from
		// "the player is running away".
		if(cell->formID != s_playerCell)
		{
			s_playerCell = cell->formID;	// a cell change is a jump, not a speed
			s_playerMs = 0;
		}
		if(s_playerMs && now > s_playerMs)
		{
			const float pdx = pp.x - s_playerPos.x, pdy = pp.y - s_playerPos.y;
			const float pv = sqrtf(pdx * pdx + pdy * pdy) / ((float)(now - s_playerMs) / 1000.0f);
			s_playerVel = 0.5f * s_playerVel + 0.5f * pv;
		}
		s_playerPos = pp;
		s_playerMs = now;

		for(size_t i = 0; i < s_tracks.size(); ++i)
			s_tracks[i].seen = false;

		const float inv = Engine::HavokScaleInverse();
		UInt32 actors = 0, inRange = 0, watched = 0, inCombatCount = 0;
		UInt32 casts = 0;
		const bool probeDue = Settings::probe && s_probeIdleSamples < kIdleProbeCap &&
							  (now - s_lastIdleProbeMs >= Settings::probeMs);
		Actor * idle[kMaxIdleCandidates];
		UInt32 idleCount = 0;

		const UInt32 count = cell->objectList.count;
		const UInt32 limit = count < kMaxScan ? count : kMaxScan;

		// ------------------------------------------------------------ scan
		for(UInt32 i = 0; i < limit; ++i)
		{
			TESObjectREFR * ref = NULL;
			if(!cell->objectList.GetNthItem(i, ref)) break;
			if(!ref || ref == (TESObjectREFR *)player) continue;
			if(!Engine::Readable(ref, 0x140)) continue;
			if(ref->formType != kFormType_Character) continue;

			Actor * npc = (Actor *)ref;
			++actors;

			const NiPoint3 np = Engine::Position(npc);
			const float dx = np.x - pp.x, dy = np.y - pp.y, dz = np.z - pp.z;
			const float hdist = sqrtf(dx * dx + dy * dy);
			const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
			if(dist > Settings::radius) continue;
			++inRange;

			if(Engine::LifeState(npc) != Engine::kLifeState_Alive) continue;

			Engine::Chain chain;
			if(!Engine::ActorChain(npc, chain)) continue;
			if(!Engine::ActorIsBuilt(npc)) continue;

			const bool drawn = Engine::WeaponState(npc) == Engine::kWeaponState_Drawn;
			const bool combat = Engine::IsInCombat(npc);
			if(combat) ++inCombatCount;

			const bool watch = combat || Settings::allActors || (Settings::includeDrawn && drawn);
			if(!watch)
			{
				if(probeDue && idleCount < kMaxIdleCandidates) idle[idleCount++] = npc;
				continue;
			}

			++watched;

			// ------------------------------------------------------ track
			const UInt32 id = ref->formID;
			Track * t = FindTrack(id);
			if(!t)
			{
				if(s_tracks.size() >= Settings::maxTracks)
				{
					if(!s_fullWarned)
					{
						s_fullWarned = true;
						_MESSAGE("observer: already watching %u NPCs; the rest are ignored (iMaxTracks)",
								 (unsigned int)Settings::maxTracks);
					}
					continue;
				}

				Track fresh;
				memset(&fresh, 0, sizeof(fresh));
				fresh.id			= id;
				fresh.baseId		= (ref->baseForm && Engine::Readable(ref->baseForm, 0x20)) ? ref->baseForm->formID : 0;
				fresh.firstMs		= now;
				fresh.lastMs		= now;
				fresh.lastSeenMs	= now;
				fresh.lastDist		= dist;
				fresh.minDist		= dist;
				fresh.lastPos		= np;
				s_tracks.push_back(fresh);
				t = &s_tracks.back();

				_MESSAGE("observer: + %08X (base %08X) appeared at %.0f units (h %.0f, dz %+.0f), in combat %d, weapon %s",
						 (unsigned int)id, (unsigned int)t->baseId, dist, hdist, dz, combat ? 1 : 0, drawn ? "drawn" : "away");
			}
			t->seen = true;
			t->lastSeenMs = now;

			const float dt = (float)(now - t->lastMs) / 1000.0f;
			if(dt > 3.0f)
			{
				// Seen again after a gap: the old position says nothing about speed.
				t->primed = false;
				t->stalled = false;
				t->stallSinceMs = 0;
				t->lastDist = dist;
				t->lastPos = np;
				t->lastMs = now;
			}
			else if(dt > 0.01f)
			{
				const float closing = (t->lastDist - dist) / dt;
				const float mx = np.x - t->lastPos.x, my = np.y - t->lastPos.y, mz = np.z - t->lastPos.z;
				const float vel = sqrtf(mx * mx + my * my) / dt;
				const float velZ = mz / dt;

				t->closing = t->primed ? 0.5f * t->closing + 0.5f * closing : closing;
				t->vel = t->primed ? 0.5f * t->vel + 0.5f * vel : vel;
				t->velZ = t->primed ? 0.5f * t->velZ + 0.5f * velZ : velZ;
				t->primed = true;
				t->lastDist = dist;
				t->lastPos = np;
				t->lastMs = now;
			}
			if(dist < t->minDist) t->minDist = dist;

			const UInt32 f1 = Engine::ActorFlags1(npc);
			const UInt32 f2 = Engine::ActorFlags2(npc);
			const UInt32 state = Engine::ControllerState(chain.controller);
			const UInt32 atk = Engine::AttackState(npc);
			const bool ranged = IsRanged(atk);

			// ----------------------------------------------------- stopped
			// The NPC itself has (nearly) stopped, it is far from the player and
			// it is not shooting. This is the case that matters: a hunter that
			// has run out of path. An NPC that is moving but not getting closer
			// is the player running away, and is not an event.
			const bool stoppedNow = t->primed && t->vel < Settings::stoppedSpeed && !ranged;
			const bool farAway = dist > Settings::stallMinDistance;
			if(stoppedNow && farAway && combat)
			{
				if(!t->stallSinceMs) t->stallSinceMs = now;
				if(!t->stalled && now - t->stallSinceMs >= Settings::stallMs)
				{
					t->stalled = true;
					++t->stalls;
					_MESSAGE("observer: STOPPED %08X for %.1f s, %.0f units from the player "
							 "(h %.0f, dz %+.0f), own speed %.0f u/s (vz %+.0f), player %.0f u/s, %s, "
							 "f1=%08X f2=%08X attack %u",
							 (unsigned int)id, (float)(now - t->stallSinceMs) / 1000.0f, dist, hdist, dz,
							 t->vel, t->velZ, s_playerVel, StateName(state),
							 (unsigned int)f1, (unsigned int)f2, (unsigned int)atk);
				}
			}
			else
			{
				if(t->stalled)
					_MESSAGE("observer: moving again %08X after %.1f s: %.0f units from the player, own speed %.0f u/s",
							 (unsigned int)id, (float)(now - t->stallSinceMs) / 1000.0f, dist, t->vel);
				t->stalled = false;
				t->stallSinceMs = 0;
			}

			// ----------------------------------------------------- world
			if(Settings::cast && s_castOk && combat && !ranged && farAway && t->primed &&
			   t->vel < Settings::stoppedSpeed * 3.0f &&
			   now - t->lastCastMs >= Settings::castMs && casts < kMaxCastsPerSample)
			{
				t->lastCastMs = now;
				++casts;
				if(!GuardedCastWorld(npc, id, np, pp))
					NoteFault("the world cast", s_castFaults, s_castOk);
			}

			// ----------------------------------------------------- status
			if(now - t->lastStatusMs >= Settings::statusMs)
			{
				t->lastStatusMs = now;

				// What the character controller itself says, to be compared with
				// the speed worked out from the positions.
				char ctrl[96];
				CtrlRead cr;
				memset(&cr, 0, sizeof(cr));
				bool velFaulted = false;
				if(s_ctrlOk && GuardedControllerRead(chain.controller, &cr, &velFaulted))
					sprintf_s(ctrl, sizeof(ctrl), "ctrl %.0f u/s (vz %+.0f), fall %.2f s, support %u",
							  sqrtf(cr.v[0] * cr.v[0] + cr.v[1] * cr.v[1]) * inv, cr.v[2] * inv,
							  cr.fall, (unsigned int)cr.support);
				else
				{
					if(velFaulted) NoteFault("the controller read", s_ctrlFaults, s_ctrlOk);
					strcpy_s(ctrl, sizeof(ctrl), "ctrl n/a");
				}

				_MESSAGE("observer: track %08X (base %08X) %.0f units (h %.0f, dz %+.0f) closing %+.0f u/s, own speed %.0f u/s "
						 "(vz %+.0f), %s, player %.0f u/s, %s, f1=%08X f2=%08X attack %u%s combat %d weapon %s%s",
						 (unsigned int)id, (unsigned int)t->baseId, dist, hdist, dz, t->closing, t->vel, t->velZ,
						 ctrl, s_playerVel, StateName(state), (unsigned int)f1, (unsigned int)f2,
						 (unsigned int)atk, ranged ? " (ranged)" : "", combat ? 1 : 0, drawn ? "drawn" : "away",
						 t->stalled ? " STOPPED" : "");
			}

			// ------------------------------------------------------ probe
			if(Settings::probe && s_probeOk && combat && now - t->lastProbeMs >= Settings::probeMs)
			{
				t->lastProbeMs = now;
				if(!GuardedProbeNpc(npc, chain, true))
					NoteFault("the probe", s_probeFaults, s_probeOk);
			}
		}

		// ---------------------------------------------- NPCs not in combat
		// A few of them, for the probe to compare against.
		if(probeDue)
		{
			s_lastIdleProbeMs = now;
			for(UInt32 k = 0; k < idleCount && k < kIdleProbesPerRound; ++k)
			{
				Actor * npc = idle[(s_idleCursor + k) % idleCount];
				Engine::Chain chain;
				if(s_probeOk && Engine::ActorChain(npc, chain) && !GuardedProbeNpc(npc, chain, false))
					NoteFault("the probe", s_probeFaults, s_probeOk);
			}
			s_idleCursor += kIdleProbesPerRound;
		}

		// ----------------------------------------------------------- leavers
		// An NPC that stops qualifying (out of combat for a moment, a step out
		// of range) is kept for iKeepMs before it is written off.
		for(size_t i = 0; i < s_tracks.size(); )
		{
			const Track & t = s_tracks[i];
			if(t.seen || now - t.lastSeenMs <= Settings::keepMs) { ++i; continue; }

			_MESSAGE("observer: - %08X left after %.1f s, closest %.0f units, %u stop(s)",
					 (unsigned int)t.id, (float)(t.lastSeenMs - t.firstMs) / 1000.0f, t.minDist, (unsigned int)t.stalls);
			s_tracks.erase(s_tracks.begin() + i);
		}
		if(s_tracks.size() < Settings::maxTracks) s_fullWarned = false;

		// ----------------------------------------------------------- summary
		if(s_perfWindowMs == 0) s_perfWindowMs = now;
		if(now - s_perfWindowMs >= kPerfLogMs)
		{
			const float seconds = (float)(now - s_perfWindowMs) / 1000.0f;
			_MESSAGE("observer: cell %08X: %u references, %u NPCs, %u within %.0f units, %u in combat, %u watched; "
					 "player in combat %d; heartbeat %.0f ticks/s, %u samples, %.0f us each on average, %.0f us at most",
					 (unsigned int)cell->formID, (unsigned int)count, (unsigned int)actors,
					 (unsigned int)inRange, Settings::radius, (unsigned int)inCombatCount, (unsigned int)watched,
					 Engine::IsInCombat(player) ? 1 : 0,
					 seconds > 0.0f ? (float)s_perfTicks / seconds : 0.0f, (unsigned int)s_perfSamples,
					 s_perfSamples ? s_perfSumUs / s_perfSamples : 0.0, s_perfMaxUs);

			s_perfWindowMs = now;
			s_perfTicks = s_perfSamples = 0;
			s_perfSumUs = s_perfMaxUs = 0.0;
		}

		if(Settings::probe && now - s_lastProbeSummaryMs >= Settings::summaryMs)
		{
			if(s_lastProbeSummaryMs) ProbeSummary((UInt32)player);
			s_lastProbeSummaryMs = now;
		}
	}

	// The handler. No objects with destructors in here, so the compiler allows
	// the __try.
	bool GuardedSample(Actor * player, DWORD now)
	{
		__try
		{
			SampleOnce(player, now);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

namespace Observer
{
	void Reset()
	{
		s_tracks.clear();
		s_lastSample	= 0;
		s_perfWindowMs	= 0;
		s_fullWarned	= false;
		s_sampled		= false;
		s_notReadyLogged = false;
		s_firstTickMs	= GetTickCount();
		Engine::ForgetMemoryLayout();
	}

	void OnTick()
	{
		if(s_dead || !Settings::enabled) return;

		const DWORD now = GetTickCount();

		if(!s_firstTick)
		{
			s_firstTick = true;
			s_firstTickMs = now;
			QueryPerformanceFrequency(&s_qpcFreq);
			_MESSAGE("heartbeat: the first tick arrived - the plugin is alive");
		}
		++s_perfTicks;

		if(now - s_lastSample < Settings::sampleMs) return;
		s_lastSample = now;

		Actor * player = (Actor *)Engine::Player();
		if(!player || !Engine::ActorIsBuilt(player))
		{
			// Ticks arrive but there is nothing to look at yet. Said once, so a
			// silent log can be told apart from a plugin that never ticked.
			if(!s_notReadyLogged && now - s_firstTickMs > 5000)
			{
				s_notReadyLogged = true;
				_MESSAGE("observer: ticks arrive, but the player is not ready (player %p, readable %d); "
						 "waiting", (void *)player, Engine::Readable(player, 0x140) ? 1 : 0);
			}
			return;
		}

		if(!s_sampled)
		{
			s_sampled = true;
			_MESSAGE("observer: the first sample - the player is ready, looking around");
		}

		LARGE_INTEGER t0, t1;
		QueryPerformanceCounter(&t0);
		const bool ok = GuardedSample(player, now);
		QueryPerformanceCounter(&t1);

		if(s_qpcFreq.QuadPart)
		{
			const double us = (double)(t1.QuadPart - t0.QuadPart) * 1000000.0 / (double)s_qpcFreq.QuadPart;
			++s_perfSamples;
			s_perfSumUs += us;
			if(us > s_perfMaxUs) s_perfMaxUs = us;
		}

		if(!ok)
		{
			++s_faults;
			_ERROR("observer: a sample faulted on a bad pointer (%u so far); skipped it", (unsigned int)s_faults);
			s_tracks.clear();
			Engine::ForgetMemoryLayout();

			if(s_faults >= kMaxFaults)
			{
				s_dead = true;
				_ERROR("observer: too many faults - switched off for the rest of the session");
			}
		}
	}
}
