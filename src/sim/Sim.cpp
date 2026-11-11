#include "sim/Sim.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace bowl
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
		constexpr double kSqrt3 = 1.7320508075688772;
		constexpr double kSettleSeconds = 0.4;   // every pin asleep this long: settled
		constexpr double kGiveUpSeconds = 12.0;  // a throw never takes longer than this
		constexpr int    kIterations = 10;       // impulse solver passes per step
		constexpr double kSlop = 0.05;           // overlap allowed before it's pushed apart
		constexpr double kPush = 0.25;           // how much of the overlap is pushed out per step
		constexpr double kMaxPush = 60.0;        // ...but never faster than this (no launching pins out of a deep overlap)
		constexpr double kBounceFrom = 25.0;     // impacts slower than this don't bounce (resting contact)
		constexpr double kSleepSpeed = 4.0;      // a pin this slow...
		constexpr double kSleepSpin = 0.6;       // ...turning this slowly...
		constexpr double kSleepAfter = 0.3;      // ...for this long goes to sleep
		constexpr double kUprightCos = 0.866;    // within 30 degrees of upright counts as standing
	}

	// ---------------------------------------------------------------- vectors

	double Vec2::Length() const { return std::sqrt(x * x + y * y); }

	Vec2 Vec2::Normalized() const
	{
		const double l = Length();
		return l > 1e-12 ? Vec2{ x / l, y / l } : Vec2{ 0.0, 0.0 };
	}

	double Vec3::Length() const { return std::sqrt(x * x + y * y + z * z); }

	Vec3 Vec3::Normalized() const
	{
		const double l = Length();
		return l > 1e-12 ? Vec3{ x / l, y / l, z / l } : Vec3{};
	}

	void Quat::ToMatrix(double a_m[3][3]) const
	{
		const double xx = x * x, yy = y * y, zz = z * z;
		const double xy = x * y, xz = x * z, yz = y * z;
		const double wx = w * x, wy = w * y, wz = w * z;
		a_m[0][0] = 1.0 - 2.0 * (yy + zz);
		a_m[0][1] = 2.0 * (xy - wz);
		a_m[0][2] = 2.0 * (xz + wy);
		a_m[1][0] = 2.0 * (xy + wz);
		a_m[1][1] = 1.0 - 2.0 * (xx + zz);
		a_m[1][2] = 2.0 * (yz - wx);
		a_m[2][0] = 2.0 * (xz - wy);
		a_m[2][1] = 2.0 * (yz + wx);
		a_m[2][2] = 1.0 - 2.0 * (xx + yy);
	}

	Vec3 Quat::Rotate(const Vec3& a_v) const
	{
		double m[3][3];
		ToMatrix(m);
		return { m[0][0] * a_v.x + m[0][1] * a_v.y + m[0][2] * a_v.z, m[1][0] * a_v.x + m[1][1] * a_v.y + m[1][2] * a_v.z,
			m[2][0] * a_v.x + m[2][1] * a_v.y + m[2][2] * a_v.z };
	}

	Quat Quat::AxisAngle(const Vec3& a_axis, double a_angle)
	{
		const Vec3   k = a_axis.Normalized();
		const double s = std::sin(a_angle * 0.5);
		return { std::cos(a_angle * 0.5), k.x * s, k.y * s, k.z * s };
	}

	Quat Quat::operator*(const Quat& a_o) const
	{
		return { w * a_o.w - x * a_o.x - y * a_o.y - z * a_o.z, w * a_o.x + x * a_o.w + y * a_o.z - z * a_o.y,
			w * a_o.y - x * a_o.z + y * a_o.w + z * a_o.x, w * a_o.z + x * a_o.y - y * a_o.x + z * a_o.w };
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

	// ---------------------------------------------------------------- the rack

	Sim::Sim(const Spec& a_spec) :
		_spec(a_spec)
	{
		BuildShape();
		Rack();
		Hold(0.0);
	}

	void Sim::BuildShape()
	{
		// BowlingPin01's profile as spheres up its axis (z from its foot, radius), and a ring of small ones round the
		// foot so it stands flat and rocks on the rim. Its widest (the belly, where the ball hits) is about 10 up.
		static constexpr std::array<std::array<double, 2>, 11> kBody{ { { 3.6, 3.4 }, { 6.6, 4.6 }, { 9.6, 5.3 }, { 12.6, 5.2 },
			{ 15.6, 4.6 }, { 18.6, 3.7 }, { 21.6, 2.8 }, { 24.5, 2.2 }, { 27.4, 2.4 }, { 30.2, 2.8 }, { 32.8, 2.4 } } };
		const double c = _spec.pinCentre;
		_shape.clear();
		for (int i = 0; i < 6; ++i) {
			const double a = i * kPi / 3.0;
			_shape.push_back({ { 1.5 * std::cos(a), 1.5 * std::sin(a), 0.9 - c }, 0.9 });
		}
		const double scale = _spec.pinHeight / 35.2;
		for (const auto& s : kBody) {
			_shape.push_back({ { 0.0, 0.0, s[0] * scale - c }, s[1] * scale });
		}
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

	void Sim::StandPin(Pin& a_pin, const Vec2& a_at, double a_yaw) const
	{
		a_pin.state = PinState::kStanding;
		a_pin.com = { a_at.x, a_at.y, _spec.pinCentre };
		a_pin.rot = Quat::AxisAngle({ 0.0, 0.0, 1.0 }, a_yaw);
		a_pin.vel = {};
		a_pin.angVel = {};
		a_pin.asleep = true;
		a_pin.quiet = 0.0;
		a_pin.frozen = false;
	}

	void Sim::Rack(double a_jitter, std::uint32_t a_seed)
	{
		std::uint32_t state = a_seed * 747796405u + 2891336453u;
		auto          next = [&]() {  // (a small PCG-style hash: the same seed always racks the same way)
			state = state * 747796405u + 2891336453u;
			const std::uint32_t w = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
			return static_cast<double>((w >> 22u) ^ w) / 4294967296.0;
		};
		for (int i = 0; i < kPins; ++i) {
			Vec2 at = Spot(i);
			if (a_jitter > 0.0) {
				const double angle = next() * 2.0 * kPi;
				const double r = a_jitter * std::sqrt(next());
				at += Vec2{ std::cos(angle), std::sin(angle) } * r;
			}
			const double yaw = 0.7 * i + (a_jitter > 0.0 ? next() * 2.0 * kPi : 0.0);  // not all facing the same way
			StandPin(_pins[static_cast<std::size_t>(i)], at, yaw);
		}
		_events.clear();
	}

	void Sim::Sweep()
	{
		for (auto& p : _pins) {
			if (p.state == PinState::kStanding && PinUpright(p)) {
				// stood straight back up where it is, facing the way it was
				const Vec3 fwd = p.rot.Rotate({ 1.0, 0.0, 0.0 });
				StandPin(p, { p.com.x, p.com.y }, std::atan2(fwd.y, fwd.x));
			} else {
				p.state = PinState::kSwept;
				p.frozen = true;
				p.asleep = true;
			}
		}
		_events.clear();
	}

	void Sim::ForceSettle()
	{
		for (auto& p : _pins) {
			if (p.frozen || p.state == PinState::kSwept) {
				continue;
			}
			if (p.state == PinState::kStanding && PinUpright(p)) {
				const Vec3 fwd = p.rot.Rotate({ 1.0, 0.0, 0.0 });
				StandPin(p, { p.com.x, p.com.y }, std::atan2(fwd.y, fwd.x));
			} else {
				if (p.state == PinState::kStanding) {
					p.state = PinState::kDown;
				}
				p.vel = {};
				p.angVel = {};
				p.asleep = true;
			}
		}
		_still = kSettleSeconds;
	}

	void Sim::KeepOnly(std::uint32_t a_mask)
	{
		for (int i = 0; i < kPins; ++i) {
			if (!(a_mask & (1u << i))) {
				auto& p = _pins[static_cast<std::size_t>(i)];
				p.state = PinState::kSwept;
				p.frozen = true;
				p.asleep = true;
			}
		}
	}

	void Sim::ClearPins()
	{
		for (auto& p : _pins) {
			p.state = PinState::kSwept;
			p.frozen = true;
			p.asleep = true;
		}
	}

	// ---------------------------------------------------------------- the ball

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
		_ball.drift = a_release.drift;
		_ball.driftWave = a_release.driftWave;
		_ball.driftPhase = a_release.driftPhase;
		_standingAtThrow = StandingMask();
		_sinceThrow = 0.0;
		_still = 0.0;
		_carry = 0.0;
		_thrown = true;
		_lastWallHit.fill(-1.0);
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
			// the lane's tilt and uneven oil nudge it across, a little
			b.vel.y += (b.drift + b.driftWave * std::sin(b.pos.x * (2.0 * kPi / 450.0) + b.driftPhase)) * a_dt;
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
			b.pitTime = 0.0;
			b.hitBack = false;
		}
		if (b.state == BallState::kPit) {
			// in the slot in front of the back wall: it hits the wall, comes off it to one side and runs along the
			// slot until it's taken into the machine at the side wall
			const double back = _spec.deckEnd + _spec.pitBack;
			b.pitTime += a_dt;
			b.pos += b.vel * a_dt;
			b.z += (R - b.z) * std::min(1.0, 8.0 * a_dt);  // (up out of a gutter onto the slot's floor)
			if (b.pos.x + R > back) {
				const double into = b.vel.x;
				b.pos.x = back - R;
				b.vel.x = -0.25 * std::abs(into);
				if (!b.hitBack) {
					b.hitBack = true;
					// which way it goes: the way it was already heading across, or (head on) towards the nearer side
					const double side = std::abs(b.vel.y) > 5.0 ? (b.vel.y > 0.0 ? 1.0 : -1.0) : (b.pos.y >= 0.0 ? 1.0 : -1.0);
					b.vel.y = side * std::max(std::abs(b.vel.y), 0.45 * std::abs(into));
					AddEvent(Event::Kind::kBallPit, b.pos, std::abs(into));
				}
			}
			if (b.pos.x < _spec.deckEnd + R) {
				b.pos.x = _spec.deckEnd + R;  // (the deck's edge keeps it in the slot)
				b.vel.x = std::max(0.0, b.vel.x);
			}
			// off the side walls too, rolling along the slot until it stops there
			if (std::abs(b.pos.y) + R > _spec.kickback) {
				const double sy = b.pos.y > 0.0 ? 1.0 : -1.0;
				b.pos.y = sy * (_spec.kickback - R);
				if (b.vel.y * sy > 0.0) {
					if (std::abs(b.vel.y) > 40.0) {
						AddEvent(Event::Kind::kBallPit, b.pos, std::abs(b.vel.y));
					}
					b.vel.y = -0.35 * b.vel.y;
				}
			}
			b.vel.x *= std::max(0.0, 1.0 - 3.0 * a_dt);
			b.vel.y *= std::max(0.0, 1.0 - 0.9 * a_dt);
			b.spin = { -b.vel.y / R, b.vel.x / R, 0.0 };
			if ((b.hitBack && b.vel.Length() < 8.0) || b.pitTime > 6.0) {
				b.state = BallState::kTaken;  // (at rest in the slot: the machine has it)
				b.vel = {};
				b.spin = {};
			}
		}
		if (b.state == BallState::kLane && b.vel.Length() < 3.0 && _sinceThrow > 2.0) {
			b.state = BallState::kTaken;  // (died on the lane: the pinsetter takes it)
		}
		RotateBy(b.orient, b.spin, a_dt);
	}

	// ---------------------------------------------------------------- stepping

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
		StepPins(kStep);

		bool moving = _ball.state == BallState::kLane || _ball.state == BallState::kGutter || _ball.state == BallState::kPit;
		for (const auto& p : _pins) {
			// (pins already in a gutter or the pit can't change anything: only the ones on the deck count)
			if (!p.frozen && !p.asleep && p.state != PinState::kGone) {
				moving = true;
			}
		}
		_still = moving ? 0.0 : _still + kStep;
	}

	bool Sim::Settled() const
	{
		return _thrown && (_still >= kSettleSeconds || _sinceThrow >= kGiveUpSeconds);
	}

	double Sim::FloorAt(double a_x, double a_y) const
	{
		if (a_x > _spec.deckEnd) {
			return -_spec.pitDepth;
		}
		return std::abs(a_y) <= _spec.laneHalfWidth ? 0.0 : -_spec.gutterDepth;
	}

	// ---------------------------------------------------------------- rigid bodies

	Vec3 Sim::InvInertiaTimes(int a_pin, const Vec3& a_v) const
	{
		const auto&  p = _pins[static_cast<std::size_t>(a_pin)];
		const double It = _spec.pinMass * _spec.pinGyration * _spec.pinGyration;
		const double Ia = _spec.pinMass * _spec.pinAxialGyration * _spec.pinAxialGyration;
		double       m[3][3];
		p.rot.ToMatrix(m);
		// into the pin's own frame, scaled, and back
		const Vec3 local{ m[0][0] * a_v.x + m[1][0] * a_v.y + m[2][0] * a_v.z, m[0][1] * a_v.x + m[1][1] * a_v.y + m[2][1] * a_v.z,
			m[0][2] * a_v.x + m[1][2] * a_v.y + m[2][2] * a_v.z };
		const Vec3 scaled{ local.x / It, local.y / It, local.z / Ia };
		return { m[0][0] * scaled.x + m[0][1] * scaled.y + m[0][2] * scaled.z, m[1][0] * scaled.x + m[1][1] * scaled.y + m[1][2] * scaled.z,
			m[2][0] * scaled.x + m[2][1] * scaled.y + m[2][2] * scaled.z };
	}

	Vec3 Sim::VelocityAt(int a_body, const Vec3& a_r) const
	{
		if (a_body == kWorld) {
			return {};
		}
		if (a_body == kBallBody) {
			return { _ball.vel.x, _ball.vel.y, 0.0 };
		}
		const auto& p = _pins[static_cast<std::size_t>(a_body)];
		return p.vel + p.angVel.Cross(a_r);
	}

	double Sim::MassAlong(int a_body, const Vec3& a_r, const Vec3& a_dir) const
	{
		// 1 / (the effective mass this body has, pushed at a_r along a_dir)
		if (a_body == kWorld) {
			return 0.0;
		}
		if (a_body == kBallBody) {
			return 1.0 / _spec.ballMass;
		}
		const Vec3 t = InvInertiaTimes(a_body, a_r.Cross(a_dir)).Cross(a_r);
		return 1.0 / _spec.pinMass + a_dir.Dot(t);
	}

	void Sim::ApplyImpulse(int a_body, const Vec3& a_r, const Vec3& a_j)
	{
		if (a_body == kWorld) {
			return;
		}
		if (a_body == kBallBody) {
			// (the ball stays on the lane: only its travel across it changes)
			_ball.vel += Vec2{ a_j.x, a_j.y } * (1.0 / _spec.ballMass);
			return;
		}
		auto& p = _pins[static_cast<std::size_t>(a_body)];
		p.vel += a_j * (1.0 / _spec.pinMass);
		p.angVel += InvInertiaTimes(a_body, a_r.Cross(a_j));
	}

	void Sim::Collide(std::vector<Contact>& a_out)
	{
		a_out.clear();
		const auto& s = _spec;
		std::array<std::vector<Vec3>, kPins> centres;
		for (int i = 0; i < kPins; ++i) {
			const auto& p = _pins[static_cast<std::size_t>(i)];
			if (p.frozen) {
				continue;
			}
			auto& c = centres[static_cast<std::size_t>(i)];
			c.reserve(_shape.size());
			for (const auto& sp : _shape) {
				c.push_back(p.com + p.rot.Rotate(sp.at));
			}
		}
		auto add = [&](int a_a, int a_b, const Vec3& a_point, const Vec3& a_n, double a_depth, double a_e, double a_mu) {
			Contact k{};
			k.a = a_a;
			k.b = a_b;
			k.point = a_point;
			k.n = a_n;
			k.depth = a_depth;
			k.restitution = a_e;
			k.friction = a_mu;
			a_out.push_back(k);
		};

		// the lane and deck (a block: its top, its edges down into the gutters, its end over the pit), the gutters'
		// floor, the pit's floor, the kickbacks and the pit's back cushion
		const double L = s.laneHalfWidth;
		for (int i = 0; i < kPins; ++i) {
			const auto& p = _pins[static_cast<std::size_t>(i)];
			if (p.frozen || p.asleep) {
				continue;
			}
			const auto& c = centres[static_cast<std::size_t>(i)];
			for (std::size_t k = 0; k < _shape.size(); ++k) {
				const Vec3&  at = c[k];
				const double r = _shape[k].r;
				// the lane block: x up to the deck's end, |y| up to the lane's edge, z up to the surface
				const Vec3 q{ std::min(at.x, s.deckEnd), std::clamp(at.y, -L, L), std::min(at.z, 0.0) };
				const Vec3 d = at - q;
				const double dl = d.Length();
				if (dl > 1e-9) {
					if (dl < r) {
						add(kWorld, i, q, d * (1.0 / dl), r - dl, s.pinDeckRestitution, s.pinDeckFriction);
					}
				} else {
					// inside it (pushed in past an edge): out through the nearest face
					const double top = -at.z;
					const double side = L - std::abs(at.y);
					const double end = s.deckEnd - at.x;
					if (top <= side && top <= end) {
						add(kWorld, i, { at.x, at.y, 0.0 }, { 0.0, 0.0, 1.0 }, top + r, s.pinDeckRestitution, s.pinDeckFriction);
					} else if (side <= end) {
						const double sy = at.y >= 0.0 ? 1.0 : -1.0;
						add(kWorld, i, { at.x, sy * L, at.z }, { 0.0, sy, 0.0 }, side + r, s.pinDeckRestitution, s.pinDeckFriction);
					} else {
						add(kWorld, i, { s.deckEnd, at.y, at.z }, { 1.0, 0.0, 0.0 }, end + r, s.pinDeckRestitution, s.pinDeckFriction);
					}
				}
				// the gutters' floor beside it, and the pit's below the end
				if (at.x <= s.deckEnd && std::abs(at.y) > L && at.z - r < -s.gutterDepth) {
					add(kWorld, i, { at.x, at.y, -s.gutterDepth }, { 0.0, 0.0, 1.0 }, -s.gutterDepth - (at.z - r), s.pinDeckRestitution, s.pinDeckFriction);
				}
				if (at.x > s.deckEnd && at.z - r < -s.pitDepth) {
					add(kWorld, i, { at.x, at.y, -s.pitDepth }, { 0.0, 0.0, 1.0 }, -s.pitDepth - (at.z - r), s.pinDeckRestitution, s.pinDeckFriction);
				}
				// the walls: the kickbacks round the deck, the gutters' far sides down the lane, the pit's back
				const double wall = at.x > s.kickbackStart - 40.0 ? s.kickback : L + s.gutterWidth;
				if (at.x < s.deckEnd + s.pitBack) {
					if (at.y + r > wall) {
						add(kWorld, i, { at.x, wall, at.z }, { 0.0, -1.0, 0.0 }, at.y + r - wall, s.pinWallRestitution, s.pinFriction);
					} else if (at.y - r < -wall) {
						add(kWorld, i, { at.x, -wall, at.z }, { 0.0, 1.0, 0.0 }, -wall - (at.y - r), s.pinWallRestitution, s.pinFriction);
					}
				}
				if (at.x + r > s.deckEnd + s.pitBack) {
					add(kWorld, i, { s.deckEnd + s.pitBack, at.y, at.z }, { -1.0, 0.0, 0.0 }, at.x + r - (s.deckEnd + s.pitBack), s.pitBackRestitution, s.pinFriction);
				}
			}
		}

		// pin on pin
		const double reach = s.pinHeight + 2.0 * s.pinRadius;
		for (int i = 0; i < kPins; ++i) {
			auto& a = _pins[static_cast<std::size_t>(i)];
			if (a.frozen) {
				continue;
			}
			for (int j = i + 1; j < kPins; ++j) {
				auto& b = _pins[static_cast<std::size_t>(j)];
				if (b.frozen || (a.asleep && b.asleep) || (a.com - b.com).Length() > reach) {
					continue;
				}
				const auto&       ca = centres[static_cast<std::size_t>(i)];
				const auto&       cb = centres[static_cast<std::size_t>(j)];
				const std::size_t before = a_out.size();
				for (std::size_t m = 0; m < _shape.size(); ++m) {
					for (std::size_t q = 0; q < _shape.size(); ++q) {
						const Vec3   d = cb[q] - ca[m];
						const double rr = _shape[m].r + _shape[q].r;
						const double dist2 = d.Dot(d);
						if (dist2 >= rr * rr) {
							continue;
						}
						const double dist = std::sqrt(dist2);
						const Vec3   n = dist > 1e-9 ? d * (1.0 / dist) : Vec3{ 1.0, 0.0, 0.0 };
						add(i, j, ca[m] + n * (_shape[m].r - (rr - dist) * 0.5), n, rr - dist, s.pinPinRestitution, s.pinFriction);
					}
				}
				if (a_out.size() > before) {
					// a moving pin wakes a sleeping one it touches
					a.asleep = false;
					b.asleep = false;
				}
			}
		}

		// the ball on the pins (a gutter ball never reaches one)
		if (_ball.state == BallState::kLane) {
			const Vec3   centre{ _ball.pos.x, _ball.pos.y, _ball.z };
			const double R = s.ballRadius;
			for (int i = 0; i < kPins; ++i) {
				auto& p = _pins[static_cast<std::size_t>(i)];
				if (p.frozen) {
					continue;
				}
				const Vec3 off = p.com - centre;
				if (off.Length() > R + s.pinHeight) {
					continue;
				}
				const auto&       c = centres[static_cast<std::size_t>(i)];
				const std::size_t before = a_out.size();
				for (std::size_t k = 0; k < _shape.size(); ++k) {
					const Vec3   d = c[k] - centre;
					const double rr = R + _shape[k].r;
					const double dist = d.Length();
					if (dist >= rr || dist < 1e-9) {
						continue;
					}
					const Vec3 n = d * (1.0 / dist);
					add(kBallBody, i, centre + n * R, n, rr - dist, s.pinRestitution, s.pinFriction);
				}
				if (a_out.size() > before) {
					p.asleep = false;
				}
			}
		}
	}

	void Sim::StepPins(double a_dt)
	{
		const auto& s = _spec;
		const double g = s.gravity;

		// gravity on everything awake
		for (auto& p : _pins) {
			if (!p.frozen && !p.asleep) {
				p.vel.z -= g * a_dt;
			}
		}

		Collide(_contacts);
		const Vec3 ballCentre{ _ball.pos.x, _ball.pos.y, _ball.z };
		auto       comOf = [&](int a_body) -> Vec3 {
            if (a_body == kBallBody) {
                return ballCentre;
            }
            return a_body >= 0 ? _pins[static_cast<std::size_t>(a_body)].com : Vec3{};
		};

		// each contact's lever arms, effective mass and the speed it should part at (a bounce, and pushing out any
		// overlap)
		for (auto& k : _contacts) {
			k.ra = k.a == kWorld ? Vec3{} : k.point - comOf(k.a);
			k.rb = k.point - comOf(k.b);
			const double inv = MassAlong(k.a, k.ra, k.n) + MassAlong(k.b, k.rb, k.n);
			k.massN = inv > 1e-12 ? 1.0 / inv : 0.0;
			const double vn = (VelocityAt(k.b, k.rb) - VelocityAt(k.a, k.ra)).Dot(k.n);
			const double bounce = vn < -kBounceFrom ? -k.restitution * vn : 0.0;
			const double push = std::min(kMaxPush, kPush * std::max(0.0, k.depth - kSlop) / a_dt);
			k.bias = std::max(bounce, push);
			k.jn = 0.0;
			k.jt = 0.0;

			// (sounds: real impacts only, not things resting on each other)
			if (vn < -40.0 && _events.size() < 256) {
				const Vec2 at{ k.point.x, k.point.y };
				if (k.a == kBallBody) {
					AddEvent(Event::Kind::kBallPin, at, -vn, k.b);
				} else if (k.a >= 0) {
					AddEvent(Event::Kind::kPinPin, at, -vn, k.b);
				} else if (k.n.z > 0.5) {
					AddEvent(Event::Kind::kPinDeck, at, -vn, k.b);
				} else if (_sinceThrow - _lastWallHit[static_cast<std::size_t>(k.b)] > 0.1) {
					_lastWallHit[static_cast<std::size_t>(k.b)] = _sinceThrow;
					AddEvent(Event::Kind::kPinWall, at, -vn, k.b);
				}
			}
		}
		// (one event of each kind per pin per step is plenty)
		std::sort(_events.begin(), _events.end(), [](const Event& a_l, const Event& a_r) {
			return a_l.pin != a_r.pin ? a_l.pin < a_r.pin : a_l.kind != a_r.kind ? a_l.kind < a_r.kind : a_l.strength > a_r.strength;
		});
		_events.erase(std::unique(_events.begin(), _events.end(), [](const Event& a_l, const Event& a_r) {
			return a_l.pin == a_r.pin && a_l.kind == a_r.kind && a_l.pin >= 0;
		}), _events.end());

		// sequential impulses: push the contacts apart (never pulling) and let friction act within its cone
		std::vector<Vec3> friction(_contacts.size());
		for (int it = 0; it < kIterations; ++it) {
			for (std::size_t c = 0; c < _contacts.size(); ++c) {
				auto&      k = _contacts[c];
				Vec3       rel = VelocityAt(k.b, k.rb) - VelocityAt(k.a, k.ra);
				const double vn = rel.Dot(k.n);
				double     dj = k.massN * (k.bias - vn);
				const double jn = std::max(0.0, k.jn + dj);
				dj = jn - k.jn;
				k.jn = jn;
				const Vec3 J = k.n * dj;
				ApplyImpulse(k.b, k.rb, J);
				ApplyImpulse(k.a, k.ra, -J);

				rel = VelocityAt(k.b, k.rb) - VelocityAt(k.a, k.ra);
				const Vec3   vt = rel - k.n * rel.Dot(k.n);
				const double vtl = vt.Length();
				if (vtl > 1e-6) {
					const Vec3   t = vt * (1.0 / vtl);
					const double inv = MassAlong(k.a, k.ra, t) + MassAlong(k.b, k.rb, t);
					if (inv > 1e-12) {
						Vec3         want = friction[c] - t * (vtl / inv);
						const double limit = k.friction * k.jn;
						const double wl = want.Length();
						if (wl > limit) {
							want = want * (limit / wl);
						}
						const Vec3 dt = want - friction[c];
						friction[c] = want;
						ApplyImpulse(k.b, k.rb, dt);
						ApplyImpulse(k.a, k.ra, -dt);
					}
				}
			}
		}

		// move, turn, roll to a stop, sleep, and see where each pin's got to
		std::array<bool, kPins> onFloor{};
		for (const auto& k : _contacts) {
			if (k.a == kWorld && k.n.z > 0.5) {
				onFloor[static_cast<std::size_t>(k.b)] = true;
			}
		}
		for (int i = 0; i < kPins; ++i) {
			auto& p = _pins[static_cast<std::size_t>(i)];
			if (p.frozen || p.asleep) {
				continue;
			}
			if (onFloor[static_cast<std::size_t>(i)]) {
				// a pin rolling on its side (or spinning on the deck) slowly loses it
				const double keep = std::exp(-s.pinRolling * a_dt);
				p.angVel = p.angVel * keep;
				p.vel.x *= keep;
				p.vel.y *= keep;
			}
			p.com += p.vel * a_dt;
			const Quat spin{ 0.0, p.angVel.x, p.angVel.y, p.angVel.z };
			const Quat dq = spin * p.rot;
			p.rot.w += 0.5 * a_dt * dq.w;
			p.rot.x += 0.5 * a_dt * dq.x;
			p.rot.y += 0.5 * a_dt * dq.y;
			p.rot.z += 0.5 * a_dt * dq.z;
			const double l = std::sqrt(p.rot.w * p.rot.w + p.rot.x * p.rot.x + p.rot.y * p.rot.y + p.rot.z * p.rot.z);
			p.rot.w /= l;
			p.rot.x /= l;
			p.rot.y /= l;
			p.rot.z /= l;

			// asleep once it's settled (a standing pin doesn't creep)
			// (resting on the deck, or on another pin)
			if (p.vel.Length() < kSleepSpeed && p.angVel.Length() < kSleepSpin) {
				p.quiet += a_dt;
				if (p.quiet >= kSleepAfter) {
					p.asleep = true;
					p.vel = {};
					p.angVel = {};
				}
			} else {
				p.quiet = 0.0;
			}

			// where it is now
			const PinState was = p.state;
			if (p.com.z < -2.0 || p.com.x > s.deckEnd) {
				p.state = PinState::kGone;  // in a gutter or over the back into the pit
				if (was != PinState::kGone && p.com.x > s.deckEnd) {
					AddEvent(Event::Kind::kPinPit, { p.com.x, p.com.y }, p.vel.Length(), i);
				}
				if (s.pitDepth > 20.0 && p.com.z < -s.pitDepth * 0.5) {
					p.frozen = true;  // (out of sight at the bottom of the pit)
					p.asleep = true;
				}
			} else {
				p.state = PinUpright(p) ? PinState::kStanding : PinState::kDown;
			}
		}
	}

	bool Sim::PinUpright(const Pin& a_pin) const
	{
		const Vec3 axis = a_pin.rot.Rotate({ 0.0, 0.0, 1.0 });
		return axis.z > kUprightCos && a_pin.com.z > _spec.pinCentre * 0.7;
	}

	void Sim::PinPose(int a_pin, Vec3& a_base, double a_rot[3][3]) const
	{
		const auto& p = _pins[static_cast<std::size_t>(a_pin)];
		p.rot.ToMatrix(a_rot);
		a_base = p.com + p.rot.Rotate({ 0.0, 0.0, -_spec.pinCentre });
	}

	Vec3 Sim::PinAxis(int a_pin) const
	{
		return _pins[static_cast<std::size_t>(a_pin)].rot.Rotate({ 0.0, 0.0, 1.0 });
	}

	// ---------------------------------------------------------------- counting

	std::uint32_t Sim::StandingMask() const
	{
		std::uint32_t mask = 0;
		for (int i = 0; i < kPins; ++i) {
			const auto& p = _pins[static_cast<std::size_t>(i)];
			if (p.state == PinState::kStanding && PinUpright(p)) {
				mask |= 1u << i;
			}
		}
		return mask;
	}

	int Sim::StandingCount() const
	{
		return static_cast<int>(std::popcount(StandingMask()));
	}

	int Sim::DownThisBall() const
	{
		return static_cast<int>(std::popcount(_standingAtThrow & ~StandingMask()));
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
