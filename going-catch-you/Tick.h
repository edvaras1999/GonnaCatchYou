// Tick.h - the heartbeat.
//
// The plugin has no per-frame callback of its own on this edition. What it
// does have, proven in the FreeRun NPC project, is the camera: the player's
// third person and first person camera states each have an Update that the
// game calls once per frame while that camera is active, on the main thread.
// Hooking that slot gives a heartbeat in step with the renderer.
//
// A first attempt used a channel bound into the player's animation graph. The
// channel was created, but the engine never polled it: it only feeds graph
// variables that exist in the loaded behaviour, and this plugin has no
// behaviour file. The camera does not depend on one.
//
// Two plugins may hook the same slot: each hook calls on to whatever was in
// the vtable before it, so FreeRun's own camera hooks keep working.

#pragma once

namespace Tick
{
	bool InstallHooks();	// the two camera-state vtable hooks
}
