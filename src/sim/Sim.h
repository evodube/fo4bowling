#pragma once

// Bowling physics: one ball and ten pins on a lane. Plain C++ (no game headers), so it's tested on its own
// (tests/sim_test.cpp).
//
// The lane's own frame, in game units: x runs from the foul line (0) towards the pins, y is across the lane with +y on
// the bowler's left, z is up from the lane surface. The game maps this onto each lane in the cell (Lanes.h).
//
// The ball is a solid sphere with real friction: it skids through the oil, hooks once the slip between its spin and
// its travel grips the dry back end, and rolls once they match.
//
// The pins are full rigid bodies: a real pin's mass and inertia, its shape built from spheres along its axis (with a
// ring of small ones for its flat foot, so it stands on its base and rocks on the rim). Contacts with the ball, the
// other pins, the deck, the gutters, the kickbacks and the pit are solved with impulses, friction and restitution, so
// pins fly, tumble end over end, roll on their sides, bounce, wobble and settle, and drop into the gutters and pit
// the way they do in a real alley.

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

		Vec3 operator+(const Vec3& a_o) const { return { x + a_o.x, y + a_o.y, z + a_o.z }; }
		Vec3 operator-(const Vec3& a_o) const { return { x - a_o.x, y - a_o.y, z - a_o.z }; }
		Vec3 operator*(double a_s) const { return { x * a_s, y * a_s, z * a_s }; }
		Vec3 operator-() const { return { -x, -y, -z }; }
		Vec3& operator+=(const Vec3& a_o) { x += a_o.x; y += a_o.y; z += a_o.z; return *this; }
		Vec3& operator-=(const Vec3& a_o) { x -= a_o.x; y -= a_o.y; z -= a_o.z; return *this; }
		double Dot(const Vec3& a_o) const { return x * a_o.x + y * a_o.y + z * a_o.z; }
		Vec3   Cross(const Vec3& a_o) const { return { y * a_o.z - z * a_o.y, z * a_o.x - x * a_o.z, x * a_o.y - y * a_o.x }; }
		double Length() const;
		Vec3   Normalized() const;
	};

	// a rotation as a unit quaternion
	struct Quat
	{
		double w{ 1.0 };
		double x{ 0.0 };
		double y{ 0.0 };
		double z{ 0.0 };

		Vec3 Rotate(const Vec3& a_v) const;
		void ToMatrix(double a_m[3][3]) const;  // a_m * v = Rotate(v)
		static Quat AxisAngle(const Vec3& a_axis, double a_angle);
		Quat operator*(const Quat& a_o) const;
	};

	// Everything about the lane and the equipment. Defaults fit lanes 3 and 6 of Back Alley Bowling and the vanilla
	// BowlingBall01 / BowlingPin01 meshes; every value can be changed in the INI.
	struct Spec
	{
		// the lane
		double headPin{ 1064.0 };       // foul line to the head pin's spot
		double laneHalfWidth{ 64.0 };   // the playing surface, either side of the middle
		double gutterWidth{ 38.0 };     // past the edge: the ball drops into the gutter (out to the raised rail at 102)
		double gutterDepth{ 8.0 };
		double kickback{ 112.0 };       // the side walls round the pin deck, either side of the middle
		double kickbackStart{ 1024.0 }; // ...from here (the masking wall) to the deck's end
		double deckEnd{ 1205.0 };       // past this: the pit (a slot in front of the back wall)
		double pitDepth{ 0.0 };         // (the alley's floor runs on to the wall: the slot's floor is level with the deck)
		double pitBack{ 27.0 };         // the back wall, this far past the deck's end (1232: the cell's own wall)
		double pitBackRestitution{ 0.45 };  // pins and the ball bounce off it
		// the line a ball runs along in a gutter: in the channel against the outer rail, clear of the lane's edge
		double GutterLine() const { return laneHalfWidth + gutterWidth - ballRadius - 1.5; }
		double returnSide{ 0.0 };       // off the back wall the ball heads this way (+1 left, -1 right: the ball return's side; 0: either)
		double oilLength{ 400.0 };      // the oiled front of the lane; dry from here on
		double oilFriction{ 0.04 };     // ball-lane sliding friction on oil
		double dryFriction{ 0.20 };     // ...and on the dry back end
		double rollResistance{ 0.010 }; // a rolling ball slows by this x g
		double gravity{ 902.0 };        // units per second squared (1 m is about 92 units at these meshes' size)

		// the ball
		double ballRadius{ 9.25 };
		double ballMass{ 7.0 };

		// the pins (BowlingPin01: origin at the bottom middle, 35.2 tall, 5.46 at the widest). Mass and inertia are a
		// regulation pin's (1.5 kg, centre of mass 5.7 inches up) at these meshes' scale
		double pinRadius{ 5.46 };      // at the belly, where the ball hits
		double pinHeight{ 35.2 };
		double pinCentre{ 13.4 };      // centre of mass above the base
		double pinMass{ 1.45 };
		double pinGyration{ 9.4 };     // radius of gyration about a line across it through its centre of mass
		double pinAxialGyration{ 3.2 };  // ...and about its own long axis
		double pinSpacing{ 28.0 };     // pin spot to pin spot (12 inches at these meshes' scale)
		double pinRestitution{ 0.70 }; // ball on pin
		double pinPinRestitution{ 0.55 };
		double pinDeckRestitution{ 0.25 };
		double pinWallRestitution{ 0.45 };
		double pinFriction{ 0.30 };      // pin on pin, and ball on pin
		double pinDeckFriction{ 0.35 };  // pin on the deck
		double pinRolling{ 5.0 };        // how fast a pin rolling (or spinning) on the deck loses it, per second
	};

	enum class BallState : std::uint8_t
	{
		kHeld,    // not thrown yet
		kLane,    // on the lane
		kGutter,  // in a gutter: rolls on to the pit, never touches a pin
		kPit,     // off the end of the deck: into the slot in front of the back wall, off the wall and away sideways
		kTaken,   // gone into the machine (out of sight)
	};

	enum class PinState : std::uint8_t
	{
		kStanding,  // upright (rocking a little counts)
		kDown,      // knocked over, still on the deck
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
		double    drift{ 0.0 };       // (this throw's drift, from its Release)
		double    driftWave{ 0.0 };
		double    driftPhase{ 0.0 };
		double    pitTime{ 0.0 };     // (how long it's been in the pit)
		bool      hitBack{ false };   // (it's hit the back wall)
		// (for drawing) which way round it is: a lane-frame vector = orient * a vector in the ball's own frame
		double orient[3][3]{ { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
	};

	struct Pin
	{
		PinState state{ PinState::kStanding };
		Vec3     com;        // centre of mass
		Quat     rot;        // its own +z is its long axis, base to head
		Vec3     vel;
		Vec3     angVel;
		bool     asleep{ true };
		double   quiet{ 0.0 };  // how long it's barely moved
		bool     frozen{ false };  // deep in the pit: no longer simulated
	};

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
		// the lane's own say: no lane is perfectly flat and evenly oiled, so a ball drifts a little left or right on its
		// way down. A steady push across the lane, plus a wavering one, in units per second squared (+ left)
		double drift{ 0.0 };
		double driftWave{ 0.0 };
		double driftPhase{ 0.0 };
	};

	// a_m (a rotation) turned on by angular velocity a_w for a_dt seconds
	void RotateBy(double a_m[3][3], const Vec3& a_w, double a_dt);

	class Sim
	{
	public:
		static constexpr int    kPins = 10;
		static constexpr double kStep = 1.0 / 480.0;

		explicit Sim(const Spec& a_spec = {});

		const Spec& GetSpec() const { return _spec; }
		void        SetSpec(const Spec& a_spec) { _spec = a_spec; }

		// all ten pins back on their spots. a_jitter: a pinsetter never puts them down exactly on the spot; each lands up
		// to about that far off it (a_seed picks how)
		void Rack(double a_jitter = 0.0, std::uint32_t a_seed = 0);
		// the pins that are down are taken away, the standing ones stand up straight where they are
		void Sweep();
		// no pins at all (for working out a ball's path on its own)
		void ClearPins();
		// call it now, moving or not: a pin still standing upright is stood straight where it is, anything else stays
		// down where it lies, and everything stops (the ball carries on)
		void ForceSettle();
		// only the pins in a_mask left standing (bit n = pin n), the rest swept away
		void KeepOnly(std::uint32_t a_mask);
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
		bool   Settled() const;
		double TimeSinceThrow() const { return _sinceThrow; }

		// pins standing, as a bitmask (bit n = pin n), and how many
		std::uint32_t StandingMask() const;
		int           StandingCount() const;
		// pins knocked down by the ball just thrown (counting from the pins that stood when it was thrown)
		int DownThisBall() const;

		const Ball&                   GetBall() const { return _ball; }
		const std::array<Pin, kPins>& Pins() const { return _pins; }
		std::vector<Event>            TakeEvents();

		// for drawing pin n: the bottom middle of its base (the mesh's origin), and its rotation (lane = a_rot * own)
		void PinPose(int a_pin, Vec3& a_base, double a_rot[3][3]) const;
		// the pin's long axis (base to head), lane frame
		Vec3 PinAxis(int a_pin) const;
		bool PinUpright(const Pin& a_pin) const;
		double BallX() const { return _ball.pos.x; }

	private:
		struct Sphere
		{
			Vec3   at;  // from the centre of mass, the pin's own frame
			double r;
		};

		// one point where two things touch, solved for with impulses
		struct Contact
		{
			int    a;        // pin index, or kWorld / kBallBody
			int    b;        // pin index (always a pin)
			Vec3   point;
			Vec3   n;        // from a to b
			double depth;
			double restitution;
			double friction;
			Vec3   ra, rb;   // from each body's centre of mass to the point
			double massN{ 0.0 };
			double bias{ 0.0 };
			double jn{ 0.0 };
			double jt{ 0.0 };
			bool   eventSent{ false };
		};
		static constexpr int kWorld = -1;
		static constexpr int kBallBody = -2;

		void   BuildShape();
		void   StandPin(Pin& a_pin, const Vec2& a_at, double a_yaw) const;
		double FloorAt(double a_x, double a_y) const;
		void   StepBall(double a_dt);
		void   StepPins(double a_dt);
		void   Collide(std::vector<Contact>& a_out);
		Vec3   VelocityAt(int a_body, const Vec3& a_r) const;
		Vec3   InvInertiaTimes(int a_pin, const Vec3& a_v) const;
		void   ApplyImpulse(int a_body, const Vec3& a_r, const Vec3& a_j);
		double MassAlong(int a_body, const Vec3& a_r, const Vec3& a_dir) const;
		void   AddEvent(Event::Kind a_kind, const Vec2& a_pos, double a_strength, int a_pin = -1);

		Spec                   _spec;
		std::vector<Sphere>    _shape;
		Ball                   _ball;
		std::array<Pin, kPins> _pins{};
		std::uint32_t          _standingAtThrow{ 0 };
		double                 _carry{ 0.0 };
		double                 _sinceThrow{ 0.0 };
		double                 _still{ 0.0 };  // how long nothing's moved
		bool                   _thrown{ false };
		std::vector<Event>     _events;
		std::vector<Contact>   _contacts;
		std::array<double, kPins> _lastWallHit{};
	};
}
