#include "FreeRun.h"
#include "Engine.h"
#include "Log.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

namespace
{
	// Names the behaviour patch uses (see the FreeRun NPC project's Graph.cpp).
	const char * const kEvent			= "SkyParkour";
	const char * const kEventInterrupt	= "SkyParkour_Interrupt";
	const char * const kVarLedge		= "SkyParkourLedge";
	const char * const kVarOngoing		= "SkyParkourOngoing";
	const char * const kVarInstalled	= "SkyParkourTPPInstalled";
	const char * const kVarDriven		= "bAnimationDriven";

	// FreeRun's move types: kFailed = 0, kGrab, kVault, kStepLow, ... A vault is 2.
	enum { kLedgeNone = -1, kLedgeVault = 2 };

	enum { kStartWaitMs = 1500, kMaxMoveMs = 6000, kAftermathMs = 2500 };

	struct Move
	{
		bool		used;
		int			phase;				// 1 = sent, waiting for the graph to start it; 2 = running; 3 = finished, watched
		UInt32		id;
		Actor *		npc;
		DWORD		sentMs, startedMs, endMs;
		NiPoint3	startPos;
		float		playerDistAtEnd;
	};

	Move	s_move;
	UInt32	s_faults = 0;
	bool	s_ok = true;

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

	float Dist3(const NiPoint3 & a, const NiPoint3 & b)
	{
		const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
		return sqrtf(dx * dx + dy * dy + dz * dz);
	}

	bool Valid(Move & m, Engine::Chain & chain)
	{
		if(!Engine::Readable(m.npc, 0x140)) return false;
		if(m.npc->formID != m.id) return false;
		if(Engine::LifeState(m.npc) != Engine::kLifeState_Alive) return false;
		return Engine::ActorChain(m.npc, chain);
	}

	// The engine's movement for this NPC is the engine's again.
	void GiveBack(Actor * npc, bool interrupt)
	{
		Engine::GraphSetBool(npc, kVarDriven, false);
		Engine::GraphSetInt(npc, kVarLedge, kLedgeNone);
		if(interrupt) Engine::NotifyAnimationGraph(npc, kEventInterrupt);
	}

	void Step(Move & m, DWORD now)
	{
		Engine::Chain chain;
		if(!Valid(m, chain))
		{
			_MESSAGE("freerun: lost %08X in the middle of a move; dropped it", (unsigned int)m.id);
			m.used = false;
			return;
		}

		if(m.phase == 1)
		{
			bool ongoing = false;
			const bool readable = Engine::GraphGetBool(m.npc, kVarOngoing, ongoing);
			if(readable && ongoing)
			{
				m.phase = 2;
				m.startedMs = now;
				_MESSAGE("freerun: %08X started after %u ms (SkyParkourOngoing went up)", (unsigned int)m.id, (unsigned int)(now - m.sentMs));
				return;
			}
			if(now - m.sentMs > kStartWaitMs)
			{
				_MESSAGE("freerun: %08X did not start within %u ms (SkyParkourOngoing %s). "
						 "The graph did not react: either FreeRun's behaviour patch is not in this NPC's graph, "
						 "or the NPC was not in a state that takes the move.",
						 (unsigned int)m.id, (unsigned int)kStartWaitMs, readable ? "readable, still down" : "not readable at all");
				GiveBack(m.npc, true);
				m.used = false;
			}
			return;
		}

		if(m.phase == 2)
		{
			bool ongoing = true;
			Engine::GraphGetBool(m.npc, kVarOngoing, ongoing);
			const bool timeout = now - m.startedMs > kMaxMoveMs;
			if(ongoing && !timeout) return;

			GiveBack(m.npc, timeout);

			const NiPoint3 pos = Engine::Position(m.npc);
			const NiPoint3 pl = Engine::Position(Engine::Player());
			m.playerDistAtEnd = Dist3(pos, pl);
			m.phase = 3;
			m.endMs = now;

			_MESSAGE("freerun: %08X %s after %.2f s; moved %.0f units (dz %+.0f) from where it stood; controller %s, support %u",
					 (unsigned int)m.id, timeout ? "TIMED OUT, interrupted" : "finished",
					 (float)(now - m.startedMs) / 1000.0f, Dist3(pos, m.startPos), pos.z - m.startPos.z,
					 StateName(Engine::ControllerState(chain.controller)), (unsigned int)Engine::ControllerSupport(chain.controller));
			return;
		}

		if(now - m.endMs >= kAftermathMs)
		{
			const NiPoint3 pos = Engine::Position(m.npc);
			const NiPoint3 pl = Engine::Position(Engine::Player());
			const float distNow = Dist3(pos, pl);
			_MESSAGE("freerun: aftermath %08X (+%.1f s): %.0f units from the player (was %.0f at the end, %s), combat %d, target %08X",
					 (unsigned int)m.id, (float)(now - m.endMs) / 1000.0f, distNow, m.playerDistAtEnd,
					 distNow < m.playerDistAtEnd - 40.0f ? "closer" : (distNow > m.playerDistAtEnd + 40.0f ? "farther" : "about the same"),
					 Engine::IsInCombat(m.npc) ? 1 : 0, (unsigned int)Engine::CombatTargetHandle(m.npc));
			m.used = false;
		}
	}

