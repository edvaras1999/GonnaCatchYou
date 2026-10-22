#include "Tick.h"
#include "Engine.h"
#include "Observer.h"
#include "Log.h"

#include "skse/GameReferences.h"
#include "skse/SafeWrite.h"

#include <windows.h>
#include <string.h>

namespace
{
	// The player's IAnimationGraphManagerHolder subobject has its own vtable.
	// Only the player's copy is patched, so no other actor changes behaviour.
	// Slot 0x0A is CreateAnimationChannels. Both the address and the slot were
	// verified in the FreeRun NPC project (see Signatures.h).
	UInt32 * const kPlayerHolderVtbl = (UInt32 *)0x010D1FB4;
	enum { kHolder_CreateChannels = 0x0A };

	// Appends a channel to the scrap array the engine passes to
	// CreateAnimationChannels, taking a reference exactly as the engine's own
	// channels do.
	typedef SInt32 (__thiscall * _AppendChannel)(void * scrapArray, void * channel);
	const _AppendChannel AppendChannel = (_AppendChannel)0x006BA780;

	// The graph variable this channel feeds. It does not exist in any behaviour
	// file, so the engine copies the value into nothing - the channel is only a
	// heartbeat.
	const char * const kChannelName = "GoingCatchYouTick";

	// A BSAnimationGraphChannel as this edition lays it out: vtable, reference
	// count, name, value. Everything past that is ours.
	struct Channel
	{
		const void **	vtbl;			// 0x00
		volatile LONG	refCount;		// 0x04
		BSFixedString	name;			// 0x08
		UInt32			value;			// 0x0C
		Actor *			owner;			// 0x10
	};

	Channel * s_channel = NULL;

	// The three virtuals, in this edition's order: destroy, poll, reset.
	void __fastcall Channel_Destroy(Channel * self, void *, UInt8 flags)
	{
		if(s_channel == self) s_channel = NULL;
		CALL_MEMBER_FN(&self->name, Release)();
		if(flags & 1)
			Engine::HeapFree(self, false);
	}

	// Special Edition's poll takes a bool. This edition's takes nothing - the
	// engine calls it with `this` in ECX and an empty stack.
	void __fastcall Channel_Poll(Channel * self, void *)
	{
		Observer::OnTick(self->owner);
	}

	void __fastcall Channel_Reset(Channel * self, void *)
	{
	}

	const void * s_channelVtbl[3] =
	{
		(const void *)&Channel_Destroy,
		(const void *)&Channel_Poll,
		(const void *)&Channel_Reset,
	};

	Channel * NewChannel(Actor * owner)
	{
		Channel * channel = (Channel *)Engine::HeapAllocate(sizeof(Channel), 0, false);
		if(!channel) return NULL;

		memset(channel, 0, sizeof(Channel));
		channel->vtbl		= s_channelVtbl;
		channel->refCount	= 0;				// the array below takes the first reference
		channel->owner		= owner;
		channel->value		= 0;				// 0.0f
		CALL_MEMBER_FN(&channel->name, Set)(kChannelName);
		return channel;
	}

	typedef bool (__thiscall * _CreateChannels)(void * holder, void * scrapArray);
	_CreateChannels s_origCreateChannels = NULL;

	bool __fastcall Hook_CreateChannels(void * holder, void *, void * scrapArray)
	{
		const bool result = s_origCreateChannels(holder, scrapArray);

		// The holder is a subobject at ref + 0x20; the reference itself starts
		// 0x20 bytes earlier.
		Actor * actor = (Actor *)((UInt8 *)holder - 0x20);

		Channel * channel = NewChannel(actor);
		if(channel)
		{
			AppendChannel(scrapArray, channel);
			s_channel = channel;
			_MESSAGE("heartbeat: channel bound to the player's animation graph");
		}

		return result;
	}
}

namespace Tick
{
	bool InstallHooks()
	{
		s_origCreateChannels = (_CreateChannels)kPlayerHolderVtbl[kHolder_CreateChannels];
		if(!s_origCreateChannels)
		{
			_ERROR("heartbeat: the player holder's CreateChannels slot is empty");
			return false;
		}

		SafeWrite32((UInt32)&kPlayerHolderVtbl[kHolder_CreateChannels], (UInt32)&Hook_CreateChannels);
		_MESSAGE("heartbeat: hook installed; the channel is bound the next time the player's graph is built (load a save)");
		return true;
	}
}
