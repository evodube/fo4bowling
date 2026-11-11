// AI bowler tests. Build and run on any machine:
//   g++ -std=c++20 -O2 -Isrc tests/ai_test.cpp src/sim/Ai.cpp src/sim/Sim.cpp -o ai_test && ./ai_test

#include "sim/Ai.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace bowl;

namespace
{
	int g_failed = 0;
	int g_checks = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		++g_checks;
		if (!a_ok) {
			++g_failed;
			std::printf("    FAIL: %s\n", a_what.c_str());
		}
	}

	std::string F(double a_v) { return std::to_string(a_v); }

}

int main()
{
	const ThrowLimits limits;
	std::printf("strike ball\n");
	for (int hand = 0; hand < 2; ++hand) {
		const bool left = hand == 1;
		Sim        s;
		const auto plan = PlanThrow(s.GetSpec(), s.Pins(), 0.8, left, limits);
		Check(left ? plan.release.axis < 0.0 : plan.release.axis > 0.0, std::string(left ? "a left-hander" : "a right-hander") + " hooks the strike ball");
		Check(left ? plan.release.y > 0.0 : plan.release.y < 0.0, "...from their own side of the lane");
		// where it really meets the pins
		Sim t;
		t.Throw(plan.release);
		double entry = 1e9;
		while (!t.Settled()) {
			t.Step();
			if (t.GetBall().state == BallState::kLane && t.GetBall().pos.x < t.GetSpec().headPin - 15.0) {
				entry = t.GetBall().pos.y;
			}
		}
		const double want = left ? 5.5 : -5.5;
		Check(std::abs(entry - want) < 1.5, "the planned line meets the pocket (" + F(entry) + ")");
		Check(t.DownThisBall() >= 8, "a pocket hit takes most of the rack (" + std::to_string(t.DownThisBall()) + ")");
	}

	std::printf("spares\n");
	{
		// single pins left standing: the AI's line reaches each and knocks it down, for most of them
		int made = 0;
		for (int pin = 0; pin < Sim::kPins; ++pin) {
			// everything but this pin swept away; the AI plans for it, then throws it
			Sim t;
			t.KeepOnly(1u << pin);
			const auto plan = PlanThrow(t.GetSpec(), t.Pins(), 0.8, false, limits);
			t.Throw(plan.release);
			while (!t.Settled()) {
				t.Step();
			}
			made += t.StandingCount() == 0 ? 1 : 0;
		}
		std::printf("    single-pin spares made: %d of 10\n", made);
		Check(made >= 8, "the AI makes most single-pin spares with a perfect throw");
	}

	{
		// a 7-10 split's key pin is a back corner: the line still goes for one of them
		Sim t;
		t.KeepOnly((1u << 6) | (1u << 9));
		const auto plan = PlanThrow(t.GetSpec(), t.Pins(), 0.8, false, limits);
		t.Throw(plan.release);
		while (!t.Settled()) {
			t.Step();
		}
		Check(t.StandingCount() <= 1, "at a 7-10 it takes at least one of them");
	}

	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed == 0 ? 0 : 1;
}
