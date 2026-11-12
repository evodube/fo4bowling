#pragma once

#include "Lanes.h"
#include "sim/Ai.h"
#include "sim/Score.h"
#include "sim/Sim.h"

namespace fb
{
	// One game of bowling at one of Back Alley Bowling's lanes: the pins and balls in the world, the camera, the
	// throw, the scoring and what the HUD shows. Everything here runs on the game's main thread except OnPresent (the
	// render thread: it only queues the tick) and Hud() (Menu Framework's draw: it only copies the HUD state).
	class Game
	{
	public:
		static Game& Get()
		{
			static Game instance;
			return instance;
		}

		// ------------------------------------------------ what the HUD draws
		struct HudState
		{
			std::string              prompt;      // "F) START BOWLING", under the crosshair at a ball return
			std::string              promptSub;   // "<  3 PLAYERS  >     LANE 3"
			std::string              promptVs;    // "VS CAIT, PRESTON GARVEY"
			bool                     game{ false };
			int                      lane{ 0 };
			std::vector<std::string> names;
			std::vector<bowl::Card>  cards;
			int                      bowler{ 0 };
			bool                     aiming{ false };
			bool                     charging{ false };
			float                    power{ 0.0f };
			float                    hook{ 0.0f };  // percent of full, + to the left
			float                    maxHook{ 60.0f };
			int                      board{ 20 };   // where the ball is on the foul line (1 = the right edge)
			std::string              banner;        // "STRIKE!"
			std::string              bannerSub;
			float                    bannerAlpha{ 0.0f };
			float                    bannerAge{ 0.0f };  // seconds since it went up (for its entrance)
			bool                     over{ false };
			std::string              keys;          // the controls, along the bottom
			std::vector<std::array<float, 2>> guide;  // the aim line, screen points 0..1 from the ball outwards
		};
		HudState Hud() const;

		// ------------------------------------------------ the game's events
		void OnPresent();      // render thread, every frame
		void Forget();         // a save's being loaded: everything we had is gone with the old game
		void ClearForSave();   // a save's being made: the game stops, so nothing of ours is in it

		// ------------------------------------------------ free camera hooks (CameraHook.cpp)
		bool OnLookMouse(const RE::MouseMoveEvent* a_event);  // true: the movement's power, not looking
		void BeforeFreeCameraUpdate(RE::FreeCameraState* a_free);
		void OnFreeCameraUpdate(RE::FreeCameraState* a_free);
		bool FreeCameraTranslation(const RE::FreeCameraState* a_free, RE::NiPoint3& a_out);

	private:
		Game() = default;

		enum class Phase
		{
			kNone,      // no game
			kPickup,    // the ball comes off the return into your hand
			kAiming,
			kRolling,   // the ball's on its way and the pins are flying
			kResult,    // what that ball did, for a moment
			kSetting,   // the pinsetter sweeps and sets
			kOver,      // the final scores, for a moment
		};

		// where one of the two balls is
		enum class BallAt
		{
			kReturn,     // on the ball return (resting, or rolling down it)
			kHand,       // being picked up, then held at the foul line
			kLane,       // thrown
			kGone,       // in the pit, on its way back
			kGutter,     // rolling back up the gutter on the return's side, to the return
		};

		struct BallSlot
		{
			RE::ObjectRefHandle ref;
			BallAt              at{ BallAt::kReturn };
			float               along{ 0.0f };      // on the return: how far along it (its own y)
			float               target{ 0.0f };     // ...where it's rolling to
			float               hidden{ 0.0f };     // in the pit: seconds until it comes back out of the hood
			RE::NiPoint3        pos;                // where it was last drawn
			double              orient[3][3]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };  // world = orient * the ball's own
			bool                prepared{ false };  // its collision's off
			double              runX{ 0.0 }, runY{ 0.0 }, runZ{ 0.0 };  // (rolling back: where it is, lane frame)
		};

		// two balls resting on each lane's return while no game's on there, for the look of the place
		struct Display
		{
			std::array<RE::ObjectRefHandle, 2> balls;
			std::array<bool, 2>                prepared{};
			std::vector<RE::ObjectRefHandle>   hidden;  // (the lane's own balls on its return, out of the way meanwhile)
		};
		void UpdateDisplay();
		void RemoveDisplay(int a_lane);  // (-1: both)
		std::array<Display, lanes::kLanes.size()> _display;

		// ------------------------------------------------ main thread
		void Tick();
		void PollInput(float a_dt);
		void UpdateLook();
		const lanes::Lane* LookedAtLane() const;

