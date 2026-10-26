#include "Tick.h"
#include "Observer.h"
#include "Log.h"

#include "skse/GameTypes.h"
#include "skse/SafeWrite.h"

namespace
{
	// TESCameraState's own table: destructor, Begin, End, Update, ...
	// Both addresses and the slot were verified in the FreeRun NPC project
	// (see Signatures.h).
	UInt32 * const kThirdPersonState	= (UInt32 *)0x010E29EC;
	UInt32 * const kFirstPersonState	= (UInt32 *)0x010E30C8;

	enum { kSlot_Update = 0x03 };

	typedef void (__thiscall * _Update)(void * state, void * nextState);

	_Update s_origThirdUpdate = NULL;
	_Update s_origFirstUpdate = NULL;

	// The game's own update runs first, so what the observer sees is the world
	// as this frame left it.
	void __fastcall Hook_ThirdUpdate(void * state, void *, void * nextState)
	{
		s_origThirdUpdate(state, nextState);
		Observer::OnTick();
	}

	void __fastcall Hook_FirstUpdate(void * state, void *, void * nextState)
	{
		s_origFirstUpdate(state, nextState);
		Observer::OnTick();
	}

	template <typename T>
	void HookVFunc(UInt32 * vtable, UInt32 slot, T & original, void * replacement)
	{
		original = (T)vtable[slot];
		SafeWrite32((UInt32)&vtable[slot], (UInt32)replacement);
	}
}

namespace Tick
{
	bool InstallHooks()
	{
		HookVFunc(kThirdPersonState, kSlot_Update, s_origThirdUpdate, (void *)&Hook_ThirdUpdate);
		HookVFunc(kFirstPersonState, kSlot_Update, s_origFirstUpdate, (void *)&Hook_FirstUpdate);

		if(!s_origThirdUpdate || !s_origFirstUpdate)
		{
			_ERROR("heartbeat: a camera state has an empty Update slot (third %p, first %p)",
				   (void *)s_origThirdUpdate, (void *)s_origFirstUpdate);
			return false;
		}

		_MESSAGE("heartbeat: hooked the Update of the third and first person camera states; "
				 "it ticks once a frame while the game is running with either camera");
		return true;
	}
}
