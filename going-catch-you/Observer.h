// Observer.h - step 1: watch the NPCs that are going after the player, change nothing.
#pragma once

class Actor;

namespace Observer
{
	// Called by the heartbeat, many times a second. Cheap when nothing is due.
	void OnTick(Actor * owner);

	// Forget every watched NPC. Called around loads and new games.
	void Reset();
}
