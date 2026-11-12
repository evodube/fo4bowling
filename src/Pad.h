#pragma once

// An Xbox-style controller, read straight from XInput (xinput1_4, or the older xinput9_1_0) each frame: the first one
// plugged in. Sticks and triggers come back -1..1 / 0..1, past their dead zone.
namespace fb::Pad
{
	struct State
	{
		bool          connected{ false };
		float         lx{ 0.0f }, ly{ 0.0f };  // left stick (right, up = +)
		float         rx{ 0.0f }, ry{ 0.0f };  // right stick
		float         lt{ 0.0f }, rt{ 0.0f };  // triggers
		std::uint16_t buttons{ 0 };            // XINPUT_GAMEPAD_* bits
	};

	State Poll(float a_deadzone);

	// a short buzz (0..1 on the heavy and light motors), stopped by Update once it's run its time
	void Rumble(float a_heavy, float a_light, float a_seconds);
	void Update(float a_dt);
	void StopRumble();

	inline constexpr std::uint16_t kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
	inline constexpr std::uint16_t kLB = 0x0100, kRB = 0x0200;
	inline constexpr std::uint16_t kLeft = 0x0004, kRight = 0x0008, kUp = 0x0001, kDown = 0x0002;
}
