#include "Jump.h"
#include "Settings.h"
#include "Engine.h"
#include "Log.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

namespace
{
	enum { kMaxHops = 2 };
	enum { kWatchMs = 1800 };			// how long after landing an NPC is checked for having fallen through
	enum { kSettleMs = 300 };			// when the landing is first looked at
	enum { kAftermathMs = 2500 };		// when the NPC is looked at again

	struct Hop
	{
		bool			used;
		int				phase;			// 1 = in the air, 2 = landed, being watched
		UInt32			id;
		Actor *			npc;
		Jump::Plan		plan;
		DWORD			startMs;
		DWORD			endMs;
		UInt32			steps;
		float			c0[3];			// the controller's position when the leap began, in physics metres
		float			playerDistAtLand;
		bool			settleLogged;
		bool			rescued;
	};

	Hop		s_hops[kMaxHops];
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

	// Is the NPC this leap was made for still there, still alive, still whole?
	bool Valid(Hop & h, Engine::Chain & chain)
	{
		if(!Engine::Readable(h.npc, 0x140)) return false;
		if(h.npc->formID != h.id) return false;
		if(Engine::LifeState(h.npc) != Engine::kLifeState_Alive) return false;
		return Engine::ActorChain(h.npc, chain);
	}

	// Puts the controller at a world position (game units) and gives it the
	// velocity and state that go with a flight.
	void Drive(const Hop & h, const Engine::Chain & chain, float wx, float wy, float wz, float vx, float vy, float vz, bool inAir)
	{
		const Jump::Plan & p = h.plan;

		// The controller lives in metres. Its position at the start is known, so
		// the target is that plus the same displacement, scaled.
		const float scale = Engine::HavokScale();
		float target[3];
		target[0] = h.c0[0] + (wx - p.start.x) * scale;
		target[1] = h.c0[1] + (wy - p.start.y) * scale;
		target[2] = h.c0[2] + (wz - p.start.z) * scale;
		Engine::ControllerSetPosition(chain.controller, target);

		if(inAir) Engine::ControllerSetState(chain.controller, Engine::Controller::kState_InAir);
		Engine::ControllerSetLinearVelocity(chain.controller, vx * scale, vy * scale, vz * scale);
		Engine::ControllerSetFall(chain.controller, wz);
	}

	// One frame of the flight.
	void StepArc(Hop & h, DWORD now)
	{
		Engine::Chain chain;
		if(!Valid(h, chain))
		{
			_MESSAGE("jump: lost %08X in the middle of a leap; dropped it", (unsigned int)h.id);
			h.used = false;
			return;
		}

		const Jump::Plan & p = h.plan;
		const DWORD elapsed = now - h.startMs;
		float t = (float)elapsed / 1000.0f;
		const bool finishing = t >= p.durationSec || elapsed > (DWORD)(p.durationSec * 1000.0f) + 600;
		if(finishing) t = p.durationSec;

		// Ballistic: constant horizontal speed, vertical speed falling with gravity.
		const float wx = p.start.x + p.dir.x * p.horizontalSpeed * t;
		const float wy = p.start.y + p.dir.y * p.horizontalSpeed * t;
		const float wz = p.start.z + p.verticalSpeed * t - 0.5f * p.gravity * t * t;
		const float vz = p.verticalSpeed - p.gravity * t;

		if(!finishing)
		{
			Drive(h, chain, wx, wy, wz, p.dir.x * p.horizontalSpeed, p.dir.y * p.horizontalSpeed, vz, true);
			++h.steps;
			return;
		}

		// The end of the arc: a hair above the landing point, with a small push
		// forward and down, still "in the air" for this last write so that the
		// physics finds the ground itself and plays the landing.
		Drive(h, chain, p.land.x, p.land.y, p.land.z + 2.0f, p.dir.x * 60.0f, p.dir.y * 60.0f, -60.0f, true);

		bool eventOk = false;
		if(Settings::jumpAnimation && Settings::jumpEndEvent[0])
			eventOk = Engine::NotifyAnimationGraph(h.npc, Settings::jumpEndEvent);

		const NiPoint3 pl = Engine::Position(Engine::Player());
		h.playerDistAtLand = Dist3(p.land, pl);
		h.phase = 2;
		h.endMs = now;
		++h.steps;

		_MESSAGE("jump: landed %08X after %.2f s (%u steps); end event %s",
				 (unsigned int)h.id, (float)elapsed / 1000.0f, (unsigned int)h.steps,
				 Settings::jumpAnimation ? (eventOk ? "accepted" : "not accepted") : "(none)");
	}

	// Puts the NPC back where it started, on the ground, with no velocity.
	void Rescue(Hop & h, const Engine::Chain & chain, const char * why)
	{
		const Jump::Plan & p = h.plan;
		Drive(h, chain, p.start.x, p.start.y, p.start.z, 0.0f, 0.0f, 0.0f, false);
		Engine::ControllerSetState(chain.controller, Engine::Controller::kState_OnGround);
		h.rescued = true;
		_MESSAGE("jump: RESCUED %08X: %s; put back where it started", (unsigned int)h.id, why);
	}

