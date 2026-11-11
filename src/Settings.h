#pragma once

#include "sim/Sim.h"

namespace fb
{
	// Data\F4SE\Plugins\fo4bowling.ini. Read at startup and again at the start of every game, so edits apply without
	// restarting.
	struct Settings
	{
		static Settings& Get()
		{
			static Settings instance;
			return instance;
		}

		void Load();

		// a key's name for prompts ("F", "Numpad 7")
		static std::string Key(int a_vk);

		// [Keys] Windows virtual-key codes (0 = off). Starting a game is the game's own activate key, at a ball return.
		int   keyLeft{ 'A' };         // move along the foul line to your left (held)
		int   keyRight{ 'D' };        // ...to your right
		int   keyFewer{ VK_LEFT };    // at the ball return: fewer players
		int   keyMore{ VK_RIGHT };    // ...more
		int   keyQuit{ 0x58 };        // X, twice: stop the game
		float activateDistance{ 240.0f };  // how close to a ball return F works

		// [Throw] the mouse aims (the camera stays put); hold the left button, pull back for power, let go to throw
		float minSpeed{ 300.0f };      // the ball's speed at the least power, units per second
		float maxSpeed{ 820.0f };      // ...and at full power
		float pullPerCount{ 0.002f };  // power per count of mouse pull (500 counts: full)
		bool  invertPull{ false };     // pushing forward gives power instead
		float aimPerCount{ 0.012f };   // degrees of aim per count of sideways mouse movement
		float maxAim{ 6.0f };          // the most the throw can angle off straight, degrees
		float revs{ 65.0f };           // spin, radians per second (about 620 rpm): the more, the more it can hook
		float maxHook{ 50.0f };        // how far full hook turns the spin axis sideways, degrees
		float spinPerCount{ 0.25f };   // percent of full hook per count of sideways mouse movement during the swing (0 = off)
		float moveSpeed{ 60.0f };      // how fast you move along the foul line, units per second
		float inaccuracy{ 1.0f };      // how much each throw strays from what you set up (0 = never; harder throws stray more)
		float laneDrift{ 1.0f };       // how much the lane itself drifts a ball left or right, a little differently each time (0 = never)

		// [Guide] the green line from the ball, along your aim
		bool  guide{ true };
		float guideLength{ 420.0f };   // how far down the lane it goes
		bool  guideFlipY{ true };      // (if the line shows up mirrored top to bottom, set 0)

		// [Camera]
		float camDistance{ 95.0f };   // behind the ball while you aim
		float camHeight{ 30.0f };     // above it
		float camPitch{ 10.0f };      // degrees down
		bool  followBall{ true };     // the camera rolls along behind the ball
		float followStop{ 760.0f };   // ...and stops this far down the lane to watch the pins

		// [Lane] fine-tuning where the lane is (both lanes)
		float laneOffsetX{ 0.0f };    // + moves everything towards the pins
		float laneOffsetY{ 0.0f };    // + moves it to the bowler's left
		float laneOffsetZ{ 0.0f };

		// [Return] where the balls sit on the ball return, in its own frame (y along it towards the hood)
		float returnSpot1{ 37.5f };
		float returnSpot2{ 57.5f };
		float returnZ{ 53.6f };
		float returnHood{ 110.0f };   // where a returning ball comes out, inside the hood
		float returnSpeed{ 70.0f };   // how fast it rolls down the return

		// [Sound] real alley recordings, positioned where they happen
		bool  sound{ true };
		float soundVolume{ 0.8f };
		float soundFullDistance{ 300.0f };  // full volume within this far of the camera...
		float soundMaxDistance{ 3500.0f };  // ...fading to nothing here

		// [Pinsetter] our own sweep bar and deck over the pins
		bool  pinsetter{ true };
		float deckHeight{ 110.0f };   // where the deck waits, its underside above the lane
		float sweepHeight{ 100.0f };  // where the sweep bar waits
		bool          pinLights{ true };               // lights over the pins while a game's on
		std::uint32_t pinLightForm{ 0x0010C56A };      // the light used (instituteLightWarm01NS: warm, bright, no shadows)
		float         pinLightHeight{ 70.0f };         // how high they hang over the lane
		float         pinLightBrightness{ 0.85f };     // their strength, as a share of the light's own

		// [HUD]
		float hudScale{ 1.0f };
		bool  notifications{ false };  // calls ("Strike!") as the game's own corner notifications too

		// [Physics] everything about the lane, ball and pins
		bowl::Spec spec;
		float      pinJitter{ 0.3f };  // how far off its spot the pinsetter may put a pin
	};
}