		// who's playing: you alone, or you against 1 to 3 others (companions, then the people nearby)
		struct Mode
		{
			std::string                      label;   // "SOLO", "3 PLAYERS"
			std::vector<RE::ObjectRefHandle> npcs;    // the people nearby you'd play, in order
			std::vector<std::string>         names;   // their names
			int                              missing{ 0 };  // how many more people it needs nearby (0: it can start)
		};
		static constexpr float kOpponentRange = 1500.0f;  // how near someone has to be to be asked to bowl
		struct Bowler
		{
			std::string         name;
			bool                ai{ false };
			float               skill{ 0.6f };
			bool                leftHanded{ false };
			RE::ObjectRefHandle actor;
			double              releaseY{ 0.0 };  // (a player's own set-up, kept between their turns)
			float               hook{ 0.0f };
			float               aim{ 0.0f };
		};
		void              BuildModes();
		static std::string PlayerName();
		static std::string NameOf(RE::Actor* a_actor);

		void StartGame(const lanes::Lane& a_lane, const Mode& a_mode);
		void HoldOpponents(bool a_hold);
		void PlanAiThrow();
		bool AiTurn() const;
		void EndGame(const std::string& a_why);
		void Throw(float a_power, bool a_ai);
		void BallSettled();
		// the X and the PA's call: as soon as the last pin goes down for a strike or spare, else when the ball's scored
		void CallBall(bool a_freshRack, int a_down, int a_standing);
		void NextBall(bool a_racked = false);  // (a_racked: the pinsetter has already swept or racked)

		// ------------------------------------------------ the pinsetter (Pinsetter.cpp)
		struct SetPin
		{
			bowl::Vec3 base;          // where it was when the machine started (lane frame)
			double     rot[3][3]{};
			double     minX{ 0.0 };   // its nearest point to the bowler
			bool       shown{ false };
			bool       onDeck{ false };    // (on the deck in front of the sweep: pushed back into the pit)
			bool       standing{ false };  // (a second ball's to come: lifted and set down again)
		};
		void  SpawnMachine(const lanes::Lane& a_lane);
		void  RemoveMachine();
		bool  MachineOn() const;
		void  StartSetting();  // does the sim's sweep or rack, then shows the machine doing it
		float SettingLength() const;
		void  SettingSounds(float a_from, float a_to);  // (what the machine does between those two times)
		float SweepTakesBall() const;  // (when, in the machine's cycle, the sweep has the thrown ball)
		bool  SettingPinPose(int a_pin, bowl::Vec3& a_base, double a_rot[3][3]) const;  // false: out of sight
		void  DrawMachine();
		void  DimLights();
		void Banner(const std::string& a_title, const std::string& a_sub, float a_seconds);
		void Say(const std::string& a_message);

		// ------------------------------------------------ the world (Game.cpp)
		RE::ObjectRefHandle Spawn(RE::TESBoundObject* a_form, const RE::NiPoint3& a_pos) const;
		void                Prepare(RE::TESObjectREFR* a_ref, bool& a_done);
		static void         PlaceNode(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, bool a_hidden, float a_scale = 1.0f);
		void                HideVanilla(const lanes::Lane& a_lane);
		void                RestoreVanilla();
		void                DrawPins();
		void                DrawBalls(float a_dt);
		RE::NiPoint3        ReturnPoint(float a_along, float a_height) const;  // a point on the ball return, its frame
		static RE::NiPoint3 ReturnPointOf(const lanes::Lane& a_lane, float a_along, float a_height);
		static double       ReturnSide(const lanes::Lane& a_lane);  // +1 the return's left of the lane, -1 right
		RE::NiPoint3        HeldPoint() const;                                  // the ball at the foul line
		bool                StillHere() const;  // in the alley, alive, not in a fight
		std::string         SplitName() const;  // "7-10 SPLIT" or empty
		void                UpdateGuide();
		void                PlaySounds(const std::vector<bowl::Event>& a_events);

		// ------------------------------------------------ the camera (Camera.cpp)
		void EnterCamera();
		void LeaveCamera();
		void BlockControls(bool a_block);
		void BlockActivate(bool a_block);
		void HoldPlayerView();
		void UpdateCameraGoal(float a_dt);

