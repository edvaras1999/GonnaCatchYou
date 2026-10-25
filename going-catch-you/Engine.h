// Engine.h - the engine addresses, offsets and vtable slots Going Catch You uses.
//
// Skyrim Legendary Edition 1.9.32.0.8 only. Everything here was carried over
// from the Engine.h of the FreeRun NPC project, where each entry was read out of
// the executable and tested in game. Nothing here is guessed from the Special
// Edition build. Step 1 only READS the game; the controller helpers are the
// read-only subset.
//
// Addresses are absolute for an image based at 0x00400000.

#pragma once

#include "skse/GameTypes.h"
#include "skse/GameForms.h"
#include "skse/GameReferences.h"
#include "skse/NiNodes.h"
#include "skse/NiTypes.h"

namespace Engine
{
	// ------------------------------------------------- reading the game safely
	//
	// See the long note in Engine.cpp. No pointer chain is walked by hand: every
	// link is checked (non-null, aligned, committed memory) before it is used.

	// Can this process read `length` bytes there? Non-null, aligned, committed.
	bool Readable(const void * address, size_t length);

	// The pointer stored at object + offset, or null if either end is unreadable.
	void * Link(const void * object, UInt32 offset);

	// Forgets what it knows about which memory is committed. Called when the game
	// has had a chance to unmap something (a load, a new game).
	void ForgetMemoryLayout();

	namespace Controller
	{
		// Offsets confirmed in the constructor at 0x00D1E850 (FreeRun NPC project).
		enum
		{
			kForwardVec			= 0x040,
			kSurfaceInfo		= 0x150,	// supportedState is the first byte
			kContext			= 0x190,
			kCurrentState		= 0x1A0,
			kFlags				= 0x1B8,
			kWaterHeight		= 0x1D8,
			kFallStartHeight	= 0x1E0,
			kFallTime			= 0x1E4,
			kSpeedPct			= 0x204,
		};

		enum
		{
			kFlag_Support			= 1 << 8,
			kFlag_NoSim				= 1 << 17,	// physics off: the actor moves only where it is put
		};

		enum { kState_OnGround = 0, kState_Jumping = 1, kState_InAir = 2, kState_Climbing = 3, kState_Flying = 4, kState_Swimming = 5 };
		enum { kSupport_Unsupported = 0, kSupport_Sliding = 1, kSupport_Supported = 2 };
	}

	struct Chain
	{
		void * process;			// actor + 0x88
		void * middle;			// process + 0x04
		void * controller;		// middle + 0x15C
	};

	// Every link of the actor -> controller chain, or false and nothing.
	bool ActorChain(void * actor, Chain & out);

	// Is this actor far enough along to be asked anything? 3D, and a whole chain.
	bool ActorIsBuilt(void * actor);

	// ---------------------------------------------------------------- units

	// The physics world runs in metres, the game world in units. Every vector
	// that crosses that border is scaled. The engine keeps the factor in three
	// places; this is the one the vanilla ray cast uses.
	inline float HavokScale()				{ return *(float *)0x01B11570; }
	inline float HavokScaleInverse()		{ const float s = HavokScale(); return s != 0.0f ? 1.0f / s : 69.99125f; }

	// ------------------------------------------------------------ singletons

	inline PlayerCharacter * Player()	{ return *(PlayerCharacter **)0x01310588; }	// SKSE LE g_thePlayer

	// --------------------------------------------------------- actor layout

	// TESObjectREFR: rot 0x28, pos 0x34, parentCell 0x40 (SKSE LE headers).
	// Actor: process 0x88, ActorState 0x64, ActorValueOwner 0x60.
	inline NiPoint3 & Position(TESObjectREFR * ref)	{ return *(NiPoint3 *)((UInt8 *)ref + 0x34); }
	inline TESObjectCELL * ParentCell(TESObjectREFR * ref) { return *(TESObjectCELL **)((UInt8 *)ref + 0x40); }

	// ActorState's two flag words (same bit layout as Special Edition).
	inline UInt32 & ActorFlags1(Actor * actor)		{ return *(UInt32 *)((UInt8 *)actor + 0x68); }
	inline UInt32 & ActorFlags2(Actor * actor)		{ return *(UInt32 *)((UInt8 *)actor + 0x6C); }

	enum
	{
		kFlag1_MovingBack		= 1 << 0,
		kFlag1_MovingForward	= 1 << 1,
		kFlag1_MovingRight		= 1 << 2,
		kFlag1_MovingLeft		= 1 << 3,
		kFlag1_Walking			= 1 << 6,
		kFlag1_Running			= 1 << 7,
		kFlag1_Sprinting		= 1 << 8,
		kFlag1_Sneaking			= 1 << 9,
		kFlag1_Swimming			= 1 << 10,

		kFlag2_Staggered		= 1 << 13,
	};

