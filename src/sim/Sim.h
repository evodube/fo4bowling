#pragma once

// Bowling physics: one ball and ten pins on a lane. Plain C++ (no game headers), so it's tested on its own
// (tests/sim_test.cpp).
//
// The lane's own frame, in game units: x runs from the foul line (0) towards the pins, y is across the lane with +y on
// the bowler's left, z is up from the lane surface. The game maps this onto each lane in the cell (Lanes.h).
//
// The ball is a solid sphere with real friction: it skids through the oil, hooks once the slip between its spin and
// its travel grips the dry back end, and rolls once they match. The pins are rigid sticks that rock about the rim of
// their base: a light touch makes them wobble and settle, a hard one tips them past the point of no return. A pin
// that's falling or lying down is a capsule that knocks into the others, so pins take pins out the way they do in a
// real alley (messengers off the kickbacks included).

#include <array>
#include <cstdint>
#include <vector>

namespace bowl
{
	struct Vec2
	{
		double x{ 0.0 };
		double y{ 0.0 };

		Vec2 operator+(const Vec2& a_o) const { return { x + a_o.x, y + a_o.y }; }
		Vec2 operator-(const Vec2& a_o) const { return { x - a_o.x, y - a_o.y }; }
		Vec2 operator*(double a_s) const { return { x * a_s, y * a_s }; }
		Vec2& operator+=(const Vec2& a_o) { x += a_o.x; y += a_o.y; return *this; }
		Vec2& operator-=(const Vec2& a_o) { x -= a_o.x; y -= a_o.y; return *this; }
		double Dot(const Vec2& a_o) const { return x * a_o.x + y * a_o.y; }
		double Length() const;
		Vec2   Normalized() const;
	};

	struct Vec3
	{
		double x{ 0.0 };
		double y{ 0.0 };
		double z{ 0.0 };
	};

	// Everything about the lane and the equipment. Defaults fit lanes 3 and 6 of Back Alley Bowling and the vanilla
	// BowlingBall01 / BowlingPin01 meshes; every value can be changed in the INI.
	struct Spec
	{
		// the lane
		double headPin{ 1064.0 };       // foul line to the head pin's spot
		double laneHalfWidth{ 64.0 };   // the playing surface, either side of the middle
		double gutterWidth{ 24.0 };     // past the edge: the ball drops into the gutter
		double gutterDepth{ 8.0 };
		double kickback{ 112.0 };       // the side walls round the pin deck, either side of the middle
		double kickbackStart{ 1024.0 }; // ...from here (the masking wall) to the deck's end
		double deckEnd{ 1280.0 };       // past this: the pit
		double oilLength{ 700.0 };      // the oiled front of the lane; dry from here on
		double oilFriction{ 0.04 };     // ball-lane sliding friction on oil
		double dryFriction{ 0.20 };     // ...and on the dry back end
		double rollResistance{ 0.010 }; // a rolling ball slows by this x g
		double gravity{ 902.0 };        // units per second squared (1 m is about 92 units at these meshes' size)

		// the ball
		double ballRadius{ 9.25 };
		double ballMass{ 7.0 };

		// the pins (BowlingPin01: origin at the bottom middle, 35.2 tall, 5.46 at the widest)
		double pinRadius{ 5.46 };      // the belly, where the ball hits
		double pinBodyRadius{ 5.0 };   // the capsule a falling pin knocks others with
		double pinHeight{ 35.2 };
		double pinBaseRadius{ 2.4 };   // it rocks about the rim of its base
		double pinCentre{ 13.4 };      // centre of mass above the base
		double pinMass{ 1.45 };
		double pinSpacing{ 28.0 };     // pin spot to pin spot (12 inches at these meshes' scale)
		double pinRestitution{ 0.65 }; // ball on pin
		double pinPinRestitution{ 0.45 };
		double pinWallRestitution{ 0.5 };
		double pinSlideFriction{ 0.35 }; // a pin sliding on its side (or its base) on the deck
		double ballWallRestitution{ 0.4 };
	};

	enum class BallState : std::uint8_t
	{
		kHeld,    // not thrown yet
		kLane,    // on the lane
		kGutter,  // in a gutter: rolls on to the pit, never touches a pin
		kPit,     // gone off the end
	};

	enum class PinState : std::uint8_t
	{
		kStanding,  // upright, or rocking on its base
		kFalling,   // past the tipping point, on its way down
		kDown,      // lying on the deck (can still slide and knock others)
		kGone,      // in the pit or a gutter
		kSwept,     // taken away between balls
	};