		// game state
		const lanes::Lane*                       _lane{ nullptr };
		Phase                                    _phase{ Phase::kNone };
		float                                    _phaseTime{ 0.0f };
		std::unique_ptr<bowl::Sim>               _sim;
		bowl::Match                              _match{ 1 };
		std::vector<std::string>                 _names;
		std::vector<Bowler>                      _bowlers;
		std::vector<Mode>                        _modes;
		int                                      _mode{ 0 };
		bool                                     _aiPlanned{ false };
		float                                    _aiThereAt{ -1.0f };  // (when an NPC got to their spot)
		bowl::AiThrow                            _aiPlan;
		std::array<RE::ObjectRefHandle, 10>      _pins{};
		std::array<bool, 10>                     _pinPrepared{};
		RE::ObjectRefHandle                      _deck;
		RE::ObjectRefHandle                      _sweep;
		std::array<RE::ObjectRefHandle, 2>       _lights{};
		bool                                     _deckPrepared{ false };
		RE::ObjectRefHandle                      _table;
		bool                                     _tablePrepared{ false };
		bool                                     _sweepPrepared{ false };
		bool                                     _setFresh{ false };
		std::array<SetPin, 10>                   _setPins{};
		std::array<BallSlot, 2>                  _balls{};
		int                                      _active{ 0 };     // the ball in play
		std::vector<RE::ObjectRefHandle>         _hiddenVanilla;   // pins and balls we disabled
		std::vector<RE::ObjectRefHandle>         _held;            // the people playing, kept where they stand
		double                                   _releaseY{ 0.0 };
		float                                    _hook{ 0.0f };    // percent of full hook, + to the left (an AI's planned spin; yours comes from the swing)
		float                                    _swingHook{ 0.0f };  // what this swing adds to the hook (sideways mouse)
		float                                    _lastPower{ 0.6f };
		float HookNow() const { return std::clamp(_hook + _swingHook, -100.0f, 100.0f); }
		std::vector<std::array<float, 2>>        _guide;
		std::mt19937                             _rng{ std::random_device{}() };
		float                                    _aimAngle{ 0.0f }; // radians, + left
		bool                                     _lastBallGutter{ false };
		std::array<int, 4>                       _strikeRun{};  // strikes in a row, per bowler (three is a turkey)
		bool                                     _rackFullAtThrow{ false };
		double                                   _impactAt{ -1.0 };  // (when the ball met the pins this throw)
		double                                   _reachedAt{ -1.0 }; // (when it reached the pins or the pit)
		double                                   _allDownAt{ -1.0 }; // (since when every pin's been down, this ball)
		bool                                     _called{ false };   // (the X and the PA have had their say this ball)
		bool                                     _ballLingers{ false };  // (the thrown ball still shows where it stopped)
		bool                                     _impactDone{ false };
		RE::NiPoint3                             _impactPos{};
		double                                   _lastPinSound{ -1.0 };
		std::chrono::steady_clock::time_point    _lastTick{};
		bool                                     _haveLastTick{ false };

		// input
		std::atomic<bool>                        _tickPending{ false };
		const lanes::Lane*                       _lookLane{ nullptr };
		std::chrono::steady_clock::time_point    _lookTime{};
		int                                      _activateVk{ 0x46 };  // F, until the control map says otherwise
		bool                                     _activateWasDown{ true };
		bool                                     _lmbWasDown{ true };
		std::array<bool, 5>                      _keyWasDown{};
		std::chrono::steady_clock::time_point    _quitAt{};
		bool                                     _quitArmed{ false };
		std::atomic<bool>                        _charging{ false };
		std::atomic<float>                       _power{ 0.0f };
		const void*                              _lastMouseEvent{ nullptr };
		std::uint32_t                            _lastMouseTime{ 0 };

		// the camera (the game's free camera, moved by us)
		mutable std::mutex                       _camMutex;
		bool                                     _inCamera{ false };
		bool                                     _camMapped{ false };
		int                                      _mapFrames{ 0 };
		int                                      _yawIndex{ 0 };
		float                                    _pitchSign{ 1.0f };
		float                                    _entryYaw{ 0.0f };
		float                                    _entryPitch{ 0.0f };
		RE::NiPoint2                             _entryFreeRotation{};
		bool                                     _haveEntryView{ false };
		float                                    _aimYaw{ lanes::kDownLaneYaw };
		RE::NiPoint3                             _camPos{};
		RE::NiPoint3                             _camLook{};
		RE::NiPoint3                             _camGoalPos{};
		RE::NiPoint3                             _camGoalLook{};
		bool                                     _camGoalSet{ false };
		bool                                     _camPlaced{ false };
		float                                    _camEase{ 4.0f };
		std::chrono::steady_clock::time_point    _lastCamUpdate{};
		std::chrono::steady_clock::time_point    _camRetryAt{};
		bool                                     _haveCamUpdate{ false };

		RE::BSTSmartPointer<RE::BSInputEnableLayer> _inputLayer;
		bool                                     _controlsBlocked{ false };
		RE::BSTSmartPointer<RE::BSInputEnableLayer> _activateLayer;
		bool                                     _activateBlocked{ false };

		// the HUD's copy
		mutable std::mutex _hudMutex;
		HudState           _hud;
		std::string        _banner;
		std::string        _bannerSub;
		float              _bannerTime{ 0.0f };
		float              _bannerLength{ 0.0f };
	};
}
