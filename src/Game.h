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
			std::string              promptSub;   // "< 1 PLAYER >"
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
			kOver,      // the final scores, for a moment
		};

		// where one of the two balls is
		enum class BallAt
		{
			kReturn,     // on the ball return (resting, or rolling down it)
			kHand,       // being picked up, then held at the foul line
			kLane,       // thrown
			kGone,       // in the pit, on its way back
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
		};

		// ------------------------------------------------ main thread
		void Tick();
		void PollInput(float a_dt);
		void UpdateLook();
		const lanes::Lane* LookedAtLane() const;

		// who's playing: you (and friends at one keyboard), or you against companions
		struct Mode
		{
			std::string                      label;   // "2 PLAYERS", "NATE VS CAIT"
			int                              humans{ 1 };
			std::vector<RE::ObjectRefHandle> npcs;
		};
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
		void PlanAiThrow();
		bool AiTurn() const;
		void EndGame(const std::string& a_why);
		void Throw(float a_power, bool a_ai);
		void BallSettled();
		void NextBall();
		void Banner(const std::string& a_title, const std::string& a_sub, float a_seconds);
		void Say(const std::string& a_message);

		// ------------------------------------------------ the world (Game.cpp)
		RE::ObjectRefHandle Spawn(RE::TESBoundObject* a_form, const RE::NiPoint3& a_pos) const;
		void                Prepare(RE::TESObjectREFR* a_ref, bool& a_done);
		static void         PlaceNode(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, bool a_hidden);
		void                HideVanilla(const lanes::Lane& a_lane);
		void                RestoreVanilla();
		void                DrawPins();
		void                DrawBalls(float a_dt);
		RE::NiPoint3        ReturnPoint(float a_along, float a_height) const;  // a point on the ball return, its frame
		RE::NiPoint3        HeldPoint() const;                                  // the ball at the foul line
		bool                StillHere() const;  // in the alley, alive, not in a fight
		std::string         SplitName() const;  // "7-10 SPLIT" or empty
		void                UpdateGuide();

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
		bowl::AiThrow                            _aiPlan;
		std::array<RE::ObjectRefHandle, 10>      _pins{};
		std::array<bool, 10>                     _pinPrepared{};
		std::array<BallSlot, 2>                  _balls{};
		int                                      _active{ 0 };     // the ball in play
		std::vector<RE::ObjectRefHandle>         _hiddenVanilla;   // pins and balls we disabled
		double                                   _releaseY{ 0.0 };
		float                                    _hook{ 0.0f };    // percent of full hook, + to the left
		float                                    _swingHook{ 0.0f };  // what this swing adds to the hook (sideways mouse)
		float                                    _lastPower{ 0.6f };
		float HookNow() const { return std::clamp(_hook + _swingHook, -100.0f, 100.0f); }
		std::vector<std::array<float, 2>>        _guide;
		std::mt19937                             _rng{ std::random_device{}() };
		float                                    _aimAngle{ 0.0f }; // radians, + left
		bool                                     _lastBallGutter{ false };
		std::chrono::steady_clock::time_point    _lastTick{};
		bool                                     _haveLastTick{ false };

		// input
		std::atomic<bool>                        _tickPending{ false };
		std::string                              _modeLabel;        // (the picked mode, kept as the list changes)
		const lanes::Lane*                       _lookLane{ nullptr };
		std::chrono::steady_clock::time_point    _lookTime{};
		int                                      _activateVk{ 0x46 };  // F, until the control map says otherwise
		bool                                     _activateWasDown{ true };
		bool                                     _lmbWasDown{ true };
		std::array<bool, 7>                      _keyWasDown{};
		float                                    _hookRepeat{ 0.0f };
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