	inline UInt32 LifeState(Actor * actor)		{ return (ActorFlags1(actor) >> 21) & 0x0F; }
	inline UInt32 KnockState(Actor * actor)		{ return (ActorFlags1(actor) >> 25) & 0x07; }
	inline UInt32 AttackState(Actor * actor)	{ return (ActorFlags1(actor) >> 28) & 0x0F; }
	inline UInt32 WeaponState(Actor * actor)	{ return (ActorFlags2(actor) >> 5) & 0x07; }

	enum { kWeaponState_Drawn = 3 };
	enum { kLifeState_Alive = 0, kLifeState_Dying = 1, kLifeState_Dead = 2 };

	// IMovementState, the vtable of the ActorState subobject at actor + 0x64.
	// Slot 5 returns the movement speed the engine itself uses for the
	// "IsMoving" condition (0x005048C0 compares it against 5.0). It reaches the
	// character controller, so it answers nothing for an actor still being built.
	inline float MovementSpeed(Actor * actor)
	{
		Chain chain;
		if(!ActorChain(actor, chain)) return 0.0f;

		void * state = (UInt8 *)actor + 0x64;
		void ** vtable = *(void ***)state;
		if(!Readable(vtable, 6 * sizeof(void *))) return 0.0f;

		float (__thiscall * fn)(void *) = (float (__thiscall *)(void *))vtable[5];
		return fn(state);
	}

	// ------------------------------------------------- character controller

	inline UInt32 ControllerState(void * ctrl)	{ return *(UInt32 *)((UInt8 *)ctrl + Controller::kCurrentState); }

	// ----------------------------------------------------------------- 3D

	// TESObjectREFR::GetNiRootNode(firstPerson) is vtable slot 0x6F in the SKSE
	// headers.
	inline NiNode * Get3D(TESObjectREFR * ref, bool firstPerson)
	{
		// A virtual call, so the reference and the table it carries both have to
		// be there. A reference being torn down is the normal case here.
		if(!Readable(ref, sizeof(void *))) return NULL;
		void ** vtable = *(void ***)ref;
		if(!Readable(vtable, (0x6F + 1) * sizeof(void *))) return NULL;

		typedef NiNode * (__thiscall * fn_t)(void *, UInt32);
		return ((fn_t)vtable[0x6F])(ref, firstPerson ? 1 : 0);
	}

	// -------------------------------------------- character controller, read

	inline void * CharController(Actor * actor)
	{
		Chain chain;
		return ActorChain(actor, chain) ? chain.controller : NULL;
	}

	inline UInt8  ControllerSupport(void * ctrl)	{ return *(UInt8 *)((UInt8 *)ctrl + Controller::kSurfaceInfo); }
	inline float  ControllerFallTime(void * ctrl)	{ return *(float *)((UInt8 *)ctrl + Controller::kFallTime); }

