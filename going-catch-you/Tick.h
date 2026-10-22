// Tick.h - the heartbeat.
//
// The plugin has no per-frame callback of its own on this edition. What it does
// have, proven in the FreeRun NPC project, is a channel bound into the player's
// animation graph: the engine polls every bound channel once per animation
// update, so a channel is a heartbeat that runs on the game's own thread.
//
// The channel is named differently from FreeRun's, so the two plugins can sit
// side by side: each one appends its own channel, and each hook calls on to
// whatever was in the vtable slot before it.

#pragma once

namespace Tick
{
	bool InstallHooks();	// the vtable hook that binds the channel
}
