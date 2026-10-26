// Observer.h - step 1: watch the NPCs that are going after the player, change nothing.
#pragma once

namespace Observer
{
	// Called by the heartbeat (the camera-state Update hooks), once a frame. Cheap when nothing is due.
	void OnTick();

	// Forget every watched NPC. Called around loads and new games.
	void Reset();
}
