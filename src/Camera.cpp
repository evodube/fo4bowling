// The bowling camera: the game's own free camera ("tfc"), moved by us.
//
// While you aim it sits behind the ball at the foul line; the mouse turns it left and right (that's your aim, within a
// few degrees of straight), and holding the left button freezes it while mouse pulls become power. Once the ball's
// thrown it rolls along behind it, stops part way down the lane, and watches the pins fall.

#include "Game.h"

#include "Settings.h"
#include "Util.h"

namespace fb
{
	namespace
	{
		constexpr float kDegToRad = 0.017453292f;

		RE::FreeCameraState* FreeCamera()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			return camera ? static_cast<RE::FreeCameraState*>(camera->cameraStates[RE::CameraStates::kFree].get()) : nullptr;
		}

		bool InFreeCamera()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* free = FreeCamera();
			return camera && free && camera->currentState.get() == free;
		}

		float& Component(RE::FreeCameraState* a_state, int a_index)
		{
			return a_index == 0 ? a_state->rotation.x : a_state->rotation.y;
		}

		RE::NiPoint3 Ease(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_rate, float a_dt)
		{
			const float k = 1.0f - std::exp(-a_rate * a_dt);
			return a_from + (a_to - a_from) * k;
		}
	}

	void Game::EnterCamera()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (!player || !camera || !FreeCamera()) {
			return;
		}
		_entryYaw = player->data.angle.z;
		_entryPitch = player->data.angle.x;
		if (auto* third = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraStates::k3rdPerson].get())) {
			_entryFreeRotation = third->freeRotation;
		}
		_haveEntryView = true;
		{
			std::scoped_lock lock(_camMutex);
			_camMapped = false;
			_mapFrames = 0;
			_camPlaced = false;
			_camGoalSet = false;
			_haveCamUpdate = false;
		}
		if (!InFreeCamera()) {
			camera->ToggleFreeCameraMode(false);
		}
		_inCamera = true;
	}

	void Game::LeaveCamera()
	{
		if (!_inCamera) {
			return;
		}
		_inCamera = false;
		HoldPlayerView();  // back exactly as you were looking before the game
		_haveEntryView = false;
		if (InFreeCamera()) {
			if (auto* camera = RE::PlayerCamera::GetSingleton()) {
				camera->ToggleFreeCameraMode(false);
			}
		}
		std::scoped_lock lock(_camMutex);
		_camMapped = false;
		_camPlaced = false;
	}

	void Game::HoldPlayerView()
	{
		if (!_haveEntryView) {
			return;
		}
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			player->data.angle.x = _entryPitch;
			player->data.angle.z = _entryYaw;
		}
		if (auto* camera = RE::PlayerCamera::GetSingleton()) {
			if (auto* third = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraStates::k3rdPerson].get())) {
				third->freeRotation = _entryFreeRotation;
			}
		}
	}

	void Game::BlockControls(bool a_block)
	{
		if (a_block == _controlsBlocked) {
			return;
		}
		auto* manager = RE::BSInputEnableManager::GetSingleton();
		if (!manager) {
			return;
		}
		if (!_inputLayer && !manager->AllocateNewLayer(_inputLayer, "fo4bowling playing")) {
			log::warn("could not get an input layer; controls stay on while bowling");
			return;
		}
		// The mouse stays on (it aims). Walking off, weapons, the Pip-Boy and the rest are off while you bowl.
		using Flag = RE::UserEvents::USER_EVENT_FLAG;
		const auto flags = static_cast<Flag>(
			std::to_underlying(Flag::kMovement) | std::to_underlying(Flag::kActivate) | std::to_underlying(Flag::kPOVSwitch) |
			std::to_underlying(Flag::kFighting) | std::to_underlying(Flag::kSneaking) | std::to_underlying(Flag::kMainFour) |
			std::to_underlying(Flag::kWheelZoom) | std::to_underlying(Flag::kJumping) | std::to_underlying(Flag::kVATS));
		manager->EnableUserEvent(_inputLayer->layerID, flags, !a_block, RE::UserEvents::SENDER_ID::kGameplay);
		_controlsBlocked = a_block;
		log::info("player controls {}", a_block ? "off for bowling" : "back on");
	}

	void Game::BlockActivate(bool a_block)
	{
		if (a_block == _activateBlocked) {
			return;
		}
		auto* manager = RE::BSInputEnableManager::GetSingleton();
		if (!manager) {
			return;
		}
		if (!_activateLayer && !manager->AllocateNewLayer(_activateLayer, "fo4bowling ball return")) {
			return;
		}
		manager->EnableUserEvent(_activateLayer->layerID, RE::UserEvents::USER_EVENT_FLAG::kActivate, !a_block, RE::UserEvents::SENDER_ID::kGameplay);
		_activateBlocked = a_block;
	}

	void Game::UpdateCameraGoal([[maybe_unused]] float a_dt)
	{
		// where the camera wants to be when it isn't behind the ball (Tick, after the world's moved)
		if (!_sim || !_lane) {
			return;
		}
		const auto& s = Settings::Get();
		const auto& spec = _sim->GetSpec();
		RE::NiPoint3 pos, look;
		float        ease = 4.0f;
		switch (_phase) {
		case Phase::kRolling:
		case Phase::kResult:
		case Phase::kOver:
			{
				const auto&  b = _sim->GetBall();
				const bool   rolling = _phase == Phase::kRolling && (b.state == bowl::BallState::kLane || b.state == bowl::BallState::kGutter);
				const double deck = spec.headPin + spec.pinSpacing * 1.3;
				if (!s.followBall) {
					pos = lanes::ToWorld(*_lane, -s.camDistance, 0.0, s.camHeight + spec.ballRadius);
					look = lanes::ToWorld(*_lane, deck, 0.0, 10.0);
					ease = 3.0f;
				} else if (rolling) {
					// behind the ball, until it's far enough down: then it waits there and watches
					const double x = std::min(b.pos.x - s.camDistance * 1.2, static_cast<double>(s.followStop));
					pos = lanes::ToWorld(*_lane, x, b.pos.y * 0.5, s.camHeight + 12.0);
					look = b.pos.x < spec.headPin - 120.0 ? lanes::ToWorld(*_lane, b.pos.x + 80.0, b.pos.y, spec.ballRadius) :
					                                        lanes::ToWorld(*_lane, deck, 0.0, 10.0);
					ease = 7.0f;
				} else {
					pos = lanes::ToWorld(*_lane, s.followStop, 0.0, s.camHeight + 16.0);
					look = lanes::ToWorld(*_lane, deck, 0.0, 10.0);
					ease = 3.0f;
				}
				break;
			}
		default:
			return;  // aiming: the camera works that out itself, from the mouse
		}
		std::scoped_lock lock(_camMutex);
		_camGoalPos = pos;
		_camGoalLook = look;
		_camEase = ease;
		_camGoalSet = true;
	}

	// ---------------------------------------------------------------- inside the game's camera update

	bool Game::OnLookMouse(const RE::MouseMoveEvent* a_event)
	{
		if (!a_event || _phase == Phase::kNone || !_inCamera) {
			return false;
		}
		if (!_charging) {
			// rolling and watching: the camera's ours, the mouse doesn't turn it
			return _phase != Phase::kAiming && _phase != Phase::kPickup;
		}
		// the same event can reach both handlers; count it once
		const bool repeat = a_event == _lastMouseEvent && a_event->timeCode == _lastMouseTime;
		_lastMouseEvent = a_event;
		_lastMouseTime = a_event->timeCode;
		if (!repeat) {
			const auto& s = Settings::Get();
			const float pull = static_cast<float>(a_event->mouseInputY) * s.pullPerCount * (s.invertPull ? -1.0f : 1.0f);
			_power = std::clamp(_power.load() + pull, 0.0f, 1.0f);
		}
		return true;
	}

	void Game::BeforeFreeCameraUpdate(RE::FreeCameraState* a_free)
	{
		if (!_inCamera || !a_free || _phase == Phase::kNone) {
			return;
		}
		const bool aiming = (_phase == Phase::kAiming || _phase == Phase::kPickup) && !_charging;
		if (aiming) {
			return;  // the mouse turns the view: that's the aim
		}
		// nothing turns the view but us
		if (auto* camera = a_free->camera) {
			camera->rotationInput.x = 0.0f;
			camera->rotationInput.y = 0.0f;
		}
		if (auto* controls = RE::PlayerControls::GetSingleton()) {
			auto& d = controls->data;
			d.lookInputVec = {};
			d.lookInputVecNormalized = {};
			d.prevLookVec = {};
			d.rotationSpeedSpring.position = {};
			d.rotationSpeedSpring.velocity = {};
		}
	}

	void Game::OnFreeCameraUpdate(RE::FreeCameraState* a_free)
	{
		if (!_inCamera || !a_free || _phase == Phase::kNone || !_lane) {
			return;
		}
		const auto& s = Settings::Get();
		std::scoped_lock lock(_camMutex);

		// The free camera keeps two angles; which one is the heading is worked out once, from where the player was
		// looking when it started (it starts there)
		if (!_camMapped) {
			const float a = a_free->rotation.x;
			const float b = a_free->rotation.y;
			if (a == 0.0f && b == 0.0f && ++_mapFrames < 10) {
				return;  // not set up yet
			}
			_yawIndex = std::abs(util::AngleDiff(a, _entryYaw)) <= std::abs(util::AngleDiff(b, _entryYaw)) ? 0 : 1;
			const float pitchValue = _yawIndex == 0 ? b : a;
			_pitchSign = 1.0f;
			if (std::abs(_entryPitch) > 0.05f && std::abs(pitchValue) > 0.05f && (pitchValue > 0.0f) != (_entryPitch > 0.0f)) {
				_pitchSign = -1.0f;
			}
			log::info("free camera ({:.3f}, {:.3f}) vs player yaw {:.3f} pitch {:.3f}: heading is value {}, pitch sign {}", a, b, _entryYaw,
				_entryPitch, _yawIndex, _pitchSign);
			Component(a_free, _yawIndex) = _aimYaw;
			_camMapped = true;
		}

		const auto  now = std::chrono::steady_clock::now();
		const float dt = _haveCamUpdate ? std::min(0.1f, std::chrono::duration<float>(now - _lastCamUpdate).count()) : 0.0f;
		_lastCamUpdate = now;
		_haveCamUpdate = true;

		float yaw = 0.0f;
		float pitch = 0.0f;
		if (_phase == Phase::kAiming || _phase == Phase::kPickup) {
			// the aim: the heading the mouse gave it, kept within a few degrees of straight down the lane (and held
			// still while charging)
			const float maxAim = s.maxAim * kDegToRad;
			yaw = _charging ? _aimYaw : Component(a_free, _yawIndex);
			const float off = std::clamp(util::AngleDiff(yaw, lanes::kDownLaneYaw), -maxAim, maxAim);
			yaw = lanes::kDownLaneYaw + off;
			_aimYaw = yaw;
			_aimAngle = -off;  // (turning the heading towards +x world is to the bowler's right)
			pitch = s.camPitch * kDegToRad;
			const RE::NiPoint3 fwd{ std::sin(yaw), std::cos(yaw), 0.0f };
			const RE::NiPoint3 goal = HeldPoint() - fwd * s.camDistance + RE::NiPoint3{ 0.0f, 0.0f, s.camHeight };
			_camPos = _camPlaced ? Ease(_camPos, goal, 6.0f, dt) : goal;
			_camLook = goal + fwd * 300.0f;
			_camPlaced = true;
		} else if (_camGoalSet) {
			_camPos = _camPlaced ? Ease(_camPos, _camGoalPos, _camEase, dt) : _camGoalPos;
			_camLook = _camPlaced ? Ease(_camLook, _camGoalLook, _camEase + 2.0f, dt) : _camGoalLook;
			_camPlaced = true;
			const RE::NiPoint3 d = _camLook - _camPos;
			yaw = std::atan2(d.x, d.y);
			pitch = std::atan2(-d.z, std::sqrt(d.x * d.x + d.y * d.y));
		} else {
			return;
		}
		Component(a_free, _yawIndex) = yaw;
		Component(a_free, 1 - _yawIndex) = _pitchSign * pitch;
		a_free->translation = _camPos;

		// the mouse reaches the player's own look controls too: undone every frame so the player doesn't turn
		HoldPlayerView();
	}

	bool Game::FreeCameraTranslation(const RE::FreeCameraState* a_free, RE::NiPoint3& a_out)
	{
		if (!_inCamera || !a_free || _phase == Phase::kNone) {
			return false;
		}
		std::scoped_lock lock(_camMutex);
		if (!_camMapped || !_camPlaced) {
			return false;
		}
		a_out = _camPos;
		return true;
	}
}
