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
	enum { kMaxArcMs = 2500 };			// no hop lasts longer than this, whatever the plan says
	enum { kSettleMs = 300 };			// when the landing is checked
	enum { kAftermathMs = 2500 };		// when the NPC is looked at again

	struct Hop
	{
		bool			used;
		int				phase;			// 1 = on the arc, 2 = landed, being watched
		UInt32			id;
		Actor *			npc;
		Jump::Plan		plan;
		DWORD			startMs;
		DWORD			endMs;
		UInt32			steps;
		float			c0[3];			// the controller's position when the hop began, in physics metres
		float			playerDistAtLand;
		bool			settleLogged;
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

	// Is the NPC this hop was made for still there, still alive, still whole?
	bool Valid(Hop & h, Engine::Chain & chain)
	{
		if(!Engine::Readable(h.npc, 0x140)) return false;
		if(h.npc->formID != h.id) return false;
		if(Engine::LifeState(h.npc) != Engine::kLifeState_Alive) return false;
		return Engine::ActorChain(h.npc, chain);
	}

	// Physics back on, and no velocity left over from the arc.
	void Release(const Engine::Chain & chain)
	{
		Engine::ControllerFlags(chain.controller) &= ~Engine::Controller::kFlag_NoSim;
		Engine::ControllerSetLinearVelocity(chain.controller, 0.0f, 0.0f, 0.0f);
	}

	// One frame of the arc.
	void StepArc(Hop & h, DWORD now)
	{
		Engine::Chain chain;
		if(!Valid(h, chain))
		{
			_MESSAGE("jump: lost %08X in the middle of a hop; dropped it", (unsigned int)h.id);
			h.used = false;
			return;
		}

		// The engine may clear the flag on its own; the arc needs it kept.
		Engine::ControllerFlags(chain.controller) |= Engine::Controller::kFlag_NoSim;

		const DWORD elapsed = now - h.startMs;
		float s = (h.plan.durationSec > 0.05f) ? (float)elapsed / (h.plan.durationSec * 1000.0f) : 1.0f;
		const bool finishing = s >= 1.0f || elapsed > kMaxArcMs;
		if(finishing) s = 1.0f;

		// Where the NPC should be: a straight line from start to landing, lifted
		// by a parabola that is 0 at both ends and `apex` high in the middle.
		const Jump::Plan & p = h.plan;
		const float lift = p.apex * 4.0f * s * (1.0f - s);
		const float wx = p.start.x + (p.land.x - p.start.x) * s;
		const float wy = p.start.y + (p.land.y - p.start.y) * s;
		const float wz = p.start.z + (p.land.z - p.start.z) * s + lift;

		// The controller lives in metres. Its position at the start of the hop is
		// known, so the target is that plus the same displacement, scaled.
		const float scale = Engine::HavokScale();
		float target[3];
		target[0] = h.c0[0] + (wx - p.start.x) * scale;
		target[1] = h.c0[1] + (wy - p.start.y) * scale;
		target[2] = h.c0[2] + (wz - p.start.z) * scale;
		Engine::ControllerSetPosition(chain.controller, target);
		++h.steps;

		if(!finishing) return;

		Release(chain);

		bool eventOk = false;
		if(Settings::jumpAnimation && Settings::jumpEndEvent[0])
			eventOk = Engine::NotifyAnimationGraph(h.npc, Settings::jumpEndEvent);

		const NiPoint3 now3 = Engine::Position(h.npc);
		const NiPoint3 pl = Engine::Position(Engine::Player());
		h.playerDistAtLand = Dist3(now3, pl);
		h.phase = 2;
		h.endMs = now;

		_MESSAGE("jump: landed %08X after %.2f s (%u steps); controller %s, support %u; end event %s -> %s",
				 (unsigned int)h.id, (float)elapsed / 1000.0f, (unsigned int)h.steps,
				 StateName(Engine::ControllerState(chain.controller)), (unsigned int)Engine::ControllerSupport(chain.controller),
				 Settings::jumpAnimation ? Settings::jumpEndEvent : "(none)",
				 Settings::jumpAnimation ? (eventOk ? "accepted" : "not accepted") : "skipped");
	}

	// Looks at the NPC after it has landed, twice: once to see whether it came
	// down where it was meant to, and once to see whether it went on after the
	// player.
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

		if(!h.settleLogged && since >= kSettleMs)
		{
			h.settleLogged = true;
			const NiPoint3 pos = Engine::Position(h.npc);
			_MESSAGE("jump: settled %08X (+%.1f s): %.0f units from the planned landing (dz %+.0f), controller %s, support %u, physics %s",
					 (unsigned int)h.id, (float)since / 1000.0f, Dist3(pos, h.plan.land), pos.z - h.plan.land.z,
					 StateName(Engine::ControllerState(chain.controller)), (unsigned int)Engine::ControllerSupport(chain.controller),
					 (Engine::ControllerFlags(chain.controller) & Engine::Controller::kFlag_NoSim) ? "STILL OFF" : "on");
		}

		if(since >= kAftermathMs)
		{
			const NiPoint3 pos = Engine::Position(h.npc);
			const NiPoint3 pl = Engine::Position(Engine::Player());
			const float distNow = Dist3(pos, pl);
			_MESSAGE("jump: aftermath %08X (+%.1f s): moved %.0f units since landing, %.0f units from the player (was %.0f at landing, %s), "
					 "combat %d, target %08X",
					 (unsigned int)h.id, (float)since / 1000.0f, Dist3(pos, h.plan.land), distNow, h.playerDistAtLand,
					 distNow < h.playerDistAtLand - 40.0f ? "closer" : (distNow > h.playerDistAtLand + 40.0f ? "farther" : "about the same"),
					 Engine::IsInCombat(h.npc) ? 1 : 0, (unsigned int)Engine::CombatTargetHandle(h.npc));
			h.used = false;
		}
	}

	// No objects with destructors in these two, so the compiler allows the __try.
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

	void GuardedRelease(Hop & h)
	{
		__try
		{
			Engine::Chain chain;
			if(Valid(h, chain)) Release(chain);
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
			if(!Engine::ControllerGetPosition(chain.controller, h.c0)) return false;

			Engine::ControllerFlags(chain.controller) |= Engine::Controller::kFlag_NoSim;

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
			// Whatever it managed to set, put back.
			GuardedRelease(h);
			h.used = false;
			++s_faults;
			_ERROR("jump: could not begin the hop of %08X (%u so far)", (unsigned int)id, (unsigned int)s_faults);
			if(s_faults >= 2) { s_ok = false; _ERROR("jump: switched off for the rest of the session"); }
			return false;
		}

		_MESSAGE("jump: begin %08X; start event %s -> %s",
				 (unsigned int)id,
				 Settings::jumpAnimation ? Settings::jumpStartEvent : "(none)",
				 Settings::jumpAnimation ? (eventOk ? "accepted" : "not accepted") : "skipped");
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
				GuardedRelease(h);
				h.used = false;
				++s_faults;
				_ERROR("jump: a step faulted on a bad pointer (%u so far); released %08X", (unsigned int)s_faults, (unsigned int)h.id);
				if(s_faults >= 2) { s_ok = false; _ERROR("jump: switched off for the rest of the session"); }
			}
		}
	}

	void Reset()
	{
		for(int i = 0; i < kMaxHops; ++i)
			s_hops[i].used = false;
	}
}
