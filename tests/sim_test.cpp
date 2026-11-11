// Bowling physics tests. Build and run on any machine:
//   g++ -std=c++20 -O2 -Isrc tests/sim_test.cpp src/sim/Sim.cpp -o sim_test && ./sim_test

#include "sim/Sim.h"

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

	// rolls the ball until it reaches a_x (or leaves the lane); its y there
	double YAt(Sim& a_sim, double a_x)
	{
		while (a_sim.GetBall().state == BallState::kLane && a_sim.GetBall().pos.x < a_x && a_sim.TimeSinceThrow() < 10.0) {
			a_sim.Step();
		}
		return a_sim.GetBall().pos.y;
	}

	void RunOut(Sim& a_sim)
	{
		while (!a_sim.Settled()) {
			a_sim.Step();
		}
	}

	// where the ball meets the pins: its y just before the head pin
	double Entry(const Release& a_r, int& a_down)
	{
		Sim    s;
		double y = 1e9;
		s.Throw(a_r);
		while (!s.Settled()) {
			s.Step();
			if (s.GetBall().state == BallState::kLane && s.GetBall().pos.x < s.GetSpec().headPin - 14.0) {
				y = s.GetBall().pos.y;
			}
		}
		a_down = s.DownThisBall();
		return y;
	}
}

