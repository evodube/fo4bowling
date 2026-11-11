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
		int   keyLeft{ VK_LEFT };    // move along the foul line (held), or fewer players at the ball return
		int   keyRight{ VK_RIGHT };
		int   keyHookLeft{ VK_UP };     // more hook to the left (for a right-hander: the usual hook)
		int   keyHookRight{ VK_DOWN };  // more hook to the right
		int   keyQuit{ 0x58 };       // X, twice: stop the game
		float activateDistance{ 240.0f };  // how close to a ball return F works

		// [Throw] hold the left mouse button, pull the mouse back for power, let go to throw
		float minSpeed{ 300.0f };      // the ball's speed at the least power, units per second
		float maxSpeed{ 820.0f };      // ...and at full power
		float pullPerCount{ 0.002f };  // power per count of mouse pull (500 counts: full)
		bool  invertPull{ false };     // pushing forward gives power instead
		float revs{ 30.0f };           // spin, radians per second (about 290 rpm)
		float maxHook{ 60.0f };        // degrees the spin axis can be turned
		float hookStep{ 5.0f };        // degrees per press of the hook keys
		float moveSpeed{ 60.0f };      // how fast the arrow keys move you along the foul line, units per second
		float maxAim{ 6.0f };          // the most the throw can angle off straight, degrees

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

		// [HUD]
		float hudScale{ 1.0f };
		bool  notifications{ false };  // calls ("Strike!") as the game's own corner notifications too

		// [Physics] everything about the lane, ball and pins
		bowl::Spec spec;
	};
}
