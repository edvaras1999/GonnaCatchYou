#include "Engine.h"
#include "Log.h"

#include "skse/GameReferences.h"

#include <windows.h>

// The SDK declares this as a pointer to the engine's own lookup and defines it
// in GameReferences.cpp - a file this plugin does not compile. The one symbol
// the SDK files we do compile need is defined here, at the address the SDK
// itself uses.
const _LookupREFRByHandle LookupREFRByHandle = (_LookupREFRByHandle)0x004A9180;

namespace Engine
{
	// ------------------------------------------------------- reading the game
	//
	// WHY EVERY POINTER WALK GOES THROUGH THIS LAYER.
	//
	// An actor's character controller is a chain:
	//
	//     actor + 0x88   -> AIProcess
	//     AIProcess + 4  -> MiddleProcess
	//     Middle + 0x15C -> the controller
	//
	// The engine's own accessor checks the middle link for NULL and nothing
	// else, because inside the engine nothing else can happen. This plugin ticks
	// from the animation graph, and an actor's AIProcess is torn down and rebuilt
	// whenever its 3D is - loading a save, crossing a cell boundary. For a moment
	// [actor+0x88] still points at a block that was handed back to the heap.
	// A tester's crash log in the FreeRun NPC project caught exactly that: a
	// pointer that was not even aligned to four, faulting inside the engine.
	//
	// So no chain is walked until every link has been checked: non-null, aligned,
	// and in memory this process has committed. When a link fails the answer is
	// "there is no controller", which every caller already handles.
	//
	// What this CANNOT do: tell a live object from a freed one. A freed heap
	// block is still committed. The structured exception handler around the
	// sampler in Observer.cpp catches the rest.
	namespace
	{
		// Committed regions are large and few, so the kernel is asked once per
		// region rather than once per read.
		enum { kRegionCache = 8 };

		struct Region
		{
			UInt32	base;
			UInt32	end;
			bool	usable;
		};

		Region	s_regions[kRegionCache];
		UInt32	s_regionNext = 0;

		bool RegionAllows(UInt32 address, size_t length)
		{
			for(UInt32 i = 0; i < kRegionCache; ++i)
			{
				const Region & region = s_regions[i];
				if(!region.end) continue;
				if(address < region.base || address >= region.end) continue;
				return region.usable && (address + length <= region.end);
			}

			MEMORY_BASIC_INFORMATION info;
			if(!VirtualQuery((const void *)address, &info, sizeof(info)))
				return false;

			Region fresh;
			fresh.base		= (UInt32)info.BaseAddress;
			fresh.end		= (UInt32)info.BaseAddress + (UInt32)info.RegionSize;
			fresh.usable	= (info.State == MEM_COMMIT) &&
							  !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD));

			// Written last, so a reader on another thread never sees half an entry.
			Region & slot = s_regions[s_regionNext % kRegionCache];
			++s_regionNext;
			slot.end	= 0;
			slot.base	= fresh.base;
			slot.usable	= fresh.usable;
			slot.end	= fresh.end;

			return fresh.usable && (address + length <= fresh.end);
		}
	}

	void ForgetMemoryLayout()
	{
		for(UInt32 i = 0; i < kRegionCache; ++i)
			s_regions[i].end = 0;
	}

	bool Readable(const void * address, size_t length)
	{
		const UInt32 value = (UInt32)address;
		if(value < 0x00010000)		return false;	// the null page
		if(value & 3)				return false;	// no engine object is misaligned
		// Not 0x80000000: a large-address-aware game keeps ordinary heap above it.
		if(value >= 0xFFFF0000)		return false;
		if(value + length < value)	return false;	// wrapped

		return RegionAllows(value, length);
	}

	void * Link(const void * object, UInt32 offset)
	{
		if(!Readable(object, offset + sizeof(void *))) return NULL;
		void * value = *(void * const *)((const UInt8 *)object + offset);
		return Readable(value, sizeof(void *)) ? value : NULL;
	}

	bool ActorChain(void * actor, Chain & out)
	{
		out.process = out.middle = out.controller = NULL;

		if(!Readable(actor, 0x8C)) return false;

		out.process = Link(actor, 0x88);
		if(!out.process) return false;

		out.middle = Link(out.process, 0x04);
		if(!out.middle) return false;

		// The controller has to be good for its whole span, because the plugin
		// reads flags and a state out of the far end of it.
		if(!Readable(out.middle, Controller::kFlags + 4)) return false;
		out.controller = *(void **)((UInt8 *)out.middle + 0x15C);
		if(!Readable(out.controller, Controller::kSpeedPct + 4)) return false;

		return true;
	}

	bool ActorIsBuilt(void * actor)
	{
		if(!actor) return false;
		if(!Readable(actor, 0x140)) return false;

		Chain chain;
		if(!ActorChain(actor, chain)) return false;

		return Get3D((TESObjectREFR *)actor, false) != NULL ||
			   Get3D((TESObjectREFR *)actor, true)  != NULL;
	}
}