	bool GuardedStep(Move & m, DWORD now)
	{
		__try
		{
			Step(m, now);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void GuardedGiveBack(Actor * npc)
	{
		__try
		{
			GiveBack(npc, false);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	bool GuardedBegin(Move & m, bool * sent)
	{
		__try
		{
			// What the graph says before anything is written. If the behaviour
			// patch is in this NPC's graph at all, these variables exist.
			bool installed = false, ongoing = false;
			SInt32 ledge = -99;
			const bool a = Engine::GraphGetBool(m.npc, kVarInstalled, installed);
			const bool b = Engine::GraphGetBool(m.npc, kVarOngoing, ongoing);
			const bool c = Engine::GraphGetInt(m.npc, kVarLedge, ledge);
			_MESSAGE("freerun: before: SkyParkourTPPInstalled=%d%s, SkyParkourOngoing=%d%s, SkyParkourLedge=%d%s",
					 installed ? 1 : 0, a ? "" : " (not readable)",
					 ongoing ? 1 : 0, b ? "" : " (not readable)",
					 (int)ledge, c ? "" : " (not readable)");

			Engine::GraphSetInt(m.npc, kVarLedge, kLedgeVault);
			Engine::GraphSetBool(m.npc, kVarDriven, true);
			*sent = Engine::NotifyAnimationGraph(m.npc, kEvent);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

namespace FreeRun
{
	bool Begin(Actor * npc, UInt32 id, float obstacleDistance, float obstacleHeight)
	{
		if(!s_ok || s_move.used) return false;

		if(obstacleHeight < kVaultMinHeight || obstacleHeight > kVaultMaxHeight)
		{
			_MESSAGE("freerun: %08X: the obstacle is about %.0f high; FreeRun's vault is for %d to %d. Not started.",
					 (unsigned int)id, obstacleHeight, (int)kVaultMinHeight, (int)kVaultMaxHeight);
			return false;
		}
		if(obstacleDistance > kMaxObstacleDistance)
		{
			_MESSAGE("freerun: %08X: the obstacle is %.0f ahead; the NPC has to stand within %d of it, facing it. Not started.",
					 (unsigned int)id, obstacleDistance, (int)kMaxObstacleDistance);
			return false;
		}

		Engine::Chain chain;
		if(!Engine::ActorChain(npc, chain)) return false;

		memset(&s_move, 0, sizeof(s_move));
		s_move.used = true;
		s_move.phase = 1;
		s_move.id = id;
		s_move.npc = npc;
		s_move.sentMs = GetTickCount();
		s_move.startPos = Engine::Position(npc);

		bool sent = false;
		if(!GuardedBegin(s_move, &sent))
		{
			GuardedGiveBack(npc);
			s_move.used = false;
			++s_faults;
			_ERROR("freerun: could not begin the move of %08X (%u so far)", (unsigned int)id, (unsigned int)s_faults);
			if(s_faults >= 2) { s_ok = false; _ERROR("freerun: switched off for the rest of the session"); }
			return false;
		}

		_MESSAGE("freerun: begin %08X: vault over an obstacle %.0f ahead and about %.0f high; the graph answered %s to the %s event",
				 (unsigned int)id, obstacleDistance, obstacleHeight, sent ? "accepted" : "not accepted", kEvent);
		return true;
	}

	bool Active(UInt32 id)
	{
		return s_move.used && s_move.phase < 3 && s_move.id == id;
	}

	void OnTick(DWORD now)
	{
		if(!s_move.used) return;

		if(!GuardedStep(s_move, now))
		{
			GuardedGiveBack(s_move.npc);
			s_move.used = false;
			++s_faults;
			_ERROR("freerun: a step faulted on a bad pointer (%u so far); gave %08X back", (unsigned int)s_faults, (unsigned int)s_move.id);
			if(s_faults >= 2) { s_ok = false; _ERROR("freerun: switched off for the rest of the session"); }
		}
	}

	void Reset()
	{
		s_move.used = false;
	}
}