int main()
{
	std::printf("rack\n");
	{
		Sim s;
		const auto& spec = s.GetSpec();
		Check(std::abs(s.Spot(0).x - spec.headPin) < 1e-9 && std::abs(s.Spot(0).y) < 1e-9, "the head pin is on its spot in the middle");
		Check(s.Spot(6).y > 0.0 && s.Spot(9).y < 0.0, "the 7 is on the left (+y), the 10 on the right");
		Check(std::abs((s.Spot(1) - s.Spot(0)).Length() - spec.pinSpacing) < 1e-6, "pins are a pin spacing apart");
		Check(std::abs((s.Spot(9) - s.Spot(6)).Length() - 3.0 * spec.pinSpacing) < 1e-6, "the back row is three spacings wide");
		Check(s.StandingCount() == 10 && s.StandingMask() == 0x3FF, "a full rack stands");

		// the pinsetter's never exact: each pin a little off its spot, the same way for the same seed
		Sim a, b, c;
		a.Rack(0.3, 7);
		b.Rack(0.3, 7);
		c.Rack(0.3, 8);
		bool within = true, same = true, differs = false;
		for (int i = 0; i < Sim::kPins; ++i) {
			const auto& pa = a.Pins()[static_cast<std::size_t>(i)];
			const Vec2  at{ pa.com.x, pa.com.y };
			within = within && (at - a.Spot(i)).Length() <= 0.3 + 1e-9;
			same = same && (pa.com - b.Pins()[static_cast<std::size_t>(i)].com).Length() < 1e-12;
			differs = differs || (pa.com - c.Pins()[static_cast<std::size_t>(i)].com).Length() > 1e-6;
		}
		Check(within, "a jittered rack keeps every pin within the jitter of its spot");
		Check(same && differs, "the same seed racks the same; another seed doesn't");
	}

	std::printf("ball\n");
	{
		Sim     s;
		Release r;
		r.y = 20.0;
		r.speed = 650.0;
		r.revs = 30.0;
		r.axis = 0.0;
		s.Throw(r);
		const double y = YAt(s, 1000.0);
		Check(std::abs(y - 20.0) < 0.5, "no side spin: it goes straight (y " + F(y) + ")");
		Check(s.GetBall().vel.x > 450.0, "and keeps most of its speed (" + F(s.GetBall().vel.x) + ")");
	}
	{
		double ends[2]{};
		for (int side = 0; side < 2; ++side) {
			Sim     s;
			Release r;
			r.y = side == 0 ? -40.0 : 40.0;
			r.speed = 650.0;
			r.revs = 30.0;
			r.axis = side == 0 ? 0.6 : -0.6;
			s.Throw(r);
			const double mid = YAt(s, s.GetSpec().oilLength * 0.6);
			const double end = YAt(s, 1000.0);
			ends[side] = end;
			const double early = std::abs(mid - r.y);
			const double late = std::abs(end - mid);
			Check(late > 2.0 * early, std::string(side == 0 ? "left" : "right") + " hook: most of it after the oil (" + F(early) + " then " + F(late) + ")");
		}
		Check(ends[0] > -20.0, "spin to the left hooks left (from -40 to " + F(ends[0]) + ")");
		Check(ends[1] < 20.0, "spin to the right hooks right (from 40 to " + F(ends[1]) + ")");
		Check(std::abs(ends[0] + ends[1]) < 1e-6, "left and right hooks mirror each other");
	}
	{
		Sim     s;
		Release r;
		r.y = 30.0;
		r.angle = 0.06;  // straight off the lane
		r.speed = 600.0;
		s.Throw(r);
		RunOut(s);
		Check(s.GetBall().state == BallState::kTaken, "a gutter ball ends up in the pit, off the back wall and away into the machine");
		Check(s.DownThisBall() == 0 && s.StandingCount() == 10, "a gutter ball knocks nothing down");
	}

	{
		// into the slot at the end: the ball comes off the back wall to one side and never through it
		Sim     s;
		Release r;
		r.y = 0.0;
		r.angle = 0.0;
		r.speed = 650.0;
		s.Throw(r);
		const double wall = s.GetSpec().deckEnd + s.GetSpec().pitBack;
		bool         through = false, sideways = false;
		while (s.GetBall().state != BallState::kTaken && s.TimeSinceThrow() < 15.0) {
			s.Step();
			const auto& b = s.GetBall();
			through = through || b.pos.x + b.vel.x * 0.0 > wall;
			sideways = sideways || (b.state == BallState::kPit && std::abs(b.vel.y) > 40.0);
		}
		Check(!through, "the ball never goes through the back wall");
		Check(sideways, "it comes off the back wall to one side");
		Check(s.GetBall().state == BallState::kTaken, "and is taken into the machine");
	}

	{
		// called early (pins still going): what's standing stands straight and still, the rest stays down, and the
		// sweep keeps exactly what was called standing
		Release r;
		r.y = -6.0;
		r.speed = 650.0;
		Sim s;
		s.Throw(r);
		while (s.TimeSinceThrow() < 2.0) {
			s.Step();
		}
		s.ForceSettle();
		const auto called = s.StandingMask();
		bool       still = true;
		for (const auto& p : s.Pins()) {
			still = still && (p.vel.Length() == 0.0 || p.frozen || p.state == PinState::kSwept);
		}
		Check(s.Settled() && still, "an early call stops every pin");
		s.Sweep();
		Check(s.StandingMask() == called, "and the sweep keeps exactly the pins called standing");
	}

	std::printf("pins\n");
	{
		Sim     s;
		Release r;
		r.y = 0.0;
		r.speed = 650.0;
		s.Throw(r);
		RunOut(s);
		Check(s.DownThisBall() >= 3, "head on: the head pin and more go (" + std::to_string(s.DownThisBall()) + ")");
		Check(!(s.StandingMask() & 1u), "head on: the head pin's down");
		Check(s.TimeSinceThrow() < 12.0, "it settles before the give-up time");
		bool still = true;
		for (const auto& p : s.Pins()) {
			if ((p.state == PinState::kStanding || p.state == PinState::kDown) && p.vel.Length() > 5.0) {
				still = false;
			}
		}
		Check(still, "settled means nothing's sliding");

		// the second ball: the down pins are swept, the standing ones stand straight where they were
		const auto standing = s.StandingMask();
		s.Sweep();
		Check(s.StandingMask() == standing, "a sweep keeps the standing pins");
		for (int i = 0; i < Sim::kPins; ++i) {
			const auto& p = s.Pins()[static_cast<std::size_t>(i)];
			if (standing & (1u << i)) {
				Check(s.PinAxis(i).z > 0.9999 && p.state == PinState::kStanding && p.vel.Length() == 0.0,
					"pin " + std::to_string(i + 1) + " stands straight and still after the sweep");
			} else {
				Check(p.state == PinState::kSwept, "pin " + std::to_string(i + 1) + " is swept away");
			}
		}
		Release again;
		again.y = 60.0;
		again.angle = 0.05;  // into the gutter
		s.Throw(again);
		RunOut(s);
		Check(s.DownThisBall() == 0 && s.StandingMask() == standing, "a gutter ball on the second ball changes nothing");
	}
	{
		// a woken rack with nothing hitting it stands: pins balance on their bases and go back to sleep
		Sim     s;
		Release r;
		r.y = 30.0;
		r.angle = 0.06;  // (into the gutter: nothing touches the pins)
		s.Throw(r);
		RunOut(s);
		Check(s.StandingCount() == 10, "an untouched rack stays standing");
	}
	{
		// pins really fly: a full-on hit sends some into the pit and the gutters, and some land on the deck
		Release r;
		r.y = -5.0;
		r.speed = 750.0;
		Sim     s;
		s.Throw(r);
		int  gone = 0;
		bool airborne = false;
		while (!s.Settled()) {
			s.Step();
			for (const auto& p : s.Pins()) {
				airborne = airborne || p.com.z > s.GetSpec().pinCentre + 6.0;
			}
		}
		for (const auto& p : s.Pins()) {
			gone += p.state == PinState::kGone ? 1 : 0;
		}
		Check(gone >= 2, "a big hit puts pins in the pit and gutters (" + std::to_string(gone) + ")");
		Check(airborne, "pins leave the deck on a big hit");
		Check(s.TimeSinceThrow() < 8.0, "and it all settles in good time (" + F(s.TimeSinceThrow()) + " s)");
	}
	{
		// the lane's drift: a ball with drift ends up off the line a ball without it holds
		Release r;
		r.y = 0.0;
		r.speed = 600.0;
		Sim straight;
		straight.Throw(r);
		r.drift = 8.0;
		Sim drifting;
		drifting.Throw(r);
		const double a = YAt(straight, 900.0);
		const double b = YAt(drifting, 900.0);
		Check(b - a > 3.0 && b - a < 30.0, "lane drift moves the ball a few boards (" + F(b - a) + ")");
	}
	{
		// the same throw twice comes out the same
		Release r;
		r.y = -36.0;
		r.angle = 0.01;
		r.speed = 700.0;
		r.revs = 32.0;
		r.axis = 0.5;
		Sim a, b;
		a.Throw(r);
		b.Throw(r);
		RunOut(a);
		RunOut(b);
		Check(a.StandingMask() == b.StandingMask() && a.TimeSinceThrow() == b.TimeSinceThrow(), "the physics is deterministic");
	}
	{
		// hook balls into the pocket (right-handed: the 1-3; left-handed: the 1-2) strike often and leave little
		int    throws[2]{}, strikes[2]{}, pins[2]{};
		for (int hand = 0; hand < 2; ++hand) {
			const double sign = hand == 0 ? 1.0 : -1.0;
			for (double y0 = -50.0; y0 <= -10.0; y0 += 3.0) {
				for (double angle = -0.02; angle <= 0.0301; angle += 0.005) {
					for (double speed : { 550.0, 650.0, 750.0 }) {
						for (double axis : { 0.3, 0.5, 0.7 }) {
							Release r;
							r.y = sign * y0;
							r.angle = sign * angle;
							r.speed = speed;
							r.revs = 30.0;
							r.axis = sign * axis;
							int          down = 0;
							const double entry = sign * Entry(r, down);
							if (entry < -9.0 || entry > -3.0) {
								continue;
							}
							++throws[hand];
							pins[hand] += down;
							strikes[hand] += down == 10 ? 1 : 0;
						}
					}
				}
			}
			const double rate = throws[hand] ? static_cast<double>(strikes[hand]) / throws[hand] : 0.0;
			const double avg = throws[hand] ? static_cast<double>(pins[hand]) / throws[hand] : 0.0;
			std::printf("    %s pocket: %d throws, %.0f%% strikes, %.2f pins\n", hand == 0 ? "right-handed" : "left-handed", throws[hand], rate * 100.0, avg);
			Check(throws[hand] > 50, "enough pocket throws to judge");
			Check(rate > 0.4 && rate < 0.85, "pocket hits strike often but not always (" + F(rate) + ")");
			Check(avg > 8.0, "pocket hits average over 8 pins (" + F(avg) + ")");
		}
		// (the same, give or take the order the solver takes the pins in: a mirrored rack isn't solved in mirrored order,
		// and a pin bouncing back off the back wall into the rest makes that show)
		const double r0 = static_cast<double>(strikes[0]) / std::max(1, throws[0]);
		const double r1 = static_cast<double>(strikes[1]) / std::max(1, throws[1]);
		Check(std::abs(throws[0] - throws[1]) <= 3 && std::abs(r0 - r1) < 0.06, "left- and right-handed play the same (" + F(r0) + " / " + F(r1) + ")");
	}

	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed == 0 ? 0 : 1;
}
