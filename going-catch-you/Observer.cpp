// Observer.cpp - step 1 of Going Catch You: look, and write down what was seen.
//
// WHAT IT DOES
//
// Several times a second it walks the references in the player's cell, picks
// out the NPCs that look like they are hunting the player, and keeps a short
// history of each: how far away it is, how fast it is really moving (worked out from
// its own positions - the engine's speed and movement flags proved unreliable on
// this edition), how fast the gap is closing, and whether it is on the ground
// or in the air. The raw flag words are written in hex so they can be decoded
// against what the NPC was seen doing. From that it
// writes three kinds of line to GoingCatchYou.log:
//
//   appeared / left   an NPC started or stopped being watched
//   track             a status line per watched NPC every iStatusMs
//   STOPPED / moving again   an NPC that has itself stopped although it is far away
//
// It changes nothing in the game. Not a position, not a flag, not an animation.
//
// WHAT "HUNTING THE PLAYER" MEANS HERE, AND WHY IT IS A GUESS
//
// The address of an actor's combat target has not been found on this edition,
// so the question "is this NPC in combat with the player" cannot be asked
// directly yet. Until it can, a watched NPC is one that is alive, has a built
// character controller, is within fRadius of the player and has a weapon drawn
// (NPCs only draw one for a fight). That is wrong in both directions - a
// follower fighting beside the player qualifies, a hunter with a sheathed
// weapon does not - and the log carries enough to see how often. Set
// bAllActors=1 to watch every NPC nearby and compare.
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
#include "Log.h"

#include "skse/GameForms.h"
#include "skse/GameReferences.h"

#include <windows.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <vector>

namespace
{
	enum { kMaxScan = 8192 };			// references looked at per sample, at most
	enum { kScanLogMs = 5000 };			// how often the one-line cell summary is written
	enum { kMaxFaults = 20 };

	struct Track
	{
		UInt32	id;
		DWORD	firstMs;
		DWORD	lastMs;
		DWORD	lastStatusMs;
		DWORD	stallSinceMs;
		float	lastDist;
		float	closing;			// smoothed, units per second, positive = getting closer
		float	vel;				// own horizontal speed from its positions, smoothed, units per second
		float	velZ;				// own vertical speed, smoothed
		NiPoint3 lastPos;
		float	minDist;
		bool	primed;				// closing has a first real measurement
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
	DWORD	s_lastScanLog	= 0;
	UInt32	s_faults		= 0;
	bool	s_dead			= false;
	bool	s_firstTick		= false;
	bool	s_fullWarned	= false;
	bool	s_sampled		= false;
	bool	s_notReadyLogged = false;
	DWORD	s_firstTickMs	= 0;

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

	Track * FindTrack(UInt32 id)
	{
		for(size_t i = 0; i < s_tracks.size(); ++i)
			if(s_tracks[i].id == id) return &s_tracks[i];
		return NULL;
	}

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

		UInt32 actors = 0, inRange = 0, watched = 0;
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

			const bool drawn = Engine::WeaponState(npc) == Engine::kWeaponState_Drawn;
			if(!drawn && !Settings::allActors) continue;

			Engine::Chain chain;
			if(!Engine::ActorChain(npc, chain)) continue;
			if(!Engine::ActorIsBuilt(npc)) continue;

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
				fresh.firstMs		= now;
				fresh.lastMs		= now;
				fresh.lastStatusMs	= 0;
				fresh.lastDist		= dist;
				fresh.minDist		= dist;
				fresh.lastPos		= np;
				s_tracks.push_back(fresh);
				t = &s_tracks.back();

				_MESSAGE("observer: + %08X appeared at %.0f units (h %.0f, dz %+.0f), weapon %s",
						 (unsigned int)id, dist, hdist, dz, drawn ? "drawn" : "away");
			}
			t->seen = true;

			const float dt = (float)(now - t->lastMs) / 1000.0f;
			if(dt > 0.01f)
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

			// ----------------------------------------------------- stopped
			// The NPC itself has (nearly) stopped, and it is far from the
			// player. This is the case that matters: a hunter that has run out
			// of path. An NPC that is moving but not getting closer is the player
			// running away, and is not an event.
			const bool stoppedNow = t->primed && t->vel < Settings::stoppedSpeed;
			const bool farAway = dist > Settings::stallMinDistance;
			if(stoppedNow && farAway)
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
							 (unsigned int)f1, (unsigned int)f2, (unsigned int)Engine::AttackState(npc));
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

			// ----------------------------------------------------- status
			if(now - t->lastStatusMs >= Settings::statusMs)
			{
				t->lastStatusMs = now;
				_MESSAGE("observer: track %08X %.0f units (h %.0f, dz %+.0f) closing %+.0f u/s, own speed %.0f u/s "
						 "(vz %+.0f), player %.0f u/s, %s, f1=%08X f2=%08X attack %u weapon %s%s",
						 (unsigned int)id, dist, hdist, dz, t->closing, t->vel, t->velZ, s_playerVel,
						 StateName(state), (unsigned int)f1, (unsigned int)f2,
						 (unsigned int)Engine::AttackState(npc), drawn ? "drawn" : "away",
						 t->stalled ? " STOPPED" : "");
			}
		}

		// ----------------------------------------------------------- leavers
		for(size_t i = 0; i < s_tracks.size(); )
		{
			if(s_tracks[i].seen) { ++i; continue; }

			const Track & t = s_tracks[i];
			_MESSAGE("observer: - %08X left after %.1f s, closest %.0f units, %u stall(s)",
					 (unsigned int)t.id, (float)(now - t.firstMs) / 1000.0f, t.minDist, (unsigned int)t.stalls);
			s_tracks.erase(s_tracks.begin() + i);
		}
		if(s_tracks.size() < Settings::maxTracks) s_fullWarned = false;

		// ----------------------------------------------------------- summary
		if(now - s_lastScanLog >= kScanLogMs)
		{
			s_lastScanLog = now;
			_MESSAGE("observer: cell %08X: %u references, %u NPCs, %u within %.0f units, %u watched",
					 (unsigned int)cell->formID, (unsigned int)count, (unsigned int)actors,
					 (unsigned int)inRange, Settings::radius, (unsigned int)watched);
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
		s_lastScanLog	= 0;
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
			_MESSAGE("heartbeat: the first tick arrived - the plugin is alive");
		}

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

		if(!GuardedSample(player, now))
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
