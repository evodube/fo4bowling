#pragma once

namespace fb::Audio
{
	enum class Bank
	{
		kRelease,  // the ball leaving the hand and landing on the lane
		kRoll,     // a ball rolling down the lane (looped)
		kCrash,    // the ball into a full rack
		kHit,      // the ball into a few pins
		kPin,      // pin on pin, pin on the deck or a kickback
		kMachine,  // the pinsetter running through its cycle
		kSet,      // the pinsetter's deck setting the pins down
		kCount
	};

	// Loads Data\Sound\fo4bowling\*.wav (real alley recordings, see CREDITS.txt) and starts XAudio2. Safe to call
	// more than once.
	void Init();

	// One of the bank's takes at a_pos in the world. a_gain 0..1 is how hard; the distance from the camera and the
	// [Sound] volume go on top.
	void Play(Bank a_bank, const RE::NiPoint3& a_pos, float a_gain, float a_pitch = 1.0f);

	// The alley's PA calling a ball (Data\Sound\fo4bowling\announce_*.wav): heard the same anywhere in the alley,
	// at the [Sound] volume times the announcer's.
	enum class Call
	{
		kStrike,
		kSpare,
		kTurkey,  // a third strike in a row
		kGutter,
		kCount
	};
	void Announce(Call a_call);

	// The rolling ball: started once, moved and turned up or down every frame, stopped (faded) when it's done.
	void Roll(const RE::NiPoint3& a_pos, float a_gain, float a_pitch);
	void StopRoll();

	// Which way the listener faces (radians, the game's heading: 0 = +y). Set every frame while a game's on.
	void SetListener(const RE::NiPoint3& a_pos, float a_yaw, bool a_valid);
}