	// The controller's virtual calls. Slot indices match Special Edition's and
	// were used for writing in the FreeRun NPC project; here they only read.
	// Slot 6 = linear velocity (metres per second), slot 8 = collision filter.
	inline bool ControllerGetLinearVelocity(void * ctrl, float * outVec4)
	{
		if(!Readable(ctrl, sizeof(void *))) return false;
		void ** vtable = *(void ***)ctrl;
		if(!Readable(vtable, 9 * sizeof(void *))) return false;

		// The engine writes the result with an aligned SSE store, so the buffer
		// it is given has to start on a 16 byte boundary. A plain float[4] on
		// the caller's stack does not, and faulted in version 0.2.0. The local
		// copy here makes the alignment this function's business, not its
		// callers'.
		__declspec(align(16)) float aligned[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		typedef void (__thiscall * fn_t)(void *, float *);
		((fn_t)vtable[6])(ctrl, aligned);

		outVec4[0] = aligned[0];
		outVec4[1] = aligned[1];
		outVec4[2] = aligned[2];
		outVec4[3] = aligned[3];
		return true;
	}

	inline UInt32 ControllerCollisionFilter(void * ctrl)
	{
		UInt32 out = 0;
		typedef UInt32 * (__thiscall * fn_t)(void *, UInt32 *);
		(*(fn_t **)ctrl)[8](ctrl, &out);
		return out;
	}

	// ------------------------------------------------------------------ combat

	// The engine's own "is this actor in combat", found in GreenTea.dll, which
	// implements its IsInCombat condition as exactly this one call: vtable
	// offset 0x388, slot 0xE2, no arguments. It does not depend on a weapon
	// (wolves, trolls and horses answer it too) and says nothing about WHO the
	// actor is fighting.
	inline bool IsInCombat(Actor * actor)
	{
		if(!Readable(actor, sizeof(void *))) return false;
		void ** vtable = *(void ***)actor;
		if(!Readable(vtable, (0xE2 + 1) * sizeof(void *))) return false;
		typedef bool (__thiscall * fn_t)(void *);
		return ((fn_t)vtable[0xE2])(actor);
	}

	// --------------------------------------------------------------- memory

	// The game's own heap. The Havok layer allocates its collision shapes here.
	inline void * HeapAllocate(UInt32 size, UInt32 alignment, bool aligned)
	{
		typedef void * (__thiscall * fn_t)(void *, UInt32, UInt32, bool);
		return ((fn_t)0x00A48D60)((void *)0x01B418B0, size, alignment, aligned);
	}

	inline void HeapFree(void * mem, bool aligned)
	{
		typedef void (__thiscall * fn_t)(void *, void *, bool);
		((fn_t)0x00A487B0)((void *)0x01B418B0, mem, aligned);
	}

	// ------------------------------------------- character controller, write
	//
	// Everything below changes the game. These are the three primitives the
	// FreeRun NPC project proved on NPCs in game: put the controller somewhere,
	// set its velocity, and turn its physics off while that is going on.
	// Vectors are in the physics world's metres; the engine writes and reads
	// them with aligned SSE moves, so every buffer handed to it is aligned here
	// rather than trusted to the caller.

	inline UInt32 & ControllerFlags(void * ctrl)	{ return *(UInt32 *)((UInt8 *)ctrl + Controller::kFlags); }

	inline bool ControllerGetPosition(void * ctrl, float * out3)
	{
		if(!Readable(ctrl, sizeof(void *))) return false;
		void ** vtable = *(void ***)ctrl;
		if(!Readable(vtable, 9 * sizeof(void *))) return false;

		__declspec(align(16)) float aligned[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		typedef void (__thiscall * fn_t)(void *, float *, bool);
		((fn_t)vtable[2])(ctrl, aligned, true);
		out3[0] = aligned[0]; out3[1] = aligned[1]; out3[2] = aligned[2];
		return true;
	}

	inline bool ControllerSetPosition(void * ctrl, const float * pos3)
	{
		if(!Readable(ctrl, sizeof(void *))) return false;
		void ** vtable = *(void ***)ctrl;
		if(!Readable(vtable, 9 * sizeof(void *))) return false;

		__declspec(align(16)) float aligned[4] = { pos3[0], pos3[1], pos3[2], 0.0f };
		typedef void (__thiscall * fn_t)(void *, const float *, bool, bool);
		((fn_t)vtable[3])(ctrl, aligned, true, false);
		return true;
	}

	inline bool ControllerSetLinearVelocity(void * ctrl, float x, float y, float z)
	{
		if(!Readable(ctrl, sizeof(void *))) return false;
		void ** vtable = *(void ***)ctrl;
		if(!Readable(vtable, 9 * sizeof(void *))) return false;

		__declspec(align(16)) float aligned[4] = { x, y, z, 0.0f };
		typedef void (__thiscall * fn_t)(void *, const float *);
		((fn_t)vtable[7])(ctrl, aligned);
		return true;
	}

	// ---------------------------------------------------------------- target

	// Where the engine keeps what an actor is fighting: a reference handle, at
	// actor + 0x90. Found by comparing NPCs in combat with NPCs that were not
	// (338 of 410 samples in combat held the player's handle there, 0 of 42
	// out of combat held anything). 0 means no target.
	enum { kPlayerHandle = 0x00100000 };
	inline UInt32 CombatTargetHandle(Actor * actor)
	{
		if(!Readable(actor, 0x94)) return 0;
		return *(UInt32 *)((UInt8 *)actor + 0x90);
	}

	// ------------------------------------------------------ animation graph

	// The holder is a subobject of every reference at + 0x20.
	inline void * GraphHolder(TESObjectREFR * ref)	{ return (UInt8 *)ref + 0x20; }

	// The holder's table, checked as far as the slot being called. A reference
	// the game is building or tearing down has a holder too, with whatever
	// table pointer the memory happened to hold.
	inline void ** HolderVtable(TESObjectREFR * ref, UInt32 slot)
	{
		if(!Readable(ref, 0x24)) return NULL;
		void ** vtable = *(void ***)((UInt8 *)ref + 0x20);
		return Readable(vtable, (slot + 1) * sizeof(void *)) ? vtable : NULL;
	}

	// Sends an event to the actor's animation graph. False if the graph does
	// not take it.
	inline bool NotifyAnimationGraph(TESObjectREFR * ref, const char * eventName)
	{
		void ** vtable = HolderVtable(ref, 0x01);
		if(!vtable) return false;

		BSFixedString name;
		CALL_MEMBER_FN(&name, Set)(eventName);
		typedef bool (__thiscall * fn_t)(void *, const BSFixedString &);
		const bool ok = ((fn_t)vtable[0x01])(GraphHolder(ref), name);
		CALL_MEMBER_FN(&name, Release)();
		return ok;
	}
}
