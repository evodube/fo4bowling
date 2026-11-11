#include "sim/Sim.h"

#include <algorithm>
#include <cmath>

namespace bowl
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		constexpr double kHalfPi = kPi * 0.5;
		constexpr double kSqrt3 = 1.7320508075688772;
		constexpr double kSettleSeconds = 0.6;   // nothing moving this long: settled
		constexpr double kGiveUpSeconds = 12.0;  // a throw never takes longer than this
		constexpr double kSlideShare = 1.0;      // how much of a knock goes into moving the pin along
		constexpr double kTipShare = 0.3;        // ...and how much of its turn about the base goes into tipping it over
		                                         // (a pin's struck near its middle: it mostly flies, and topples as it goes)
		constexpr double kFlyingSpeed = 200.0;   // a pin this fast clears a gutter (and can bounce off a kickback)
		constexpr double kBellyHeight = 10.5;    // where a ball meets a standing pin

		double Clamp01(double a_v) { return std::clamp(a_v, 0.0, 1.0); }

		// closest points between segments p0-p1 and q0-q1: the fractions along each
		void ClosestOnSegments(const Vec3& a_p0, const Vec3& a_p1, const Vec3& a_q0, const Vec3& a_q1, double& a_s, double& a_t)
		{
			const Vec3   d1{ a_p1.x - a_p0.x, a_p1.y - a_p0.y, a_p1.z - a_p0.z };
			const Vec3   d2{ a_q1.x - a_q0.x, a_q1.y - a_q0.y, a_q1.z - a_q0.z };
			const Vec3   r{ a_p0.x - a_q0.x, a_p0.y - a_q0.y, a_p0.z - a_q0.z };
			const double a = d1.x * d1.x + d1.y * d1.y + d1.z * d1.z;
			const double e = d2.x * d2.x + d2.y * d2.y + d2.z * d2.z;
			const double f = d2.x * r.x + d2.y * r.y + d2.z * r.z;
			const double c = d1.x * r.x + d1.y * r.y + d1.z * r.z;
			const double b = d1.x * d2.x + d1.y * d2.y + d1.z * d2.z;
			const double denom = a * e - b * b;
			double       s = denom > 1e-9 ? Clamp01((b * f - c * e) / denom) : 0.0;
			double       t = e > 1e-9 ? (b * s + f) / e : 0.0;
			if (t < 0.0) {
				t = 0.0;
				s = a > 1e-9 ? Clamp01(-c / a) : 0.0;
			} else if (t > 1.0) {
				t = 1.0;
				s = a > 1e-9 ? Clamp01((b - c) / a) : 0.0;
			}
			a_s = s;
			a_t = t;
		}

		Vec3 Lerp(const Vec3& a_a, const Vec3& a_b, double a_t)
		{
			return { a_a.x + (a_b.x - a_a.x) * a_t, a_a.y + (a_b.y - a_a.y) * a_t, a_a.z + (a_b.z - a_a.z) * a_t };
		}
	}

	void RotateBy(double a_m[3][3], const Vec3& a_w, double a_dt)
	{
		const double wl = std::sqrt(a_w.x * a_w.x + a_w.y * a_w.y + a_w.z * a_w.z);
		const double angle = wl * a_dt;
		if (angle < 1e-9) {
			return;
		}
		const double kx = a_w.x / wl, ky = a_w.y / wl, kz = a_w.z / wl;
		const double c = std::cos(angle), s = std::sin(angle), t = 1.0 - c;
		const double r[3][3]{ { t * kx * kx + c, t * kx * ky - s * kz, t * kx * kz + s * ky },
			{ t * kx * ky + s * kz, t * ky * ky + c, t * ky * kz - s * kx },
			{ t * kx * kz - s * ky, t * ky * kz + s * kx, t * kz * kz + c } };
		double out[3][3]{};
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				out[i][j] = r[i][0] * a_m[0][j] + r[i][1] * a_m[1][j] + r[i][2] * a_m[2][j];
			}
		}
		// keep it a rotation (Gram-Schmidt on the columns) so rounding never skews the ball
		for (int col = 0; col < 3; ++col) {
			for (int prev = 0; prev < col; ++prev) {
				const double d = out[0][col] * out[0][prev] + out[1][col] * out[1][prev] + out[2][col] * out[2][prev];
				for (int i = 0; i < 3; ++i) {
					out[i][col] -= d * out[i][prev];
				}
			}
			const double l = std::sqrt(out[0][col] * out[0][col] + out[1][col] * out[1][col] + out[2][col] * out[2][col]);
			for (int i = 0; i < 3; ++i) {
				a_m[i][col] = l > 1e-12 ? out[i][col] / l : (i == col ? 1.0 : 0.0);
			}
		}
	}

	double Vec2::Length() const { return std::sqrt(x * x + y * y); }

	Vec2 Vec2::Normalized() const
	{
		const double l = Length();
		return l > 1e-12 ? Vec2{ x / l, y / l } : Vec2{ 0.0, 0.0 };
	}

	Sim::Sim(const Spec& a_spec) :
		_spec(a_spec)
	{
		Rack();
		Hold(0.0);
	}

	Vec2 Sim::Spot(int a_pin) const
	{
		// pins 1..10: row 0 the head pin; row 1 the 2 (left) and 3; row 2 the 4, 5, 6; row 3 the 7, 8, 9, 10
		static constexpr std::array<std::array<int, 2>, kPins> kRowAndPlace{ { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 0 }, { 2, 1 }, { 2, 2 },
			{ 3, 0 }, { 3, 1 }, { 3, 2 }, { 3, 3 } } };
		const auto   rp = kRowAndPlace[static_cast<std::size_t>(std::clamp(a_pin, 0, kPins - 1))];
		const double s = _spec.pinSpacing;
		return { _spec.headPin + rp[0] * s * kSqrt3 * 0.5, (rp[0] * 0.5 - rp[1]) * s };  // +y is the bowler's left
	}

	void Sim::Rack()
	{
		for (int i = 0; i < kPins; ++i) {
			Pin p;
			p.pos = Spot(i);
			p.yaw = 0.7 * i;  // not all facing the same way
			_pins[static_cast<std::size_t>(i)] = p;
		}
		_events.clear();
	}

	void Sim::Sweep()
	{
		for (auto& p : _pins) {
			if (p.state == PinState::kStanding && p.tilt < TippingAngle()) {
				p.tilt = 0.0;
				p.tiltRate = 0.0;
				p.vel = {};
				p.yawRate = 0.0;
				p.z = 0.0;
			} else {
				p.state = PinState::kSwept;
			}
		}
		_events.clear();
	}

	void Sim::Hold(double a_y)
	{
		Ball b;
		b.state = BallState::kHeld;
		b.pos = { 0.0, std::clamp(a_y, -_spec.laneHalfWidth + _spec.ballRadius, _spec.laneHalfWidth - _spec.ballRadius) };
		b.z = _spec.ballRadius;
		std::copy(&_ball.orient[0][0], &_ball.orient[0][0] + 9, &b.orient[0][0]);  // it keeps whichever way it's turned
		_ball = b;
		_thrown = false;
	}

	void Sim::Throw(const Release& a_release)
	{
		Hold(a_release.y);
		_ball.state = BallState::kLane;
		const Vec2 dir{ std::cos(a_release.angle), std::sin(a_release.angle) };
		_ball.vel = dir * std::max(1.0, a_release.speed);
		// rolling forward along dir is spin about the axis (-dir.y, dir.x); turning that axis by a_release.axis
		// towards the throw puts side roll on it. Positive turns it to hook left.
		const double c = std::cos(a_release.axis), s = std::sin(a_release.axis);
		const Vec2   rollAxis{ -dir.y, dir.x };
		const Vec2   axis = rollAxis * c - dir * s;
		_ball.spin = { axis.x * a_release.revs, axis.y * a_release.revs, 0.0 };
		_standingAtThrow = StandingMask();
		_sinceThrow = 0.0;
		_still = 0.0;
		_carry = 0.0;
		_thrown = true;
	}

	double Sim::TippingAngle() const
	{
		return std::atan2(_spec.pinBaseRadius, _spec.pinCentre);
	}

	double Sim::PinInertia() const
	{
		// a slim stick pivoting on its base rim: centre of mass term plus its own length
		const double h = _spec.pinCentre;
		const double r = _spec.pinBaseRadius;
		return _spec.pinMass * (h * h + r * r + _spec.pinHeight * _spec.pinHeight / 14.0);
	}

	Vec2 Sim::BellyOf(const Pin& a_pin) const
	{
		return a_pin.pos + a_pin.tiltDir * (kBellyHeight * std::sin(a_pin.tilt));
	}

	Vec3 Sim::PinAxis(int a_pin) const
	{
		const auto& p = _pins[static_cast<std::size_t>(a_pin)];
		const double s = std::sin(p.tilt);
		return { p.tiltDir.x * s, p.tiltDir.y * s, std::cos(p.tilt) };
	}

	void Sim::Advance(double a_seconds)
	{
		_carry += std::clamp(a_seconds, 0.0, 0.25);
		while (_carry >= kStep) {
			Step();
			_carry -= kStep;
		}
	}

	void Sim::Step()
	{
		if (!_thrown) {
			return;
		}
		_sinceThrow += kStep;
		StepBall(kStep);
		BallHitsPins();
		PinsHitPins();
		StepPins(kStep);

		// still: the ball's gone and no pin is moving
		bool moving = _ball.state == BallState::kLane || _ball.state == BallState::kGutter;
		for (const auto& p : _pins) {
			if (p.state == PinState::kFalling) {
				moving = true;
			} else if (p.state == PinState::kStanding || p.state == PinState::kDown) {
				if (p.vel.Length() > 1.5 || std::abs(p.tiltRate) > 0.05 || (p.state == PinState::kStanding && p.tilt > 0.01)) {
					moving = true;
				}
			}
		}
		_still = moving ? 0.0 : _still + kStep;
	}

	bool Sim::Settled() const
	{
		return _thrown && (_still >= kSettleSeconds || _sinceThrow >= kGiveUpSeconds);
	}

	void Sim::StepBall(double a_dt)
	{
		auto&        b = _ball;
		const double R = _spec.ballRadius;
		const double g = _spec.gravity;
		if (b.state == BallState::kLane) {
			// the slip where the ball touches the lane: its travel less what its spin carries the contact point
			const Vec2   slip{ b.vel.x - R * b.spin.y, b.vel.y + R * b.spin.x };
			const double slipLen = slip.Length();
			const double mu = b.pos.x < _spec.oilLength ? _spec.oilFriction :
			                  b.pos.x < _spec.oilLength + 40.0 ? _spec.oilFriction + (_spec.dryFriction - _spec.oilFriction) * (b.pos.x - _spec.oilLength) / 40.0 :
			                                                     _spec.dryFriction;
			const double slipDrop = 3.5 * mu * g * a_dt;  // (for a solid ball the slip closes 7/2 as fast as friction slows it)
			if (slipLen > 1e-6 && slipLen > slipDrop) {
				const Vec2 a = slip * (-mu * g / slipLen);
				b.vel += a * a_dt;
				b.spin.x += 2.5 / R * a.y * a_dt;
				b.spin.y -= 2.5 / R * a.x * a_dt;
			} else {
				// rolling: the spin matches the travel, and it slowly loses speed
				b.vel -= slip * (2.0 / 7.0);
				const double speed = b.vel.Length();
				const double slower = std::max(0.0, speed - _spec.rollResistance * g * a_dt);
				b.vel = speed > 1e-9 ? b.vel * (slower / speed) : Vec2{};
				b.spin.x = -b.vel.y / R;
				b.spin.y = b.vel.x / R;
			}
			b.spin.z *= std::exp(-0.5 * a_dt);
			b.pos += b.vel * a_dt;
			b.z = R;

			if (std::abs(b.pos.y) > _spec.laneHalfWidth) {
				// over the edge: into the gutter, and straight on down it
				b.state = BallState::kGutter;
				b.pos.y = (b.pos.y > 0.0 ? 1.0 : -1.0) * (_spec.laneHalfWidth + _spec.gutterWidth * 0.5);
				AddEvent(Event::Kind::kBallGutter, b.pos, b.vel.Length());
			}
		} else if (b.state == BallState::kGutter) {
			const double speed = std::max(std::abs(b.vel.x), 60.0);
			b.vel = { std::max(0.0, speed - _spec.rollResistance * g * a_dt), 0.0 };
			b.spin = { 0.0, b.vel.x / R, 0.0 };
			b.pos += b.vel * a_dt;
			b.z = std::max(R - _spec.gutterDepth, b.z - 60.0 * a_dt);
		}
		if ((b.state == BallState::kLane || b.state == BallState::kGutter) && b.pos.x > _spec.deckEnd + R) {
			b.state = BallState::kPit;
			AddEvent(Event::Kind::kBallPit, b.pos, b.vel.Length());
		}
		if (b.state == BallState::kLane && b.vel.Length() < 3.0 && _sinceThrow > 2.0) {
			b.state = BallState::kPit;  // (died on the lane: the pinsetter takes it)
		}
		RotateBy(b.orient, b.spin, a_dt);
	}

	void Sim::PushPin(Pin& a_pin, const Vec2& a_dir, double a_impulse, double a_height)
	{
		if (a_impulse <= 0.0) {
			return;
		}
		a_pin.vel += a_dir * (a_impulse / _spec.pinMass * kSlideShare);
		const double spin = kTipShare * a_impulse * std::max(1.0, a_height) / PinInertia();
		if (a_pin.state == PinState::kStanding && a_pin.tilt < 0.08) {
			// nearly upright: the knock picks which way it leans
			const Vec2   v = a_pin.tiltDir * a_pin.tiltRate + a_dir * spin;
			const double l = v.Length();
			if (l > 1e-9) {
				a_pin.tiltDir = v * (1.0 / l);
				a_pin.tiltRate = l;
			}
		} else if (a_pin.state != PinState::kDown) {
			const double along = a_dir.Dot(a_pin.tiltDir);
			if (along >= 0.0) {
				a_pin.tiltRate += spin * along;
			} else {
				// knocked back against the way it's falling: it loses its fall, it doesn't fly back up
				a_pin.tiltRate = std::max(a_pin.tiltRate * 0.3, a_pin.tiltRate + spin * along);
			}
			a_pin.vel += (a_dir - a_pin.tiltDir * along) * (spin * a_height * 0.2);
		}
		a_pin.yawRate += (a_dir.x * a_pin.tiltDir.y - a_dir.y * a_pin.tiltDir.x) * spin * 2.0 + spin * 0.3;
	}

	void Sim::BallHitsPins()
	{
		auto& b = _ball;
		if (b.state != BallState::kLane) {
			return;  // (a gutter ball never reaches a pin)
		}
		const double R = _spec.ballRadius;
		for (int i = 0; i < kPins; ++i) {
			auto& p = _pins[static_cast<std::size_t>(i)];
			if (!PinActive(p)) {
				continue;
			}
			Vec2   contact;   // the nearest point of the pin to the ball, seen from above
			double reach;     // ball radius + the pin's there
			double height;    // how far up the pin it is
			if (p.state != PinState::kDown && p.tilt < 1.0) {
				contact = BellyOf(p);
				reach = R + _spec.pinRadius;
				height = kBellyHeight;
			} else {
				const Vec2   top = p.pos + p.tiltDir * (_spec.pinHeight * std::sin(p.tilt));
				const Vec2   seg = top - p.pos;
				const double len2 = seg.Dot(seg);
				const double t = len2 > 1e-9 ? Clamp01((b.pos - p.pos).Dot(seg) / len2) : 0.0;
				contact = p.pos + seg * t;
				reach = R + _spec.pinBodyRadius;
				height = t * _spec.pinHeight;
			}
			const Vec2   d = contact - b.pos;
			const double dist = d.Length();
			if (dist >= reach || dist < 1e-9) {
				continue;
			}
			const Vec2   n = d * (1.0 / dist);
			const Vec2   pinVel = p.vel + p.tiltDir * (p.tiltRate * height * std::cos(p.tilt));
			const double closing = (b.vel - pinVel).Dot(n);
			// out of the overlap (the pin gives way, the ball barely does)
			const double overlap = reach - dist;
			p.pos += n * (overlap * 0.85);
			b.pos -= n * (overlap * 0.15);
			if (closing <= 0.0) {
				continue;
			}
			const double J = (1.0 + _spec.pinRestitution) * closing / (1.0 / _spec.ballMass + 1.0 / _spec.pinMass);
			b.vel -= n * (J / _spec.ballMass);
			PushPin(p, n, J, height);
			AddEvent(Event::Kind::kBallPin, contact, closing, i);
		}
	}

	void Sim::PinsHitPins()
	{
		const double L = _spec.pinHeight * 0.95;
		const double r = _spec.pinBodyRadius;
		auto segment = [&](const Pin& a_p, Vec3& a_base, Vec3& a_top) {
			const double s = std::sin(a_p.tilt);
			const double lift = a_p.state == PinState::kDown ? _spec.pinRadius * 0.8 : 0.0;
			a_base = { a_p.pos.x, a_p.pos.y, a_p.z + lift };
			a_top = { a_p.pos.x + a_p.tiltDir.x * s * L, a_p.pos.y + a_p.tiltDir.y * s * L, a_p.z + lift + std::cos(a_p.tilt) * L };
		};
		auto still = [&](const Pin& a_p) {
			return a_p.state == PinState::kStanding && a_p.tilt < 0.02 && std::abs(a_p.tiltRate) < 0.05 && a_p.vel.Length() < 1.0;
		};
		for (int i = 0; i < kPins; ++i) {
			auto& a = _pins[static_cast<std::size_t>(i)];
			if (!PinActive(a)) {
				continue;
			}
			for (int j = i + 1; j < kPins; ++j) {
				auto& c = _pins[static_cast<std::size_t>(j)];
				if (!PinActive(c) || (still(a) && still(c))) {
					continue;
				}
				// quick reject: too far apart for any part of them to touch
				if ((a.pos - c.pos).Length() > 2.0 * (L + r)) {
					continue;
				}
				Vec3 a0, a1, c0, c1;
				segment(a, a0, a1);
				segment(c, c0, c1);
				double s = 0.0, t = 0.0;
				ClosestOnSegments(a0, a1, c0, c1, s, t);
				const Vec3   pa = Lerp(a0, a1, s);
				const Vec3   pc = Lerp(c0, c1, t);
				const Vec3   d{ pc.x - pa.x, pc.y - pa.y, pc.z - pa.z };
				const double dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
				if (dist >= 2.0 * r) {
					continue;
				}
				Vec2 n{ d.x, d.y };
				if (n.Length() < 1e-6) {
					n = c.pos - a.pos;
				}
				n = n.Normalized();
				if (n.Length() < 0.5) {
					n = { 1.0, 0.0 };
				}
				const double ha = s * L;
				const double hc = t * L;
				const Vec2   va = a.vel + a.tiltDir * (a.tiltRate * ha * std::cos(a.tilt));
				const Vec2   vc = c.vel + c.tiltDir * (c.tiltRate * hc * std::cos(c.tilt));
				const double closing = (va - vc).Dot(n);
				// apart: each moves half the overlap (standing pins hardly at all: their base grips)
				const double overlap = 2.0 * r - dist;
				const double wa = still(a) ? 0.1 : 1.0;
				const double wc = still(c) ? 0.1 : 1.0;
				a.pos -= n * (overlap * wa / (wa + wc));
				c.pos += n * (overlap * wc / (wa + wc));
				if (closing <= 0.0) {
					continue;
				}
				const double J = (1.0 + _spec.pinPinRestitution) * closing / (2.0 / _spec.pinMass);
				PushPin(c, n, J, hc);
				PushPin(a, n * -1.0, J, ha);
				if (closing > 15.0) {
					AddEvent(Event::Kind::kPinPin, { (pa.x + pc.x) * 0.5, (pa.y + pc.y) * 0.5 }, closing, j);
				}
			}
		}
	}

	void Sim::StepPins(double a_dt)
	{
		const double g = _spec.gravity;
		const double I = PinInertia();
		const double tip = TippingAngle();
		for (int i = 0; i < kPins; ++i) {
			auto& p = _pins[static_cast<std::size_t>(i)];
			if (p.state == PinState::kGone) {
				// dropping out of sight into the pit or a gutter
				p.drop += g * a_dt;
				p.z = std::max(-60.0, p.z - p.drop * a_dt);
				p.pos += p.vel * a_dt;
				p.vel = p.vel * std::exp(-3.0 * a_dt);
				continue;
			}
			if (!PinActive(p)) {
				continue;
			}

			// rocking and falling: gravity about the rim of the base
			if (p.state == PinState::kStanding || p.state == PinState::kFalling) {
				const double torque = _spec.pinMass * g * (_spec.pinCentre * std::sin(p.tilt) - _spec.pinBaseRadius * std::cos(p.tilt));
				p.tiltRate += torque / I * a_dt;
				if (p.state == PinState::kStanding) {
					p.tiltRate *= std::exp(-1.5 * a_dt);  // (a rocking pin loses its wobble)
				}
				p.tilt += p.tiltRate * a_dt;
				if (p.state == PinState::kFalling && p.tilt < 0.0) {
					// swung back past upright: it's falling the other way now
					p.tilt = -p.tilt;
					p.tiltDir = p.tiltDir * -1.0;
					p.tiltRate = -p.tiltRate;
				}
				if (p.state == PinState::kStanding) {
					if (p.tilt <= 0.0) {
						// back on its base: rocks over to the other side, with less, or settles
						p.tilt = -p.tilt;
						p.tiltDir = p.tiltDir * -1.0;
						p.tiltRate = -p.tiltRate * 0.35;
						if (std::abs(p.tiltRate) < 0.25) {
							p.tilt = 0.0;
							p.tiltRate = 0.0;
						}
					} else if (p.tilt > tip && p.tiltRate > 0.0) {
						p.state = PinState::kFalling;  // past the point of no return
					}
				}
				if (p.state == PinState::kFalling && p.tilt >= kHalfPi) {
					// down: some of the fall goes into sliding along the deck
					p.vel += p.tiltDir * (p.tiltRate * _spec.pinCentre * 0.35);
					AddEvent(Event::Kind::kPinDeck, p.pos, p.tiltRate * _spec.pinCentre, i);
					p.tilt = kHalfPi;
					p.tiltRate = 0.0;
					p.state = PinState::kDown;
				}
			}

			// sliding on the deck
			const double speed = p.vel.Length();
			if (speed > 1e-6) {
				// (a fast pin is mostly in the air: the deck hardly slows it)
				const double grip = speed > kFlyingSpeed ? 0.15 : p.state == PinState::kDown ? 1.0 : 0.6;
				const double friction = _spec.pinSlideFriction * g * grip;
				const double slower = std::max(0.0, speed - friction * a_dt);
				const Vec2   dir = p.vel * (1.0 / speed);
				p.vel = p.vel * (slower / speed);
				// an upright pin pushed along trips over its own base: the deck holds its foot back, its top goes on
				if (p.state != PinState::kDown && slower > 0.0) {
					const double trip = friction * _spec.pinMass * _spec.pinCentre / I * a_dt;
					if (p.state == PinState::kStanding && p.tilt < 0.08) {
						const Vec2   v = p.tiltDir * p.tiltRate + dir * trip;
						const double l = v.Length();
						if (l > 1e-9) {
							p.tiltDir = v * (1.0 / l);
							p.tiltRate = l;
						}
					} else {
						p.tiltRate += trip * std::max(0.0, dir.Dot(p.tiltDir));
					}
				}
			}
			p.pos += p.vel * a_dt;
			p.yaw += p.yawRate * a_dt;
			p.yawRate *= std::exp(-2.0 * a_dt);

			// the kickbacks (both ends of a lying pin): it bounces back onto the deck
			if (p.pos.x > _spec.kickbackStart - 40.0) {
				const double outer = std::max(std::abs(p.pos.y), std::abs(p.pos.y + p.tiltDir.y * std::sin(p.tilt) * _spec.pinHeight));
				if (outer + _spec.pinBodyRadius > _spec.kickback && p.vel.y * p.pos.y > 0.0) {
					const double hit = std::abs(p.vel.y);
					p.vel.y = -p.vel.y * _spec.pinWallRestitution;
					p.yawRate += hit * 0.05;
					AddEvent(Event::Kind::kPinWall, p.pos, hit, i);
				}
			}

			// off the deck: the pit at the back, the gutters at the sides (a fast pin flies over a gutter)
			if (p.pos.x > _spec.deckEnd) {
				p.state = PinState::kGone;
				p.drop = 0.0;
				AddEvent(Event::Kind::kPinPit, p.pos, p.vel.Length(), i);
			} else if (std::abs(p.pos.y) > _spec.laneHalfWidth + 4.0 && p.vel.Length() < kFlyingSpeed) {
				p.state = PinState::kGone;
				p.drop = 0.0;
				p.vel.y *= 0.2;
			} else if (p.pos.x < -40.0) {
				p.state = PinState::kGone;  // (all the way back up the lane: as good as gone)
				p.drop = 0.0;
			}
		}
	}

	std::uint32_t Sim::StandingMask() const
	{
		std::uint32_t mask = 0;
		for (int i = 0; i < kPins; ++i) {
			const auto& p = _pins[static_cast<std::size_t>(i)];
			if (p.state == PinState::kStanding && p.tilt < TippingAngle()) {
				mask |= 1u << i;
			}
		}
		return mask;
	}

	int Sim::StandingCount() const
	{
		int n = 0;
		for (std::uint32_t m = StandingMask(); m; m &= m - 1) {
			++n;
		}
		return n;
	}

	int Sim::DownThisBall() const
	{
		int n = 0;
		for (std::uint32_t m = _standingAtThrow & ~StandingMask(); m; m &= m - 1) {
			++n;
		}
		return n;
	}

	std::vector<Event> Sim::TakeEvents()
	{
		std::vector<Event> out;
		out.swap(_events);
		return out;
	}

	void Sim::AddEvent(Event::Kind a_kind, const Vec2& a_pos, double a_strength, int a_pin)
	{
		if (_events.size() < 256) {
			_events.push_back({ a_kind, a_pos, a_strength, a_pin });
		}
	}
}
