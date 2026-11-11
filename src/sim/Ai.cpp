#include "sim/Ai.h"

#include <algorithm>
#include <cmath>

namespace bowl
{
	namespace
	{
		bool Standing(const Pin& a_pin)
		{
			return a_pin.state == PinState::kStanding;
		}

		// where a ball thrown like this is across the lane when it reaches a_x (or +-1000 if it leaves the lane first)
		double YAt(const Spec& a_spec, const Release& a_release, double a_x)
		{
			Sim s(a_spec);
			s.ClearPins();  // (only the ball's path: nothing in the way)
			s.Throw(a_release);
			while (s.GetBall().state == BallState::kLane && s.GetBall().pos.x < a_x && s.TimeSinceThrow() < 6.0) {
				s.Step();
			}
			if (s.GetBall().state != BallState::kLane) {
				return s.GetBall().pos.y > 0.0 ? 1000.0 : -1000.0;
			}
			return s.GetBall().pos.y;
		}

		// the angle that takes the ball through (a_x, a_y), or false if no angle within the limits does
		bool SolveAngle(const Spec& a_spec, Release& a_release, double a_x, double a_y, double a_maxAngle)
		{
			double lo = -a_maxAngle;
			double hi = a_maxAngle;
			Release r = a_release;
			r.angle = lo;
			const double yLo = YAt(a_spec, r, a_x);
			r.angle = hi;
			const double yHi = YAt(a_spec, r, a_x);
			if (a_y < yLo || a_y > yHi) {
				return false;  // (more angle always means further left: out of reach both ends)
			}
			for (int i = 0; i < 22; ++i) {
				r.angle = 0.5 * (lo + hi);
				const double y = YAt(a_spec, r, a_x);
				if (y < a_y) {
					lo = r.angle;
				} else {
					hi = r.angle;
				}
			}
			a_release.angle = 0.5 * (lo + hi);
			return true;
		}
	}

	AiThrow PlanThrow(const Spec& a_spec, const std::array<Pin, Sim::kPins>& a_pins, double a_skill, bool a_leftHanded, const ThrowLimits& a_limits)
	{
		const double skill = std::clamp(a_skill, 0.0, 1.0);
		const double side = a_leftHanded ? -1.0 : 1.0;  // a right-hander stands right (-y) and hooks left (+y)
		const double edge = a_spec.laneHalfWidth - a_spec.ballRadius - 1.0;

		int    count = 0;
		int    front = -1;
		double sumY = 0.0;
		for (int i = 0; i < Sim::kPins; ++i) {
			const auto& p = a_pins[static_cast<std::size_t>(i)];
			if (!Standing(p)) {
				continue;
			}
			++count;
			sumY += p.com.y;
			const auto& f = front >= 0 ? a_pins[static_cast<std::size_t>(front)] : p;
			if (front < 0 || p.com.x < f.com.x - 2.0 || (std::abs(p.com.x - f.com.x) <= 2.0 && std::abs(p.com.y) < std::abs(f.com.y))) {
				front = i;
			}
		}

		AiThrow plan;
		Release r;
		r.revs = a_limits.revs;
		double tx = a_spec.headPin - a_spec.pinRadius - a_spec.ballRadius - 3.0;
		double ty = 0.0;
		std::array<double, 5> starts{};
		if (count == 0 || front < 0) {
			// (nothing to hit: roll it down the middle)
			plan.power = 0.5;
			r.y = 0.0;
			r.axis = 0.0;
		} else if (count == Sim::kPins) {
			// the strike ball: into the pocket with a hook, harder and with more hook the better they are
			plan.power = 0.45 + 0.3 * skill;
			r.axis = side * a_limits.maxAxis * (0.35 + 0.35 * skill);
			ty = -side * 5.5;
			starts = { -side * 30.0, -side * 38.0, -side * 22.0, -side * 45.0, -side * 14.0 };
		} else {
			// a spare: the key pin (the front one), hit on the side the rest of them are on, with a straight-ish ball
			// from across the lane
			const auto&  key = a_pins[static_cast<std::size_t>(front)];
			const double rest = count > 1 ? (sumY - key.com.y) / (count - 1) : key.com.y;
			const double lean = count > 1 ? std::clamp((rest - key.com.y) * 0.4, -6.0, 6.0) : 0.0;
			tx = key.com.x - a_spec.pinRadius - a_spec.ballRadius - 3.0;
			ty = key.com.y + lean;
			plan.power = 0.55 + 0.2 * skill;
			r.axis = side * a_limits.maxAxis * 0.12 * skill;
			const double across = ty > 4.0 ? -1.0 : ty < -4.0 ? 1.0 : -side;  // (cross-lane: start on the other side)
			starts = { across * 25.0, across * 15.0, across * 35.0, 0.0, -across * 10.0 };
		}
		r.speed = a_limits.minSpeed + (a_limits.maxSpeed - a_limits.minSpeed) * std::pow(plan.power, 1.3);
		plan.target = { tx, ty };

		if (count > 0 && front >= 0) {
			bool found = false;
			for (const double y0 : starts) {
				Release tryIt = r;
				tryIt.y = std::clamp(y0, -edge, edge);
				if (SolveAngle(a_spec, tryIt, tx, ty, a_limits.maxAngle)) {
					r = tryIt;
					found = true;
					break;
				}
			}
			if (!found) {
				// (no line reaches it with this spin: straight at it from where it is)
				r.axis = 0.0;
				r.y = std::clamp(ty, -edge, edge);
				SolveAngle(a_spec, r, tx, ty, a_limits.maxAngle);
			}
		}
		plan.release = r;
		return plan;
	}
}
