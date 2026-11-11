#pragma once

// How an AI bowler (a companion) picks its throw. Plain C++, tested in tests/ai_test.cpp.
//
// At a full rack it goes for the pocket (the 1-3 for a right-hander, the 1-2 for a left-hander) with a hook ball;
// at a spare it picks the key pin (the front one of what's left) and the side of it that carries the rest, and throws
// a straighter ball at it from across the lane. The line is found the way a bowler finds it: the ball's own path,
// worked out ahead (the same physics), until it crosses the target. How well the throw then matches that plan is
// down to the bowler's skill (the game adds the misses).

#include "sim/Sim.h"

namespace bowl
{
	struct ThrowLimits
	{
		double minSpeed{ 300.0 };
		double maxSpeed{ 820.0 };
		double revs{ 65.0 };
		double maxAxis{ 0.87 };   // radians: full hook
		double maxAngle{ 0.105 }; // radians either side of straight
	};

	struct AiThrow
	{
		Release release;
		double  power{ 0.6 };   // 0..1, as a player's pull would be
		Vec2    target;         // where it means the ball to cross (for the log)
	};

	// a_skill 0..1: a better bowler puts more hook and pace on it and picks its lines better
	AiThrow PlanThrow(const Spec& a_spec, const std::array<Pin, Sim::kPins>& a_pins, double a_skill, bool a_leftHanded, const ThrowLimits& a_limits);
}