	struct Ball
	{
		BallState state{ BallState::kHeld };
		Vec2      pos;
		double    z{ 0.0 };   // centre height above the lane
		Vec2      vel;
		Vec3      spin;       // angular velocity, radians per second
		// (for drawing) which way round it is: a lane-frame vector = orient * a vector in the ball's own frame
		double orient[3][3]{ { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
	};

	struct Pin
	{
		PinState state{ PinState::kStanding };
		Vec2     pos;          // bottom middle
		Vec2     vel;
		Vec2     tiltDir{ 1.0, 0.0 };  // the way its top leans
		double   tilt{ 0.0 };          // radians off upright
		double   tiltRate{ 0.0 };
		double   yaw{ 0.0 };           // (for drawing) turned about its own axis
		double   yawRate{ 0.0 };
		double   z{ 0.0 };             // its base's height (sinks into the pit or a gutter as it goes)
		double   drop{ 0.0 };          // falling speed once it's left the deck
	};

	// a_m (a rotation) turned on by angular velocity a_w for a_dt seconds
	void RotateBy(double a_m[3][3], const Vec3& a_w, double a_dt);

	// What happened during a step: for sounds.
	struct Event
	{
		enum class Kind : std::uint8_t
		{
			kBallPin,
			kPinPin,
			kPinWall,
			kPinDeck,    // a pin landing on the deck
			kBallGutter,
			kBallPit,
			kPinPit,
		};
		Kind   kind;
		Vec2   pos;
		double strength;  // impact speed, units per second
		int    pin{ -1 };
	};

	// How the ball leaves the hand.
	struct Release
	{
		double y{ 0.0 };        // across the foul line (+ left)
		double angle{ 0.0 };    // radians off straight down the lane (+ left)
		double speed{ 650.0 };  // units per second
		double revs{ 30.0 };    // spin, radians per second
		double axis{ 0.0 };     // the spin axis turned towards the throw, radians (+ hooks left, - right, 0 straight)
	};

	class Sim
	{
	public:
		static constexpr int    kPins = 10;
		static constexpr double kStep = 1.0 / 240.0;

		explicit Sim(const Spec& a_spec = {});

		const Spec& GetSpec() const { return _spec; }
		void        SetSpec(const Spec& a_spec) { _spec = a_spec; }

		// all ten pins back on their spots
		void Rack();
		// the pins that are down are taken away, the standing ones stand up straight where they are
		void Sweep();
		// where pin n (0 = the head pin, 1..9 the 2- to 10-pin) stands in a full rack
		Vec2 Spot(int a_pin) const;

		// the ball at the foul line, ready to throw
		void Hold(double a_y);
		void Throw(const Release& a_release);

		// runs a_seconds of the world (in fixed steps; leftover time carries to the next call)
		void Advance(double a_seconds);
		// one fixed step
		void Step();

		// the ball's gone and nothing's moved for a while (or it's taken too long)
		bool Settled() const;
		double TimeSinceThrow() const { return _sinceThrow; }

		// pins standing, as a bitmask (bit n = pin n), and how many
		std::uint32_t StandingMask() const;
		int           StandingCount() const;
		// pins knocked down by the ball just thrown (counting from the pins that stood when it was thrown)
		int DownThisBall() const;

		const Ball&                     GetBall() const { return _ball; }
		const std::array<Pin, kPins>&   Pins() const { return _pins; }
		std::vector<Event>              TakeEvents();

		// the pin's top, for drawing: base + its axis * height
		Vec3 PinAxis(int a_pin) const;
		// furthest the ball has got down the lane, and its y there (for the log)
		double BallX() const { return _ball.pos.x; }

	private:
		bool PinActive(const Pin& a_pin) const { return a_pin.state == PinState::kStanding || a_pin.state == PinState::kFalling || a_pin.state == PinState::kDown; }
		double TippingAngle() const;
		double PinInertia() const;  // about the rim of its base
		Vec2   BellyOf(const Pin& a_pin) const;  // the pin's widest part, seen from above
		void   StepBall(double a_dt);
		void   StepPins(double a_dt);
		void   BallHitsPins();
		void   PinsHitPins();
		void   PushPin(Pin& a_pin, const Vec2& a_dir, double a_impulse, double a_height);
		void   AddEvent(Event::Kind a_kind, const Vec2& a_pos, double a_strength, int a_pin = -1);

		Spec                   _spec;
		Ball                   _ball;
		std::array<Pin, kPins> _pins{};
		std::uint32_t          _standingAtThrow{ 0 };
		double                 _carry{ 0.0 };
		double                 _sinceThrow{ 0.0 };
		double                 _still{ 0.0 };  // how long nothing's moved
		bool                   _thrown{ false };
		std::vector<Event>     _events;
	};
}