	// Looks at the NPC after it has landed: for a while on every frame, to catch
	// one that has gone through the floor, and twice more to see where it came
	// down and whether it went on after the player.
	void StepAfter(Hop & h, DWORD now)
	{
		Engine::Chain chain;
		if(!Valid(h, chain))
		{
			_MESSAGE("jump: lost %08X after landing; dropped it", (unsigned int)h.id);
			h.used = false;
			return;
		}

		const DWORD since = now - h.endMs;
		const NiPoint3 pos = Engine::Position(h.npc);

		if(!h.rescued && since < kWatchMs && since > 120)
		{
			const UInt32 state = Engine::ControllerState(chain.controller);
			const UInt8 support = Engine::ControllerSupport(chain.controller);
			if(pos.z < h.plan.land.z - 90.0f)
				Rescue(h, chain, "it is well below the landing point");
			else if(since > 700 && state == Engine::Controller::kState_InAir && support == 0 &&
					Engine::ControllerFallTime(chain.controller) > 0.8f)
				Rescue(h, chain, "it has been falling for almost a second after landing");
		}

		if(!h.settleLogged && since >= kSettleMs)
		{
			h.settleLogged = true;
			_MESSAGE("jump: settled %08X (+%.1f s): %.0f units from the planned landing (dz %+.0f), controller %s, support %u, fall %.2f s",
					 (unsigned int)h.id, (float)since / 1000.0f, Dist3(pos, h.plan.land), pos.z - h.plan.land.z,
					 StateName(Engine::ControllerState(chain.controller)), (unsigned int)Engine::ControllerSupport(chain.controller),
					 Engine::ControllerFallTime(chain.controller));
		}

		if(since >= kAftermathMs)
		{
			const NiPoint3 pl = Engine::Position(Engine::Player());
			const float distNow = Dist3(pos, pl);
			_MESSAGE("jump: aftermath %08X (+%.1f s): %.0f units from the planned landing, %.0f units from the player (was %.0f at landing, %s), "
					 "combat %d, target %08X%s",
					 (unsigned int)h.id, (float)since / 1000.0f, Dist3(pos, h.plan.land), distNow, h.playerDistAtLand,
					 distNow < h.playerDistAtLand - 40.0f ? "closer" : (distNow > h.playerDistAtLand + 40.0f ? "farther" : "about the same"),
					 Engine::IsInCombat(h.npc) ? 1 : 0, (unsigned int)Engine::CombatTargetHandle(h.npc), h.rescued ? ", was rescued" : "");
			h.used = false;
		}
	}

	// No objects with destructors in these, so the compiler allows the __try.
	bool GuardedStep(Hop & h, DWORD now)
	{
		__try
		{
			if(h.phase == 1) StepArc(h, now);
			else StepAfter(h, now);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void GuardedRescue(Hop & h)
	{
		__try
		{
			Engine::Chain chain;
			if(Valid(h, chain)) Rescue(h, chain, "a step faulted");
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	bool GuardedBegin(Hop & h, bool * eventOk)
	{
		__try
		{
			Engine::Chain chain;
			if(!Engine::ActorChain(h.npc, chain)) return false;
			if(Engine::ControllerState(chain.controller) != Engine::Controller::kState_OnGround) return false;
			if(!Engine::ControllerGetPosition(chain.controller, h.c0)) return false;

			*eventOk = false;
			if(Settings::jumpAnimation && Settings::jumpStartEvent[0])
				*eventOk = Engine::NotifyAnimationGraph(h.npc, Settings::jumpStartEvent);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

namespace Jump
{
	// Returns true when this NPC's last landing needed a rescue and the aftermath
	// has not yet been reported. The Observer calls this to apply the extra cooldown.
	bool WasRescued(UInt32 id);

	bool Begin(Actor * npc, UInt32 id, const Plan & plan)
	{
		if(!s_ok) return false;

		Hop * slot = NULL;
		for(int i = 0; i < kMaxHops; ++i)
		{
			if(s_hops[i].used)
			{
				if(s_hops[i].id == id) return false;
			}
			else if(!slot) slot = &s_hops[i];
		}
		if(!slot) return false;

		Hop & h = *slot;
		memset(&h, 0, sizeof(h));
		h.used = true;
		h.phase = 1;
		h.id = id;
		h.npc = npc;
		h.plan = plan;
		h.startMs = GetTickCount();

		bool eventOk = false;
		if(!GuardedBegin(h, &eventOk))
		{
			h.used = false;
			// Not necessarily a fault: an NPC that is not on the ground is refused.
			_MESSAGE("jump: could not begin the leap of %08X (not on the ground, or its controller cannot be read)", (unsigned int)id);
			return false;
		}

		_MESSAGE("jump: begin %08X; start event %s",
				 (unsigned int)id,
				 Settings::jumpAnimation ? (eventOk ? "accepted" : "not accepted") : "(none)");
		return true;
	}

	bool Active(UInt32 id)
	{
		for(int i = 0; i < kMaxHops; ++i)
			if(s_hops[i].used && s_hops[i].phase == 1 && s_hops[i].id == id) return true;
		return false;
	}

	UInt32 ActiveCount()
	{
		UInt32 n = 0;
		for(int i = 0; i < kMaxHops; ++i)
			if(s_hops[i].used && s_hops[i].phase == 1) ++n;
		return n;
	}

	void OnTick(DWORD now)
	{
		for(int i = 0; i < kMaxHops; ++i)
		{
			Hop & h = s_hops[i];
			if(!h.used) continue;

			if(!GuardedStep(h, now))
			{
				GuardedRescue(h);
				h.used = false;
				++s_faults;
				_ERROR("jump: a step faulted on a bad pointer (%u so far); put %08X back", (unsigned int)s_faults, (unsigned int)h.id);
				if(s_faults >= 2) { s_ok = false; _ERROR("jump: switched off for the rest of the session"); }
			}
		}
	}

	bool WasRescued(UInt32 id)
	{
		for(int i = 0; i < kMaxHops; ++i)
			if(s_hops[i].used && s_hops[i].id == id && s_hops[i].rescued) return true;
		return false;
	}

	void Reset()
	{
		for(int i = 0; i < kMaxHops; ++i)
			s_hops[i].used = false;
	}
}
