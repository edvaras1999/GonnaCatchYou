#include "Manual.h"
#include "Settings.h"
#include "Log.h"

namespace
{
	Manual::Request	s_pending = Manual::kNone;
	DWORD			s_pendingMs = 0;
	bool			s_prevArc = false;
	bool			s_prevFree = false;

	bool KeyDown(int vk)
	{
		return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
	}

	// GetAsyncKeyState sees the keyboard whatever window has it.
	bool GameIsForeground()
	{
		HWND hwnd = GetForegroundWindow();
		if(!hwnd) return false;
		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		return pid == GetCurrentProcessId();
	}
}

namespace Manual
{
	void OnTick(DWORD now)
	{
		if(!Settings::manualKeys) return;

		// A request nobody picked up (the sample could not run) does not stay
		// around forcing a sample every frame.
		if(s_pending != kNone && now - s_pendingMs > 2000)
		{
			s_pending = kNone;
			_MESSAGE("manual: the request expired without a sample to carry it out");
		}

		if(!GameIsForeground())
		{
			s_prevArc = s_prevFree = false;
			return;
		}

		const bool arc = KeyDown(Settings::keyArc);
		const bool free = KeyDown(Settings::keyFreeRun);

		// A request is made once per press, not once per frame the key is down.
		if(arc && !s_prevArc)
		{
			s_pending = kArc;
			s_pendingMs = now;
			_MESSAGE("manual: the arc key was pressed");
		}
		else if(free && !s_prevFree)
		{
			s_pending = kFreeRun;
			s_pendingMs = now;
			_MESSAGE("manual: the FreeRun key was pressed");
		}

		s_prevArc = arc;
		s_prevFree = free;
	}

	Request Pending()	{ return s_pending; }
	void Clear()		{ s_pending = kNone; }
}
