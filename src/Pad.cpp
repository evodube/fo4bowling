#include "Pad.h"

#include <xinput.h>

namespace fb::Pad
{
	namespace
	{
		using GetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
		using SetState_t = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);

		GetState_t g_getState{ nullptr };
		SetState_t g_setState{ nullptr };
		bool       g_loaded{ false };
		int        g_user{ -1 };     // the controller we last read
		int        g_searchIn{ 0 };  // (frames until we look for one again when none's plugged in)
		float      g_rumbleLeft{ 0.0f };

		void Load()
		{
			if (g_loaded) {
				return;
			}
			g_loaded = true;
			for (const auto* name : { "xinput1_4.dll", "xinput9_1_0.dll", "xinput1_3.dll" }) {
				if (auto* dll = ::LoadLibraryA(name)) {
					g_getState = reinterpret_cast<GetState_t>(::GetProcAddress(dll, "XInputGetState"));
					g_setState = reinterpret_cast<SetState_t>(::GetProcAddress(dll, "XInputSetState"));
					if (g_getState) {
						log::info("controller: XInput from {}", name);
						return;
					}
				}
			}
			log::warn("controller: no XInput on this system");
		}

		float Stick(SHORT a_v, float a_dead)
		{
			const float v = std::clamp(static_cast<float>(a_v) / 32767.0f, -1.0f, 1.0f);
			const float m = std::abs(v);
			if (m <= a_dead) {
				return 0.0f;
			}
			const float t = (m - a_dead) / (1.0f - a_dead);
			return std::copysign(t * t, v);  // (squared: fine control near the middle)
		}

		float Trigger(BYTE a_v) { return std::clamp((static_cast<float>(a_v) - 20.0f) / 235.0f, 0.0f, 1.0f); }

		void Set(float a_heavy, float a_light)
		{
			if (!g_setState || g_user < 0) {
				return;
			}
			XINPUT_VIBRATION v{ static_cast<WORD>(std::clamp(a_heavy, 0.0f, 1.0f) * 65535.0f),
				static_cast<WORD>(std::clamp(a_light, 0.0f, 1.0f) * 65535.0f) };
			g_setState(static_cast<DWORD>(g_user), &v);
		}
	}

	State Poll(float a_deadzone)
	{
		Load();
		State out;
		if (!g_getState) {
			return out;
		}
		XINPUT_STATE xs{};
		if (g_user >= 0 && g_getState(static_cast<DWORD>(g_user), &xs) != ERROR_SUCCESS) {
			g_user = -1;
		}
		if (g_user < 0) {
			// (asking after an empty slot is slow on some systems: only a couple of times a second)
			if (g_searchIn-- > 0) {
				return out;
			}
			g_searchIn = 30;
			for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
				if (g_getState(i, &xs) == ERROR_SUCCESS) {
					g_user = static_cast<int>(i);
					log::info("controller {} found", i);
					break;
				}
			}
			if (g_user < 0) {
				return out;
			}
		}
		const auto& g = xs.Gamepad;
		const float dead = std::clamp(a_deadzone, 0.0f, 0.9f);
		out.connected = true;
		out.lx = Stick(g.sThumbLX, dead);
		out.ly = Stick(g.sThumbLY, dead);
		out.rx = Stick(g.sThumbRX, dead);
		out.ry = Stick(g.sThumbRY, dead);
		out.lt = Trigger(g.bLeftTrigger);
		out.rt = Trigger(g.bRightTrigger);
		out.buttons = g.wButtons;
		return out;
	}

	void Rumble(float a_heavy, float a_light, float a_seconds)
	{
		Set(a_heavy, a_light);
		g_rumbleLeft = a_seconds;
	}

	void Update(float a_dt)
	{
		if (g_rumbleLeft > 0.0f) {
			g_rumbleLeft -= a_dt;
			if (g_rumbleLeft <= 0.0f) {
				Set(0.0f, 0.0f);
			}
		}
	}

	void StopRumble()
	{
		if (g_rumbleLeft > 0.0f) {
			g_rumbleLeft = 0.0f;
			Set(0.0f, 0.0f);
		}
	}
}
