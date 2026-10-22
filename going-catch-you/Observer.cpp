// Observer.cpp - step 1 of Going Catch You: look, and write down what was seen.
//
// WHAT IT DOES
//
// Several times a second it walks the references in the player's cell, picks
// out the NPCs that look like they are hunting the player, and keeps a short
// history of each: how far away it is, how fast the gap is closing, whether it
// is on the ground or in the air, whether it is still moving. From that it
// writes three kinds of line to GoingCatchYou.log:
//
//   appeared / left   an NPC started or stopped being watched
//   track             a status line per watched NPC every iStatusMs
//   STALL / resumed   an NPC that is not getting closer although it is far away
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
		float	minDist;
		bool	primed;				// closing has a first real measurement
		UInt32	stalls;
		bool	seen;
		bool	stalled;
	};

	std::vector<Track>	s_tracks;

	DWORD	s_lastSample	= 0;
	DWORD	s_lastScanLog	= 0;
	UInt32	s_faults		= 0;
	bool	s_dead			= false;
	bool	s_firstTick		= false;
	bool	s_fullWarned	= false;

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

	void FlagsText(UInt32 f1, char * out, size_t size)
	{
		out[0] = 0;
		if(f1 & Engine::kFlag1_MovingForward)	strcat_s(out, size, "fwd ");
		if(f1 & Engine::kFlag1_MovingBack)		strcat_s(out, size, "back ");
		if(f1 & Engine::kFlag1_MovingLeft)		strcat_s(out, size, "left ");
		if(f1 & Engine::kFlag1_MovingRight)		strcat_s(out, size, "right ");
		if(f1 & Engine::kFlag1_Walking)			strcat_s(out, size, "walk ");
		if(f1 & Engine::kFlag1_Running)			strcat_s(out, size, "run ");
		if(f1 & Engine::kFlag1_Sprinting)		strcat_s(out, size, "sprint ");
		if(f1 & Engine::kFlag1_Sneaking)		strcat_s(out, size, "sneak ");
		if(f1 & Engine::kFlag1_Swimming)		strcat_s(out, size, "swim ");
		if(!out[0])								strcat_s(out, size, "-");
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
				t->closing = t->primed ? 0.5f * t->closing + 0.5f * closing : closing;
				t->primed = true;
				t->lastDist = dist;
				t->lastMs = now;
			}
			if(dist < t->minDist) t->minDist = dist;

			const float speed = Engine::MovementSpeed(npc);
			const UInt32 f1 = Engine::ActorFlags1(npc);
			const UInt32 state = Engine::ControllerState(chain.controller);
			char flags[96];
			FlagsText(f1, flags, sizeof(flags));

			// ------------------------------------------------------ stall
			const bool slow = t->primed && t->closing < Settings::stallClosing;
			const bool farAway = dist > Settings::stallMinDistance;
			if(slow && farAway)
			{
				if(!t->stallSinceMs) t->stallSinceMs = now;
				if(!t->stalled && now - t->stallSinceMs >= Settings::stallMs)
				{
					t->stalled = true;
					++t->stalls;
					_MESSAGE("observer: STALL %08X not getting closer for %.1f s: %.0f units away "
							 "(h %.0f, dz %+.0f), closing %+.0f u/s, speed %.0f, %s, moving [%s], attack %u",
							 (unsigned int)id, (float)(now - t->stallSinceMs) / 1000.0f, dist, hdist, dz,
							 t->closing, speed, StateName(state), flags, (unsigned int)Engine::AttackState(npc));
				}
			}
			else
			{
				if(t->stalled)
					_MESSAGE("observer: resumed %08X after %.1f s: %.0f units away, closing %+.0f u/s",
							 (unsigned int)id, (float)(now - t->stallSinceMs) / 1000.0f, dist, t->closing);
				t->stalled = false;
				t->stallSinceMs = 0;
			}

			// ----------------------------------------------------- status
			if(now - t->lastStatusMs >= Settings::statusMs)
			{
				t->lastStatusMs = now;
				_MESSAGE("observer: track %08X %.0f units (h %.0f, dz %+.0f) closing %+.0f u/s speed %.0f "
						 "%s moving [%s] attack %u weapon %s%s",
						 (unsigned int)id, dist, hdist, dz, t->closing, speed, StateName(state), flags,
						 (unsigned int)Engine::AttackState(npc), drawn ? "drawn" : "away",
						 t->stalled ? " STALLED" : "");
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
		Engine::ForgetMemoryLayout();
	}

	void OnTick(Actor * owner)
	{
		if(s_dead || !Settings::enabled) return;

		if(!s_firstTick)
		{
			s_firstTick = true;
			_MESSAGE("heartbeat: the first tick arrived - the plugin is alive");
		}

		const DWORD now = GetTickCount();
		if(now - s_lastSample < Settings::sampleMs) return;
		s_lastSample = now;

		Actor * player = (Actor *)Engine::Player();
		if(!player || !Engine::ActorIsBuilt(player)) return;

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
