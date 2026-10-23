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
		enum
		{
			kCurrentState	= 0x1A0,	// hkpCharacterContext state, read by 0x00D42B80
			kFlags			= 0x1B8,
			kSpeedPct		= 0x204,
		};

		enum { kState_OnGround = 0, kState_Jumping = 1, kState_InAir = 2, kState_Climbing = 3, kState_Flying = 4, kState_Swimming = 5 };
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
}
