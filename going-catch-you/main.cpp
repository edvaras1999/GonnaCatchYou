// Going Catch You
//
// NPCs that go after the player the way a player would: over, up and around,
// not just along the navmesh. Skyrim Legendary Edition 1.9.32.0.8, SKSE 1.7.3.
//
// WHERE IT STANDS
//
//   - It reads: which NPCs hunt the player (in combat, with the player as the
//     target), how they move, and what is in front of them.
//   - Version 0.4.0 adds the explorer, which finds out how an NPC's movement and
//     combat AI are laid out in memory (names every object from the game's own
//     RTTI, looks for paths, correlates fields). It changes nothing.
//   - The hop over low obstacles, FreeRun's vault and the test keys are still in
//     the code and are off by default.
//
// It is a plugin of its own. It does not patch FreeRun and does not need it.
//
// The engine access, the memory checks, the startup verification and the
// heartbeat come from the FreeRun NPC project (see NOTICE.md).

#include "skse/PluginAPI.h"
#include "skse/skse_version.h"
#include "skse/GameAPI.h"

#include "Engine.h"
#include "Settings.h"
#include "Tick.h"
#include "Observer.h"
#include "Verify.h"
#include "Log.h"

#include <shlobj.h>

#define PLUGIN_NAME				"GoingCatchYou"
#define PLUGIN_LOG_FILE			"\\My Games\\Skyrim\\SKSE\\GoingCatchYou.log"
#define PLUGIN_VERSION_STRING	"0.5.1-leap"

static PluginHandle				g_pluginHandle	= kPluginHandle_Invalid;
static SKSEMessagingInterface *	g_messaging		= NULL;

namespace
{
	// Set when the startup check says this is not the game the plugin was built
	// for. Off means off: no hooks, and no message handled afterwards either.
	bool s_disabled = false;

	void OnMessage(SKSEMessagingInterface::Message * message)
	{
		if(s_disabled) return;

		switch(message->type)
		{
			case SKSEMessagingInterface::kMessage_DataLoaded:
			{
				Settings::Load();
				if(!Settings::enabled)
				{
					s_disabled = true;
					_MESSAGE("bEnabled=0: nothing is hooked");
					break;
				}

				// Before anything is hooked: is this the executable the plugin
				// was written against? See Verify.cpp.
				const Verify::Result check = Verify::Run();
				if(!check.ok)
				{
					s_disabled = true;
					_ERROR("ABORTING: the game does not match what this plugin was built for - "
						   "no hooks installed, nothing further handled, see the lines above");
					break;
				}

				if(Tick::InstallHooks())
					_MESSAGE("hooks installed");
				else
					_ERROR("the heartbeat hook did not install: the plugin will see nothing");
				break;
			}

			case SKSEMessagingInterface::kMessage_PreLoadGame:
			case SKSEMessagingInterface::kMessage_PostLoadGame:
			case SKSEMessagingInterface::kMessage_NewGame:
				// Anything watched belongs to a world that is gone.
				Observer::Reset();
				break;

			default:
				break;
		}
	}
}

extern "C"
{

bool SKSEPlugin_Query(const SKSEInterface * skse, PluginInfo * info)
{
	gLog.OpenRelative(CSIDL_MYDOCUMENTS, PLUGIN_LOG_FILE);

	_MESSAGE("Going Catch You " PLUGIN_VERSION_STRING " (built " __DATE__ " " __TIME__ ")");

	info->infoVersion	= PluginInfo::kInfoVersion;
	info->name			= PLUGIN_NAME;
	info->version		= 1;

	g_pluginHandle = skse->GetPluginHandle();

	if(skse->isEditor)
	{
		_MESSAGE("ABORTING: loaded in the editor");
		return false;
	}

	// Every address in this plugin was read out of this one build. On anything
	// else it does nothing at all rather than write to the wrong place.
	if(skse->runtimeVersion != RUNTIME_VERSION_1_9_32_0)
	{
		_MESSAGE("ABORTING: unsupported runtime %08X, expected 1.9.32.0 (%08X)",
				 skse->runtimeVersion, RUNTIME_VERSION_1_9_32_0);
		return false;
	}

	return true;
}

bool SKSEPlugin_Load(const SKSEInterface * skse)
{
	g_messaging = (SKSEMessagingInterface *)skse->QueryInterface(kInterface_Messaging);
	if(!g_messaging || !g_messaging->RegisterListener(g_pluginHandle, "SKSE", OnMessage))
	{
		_MESSAGE("ABORTING: the messaging interface is not available");
		return false;
	}

	_MESSAGE("running");
	return true;
}

};
