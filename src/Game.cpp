// A game of bowling: starting it at a ball return, the throw, the pins and balls in the world, the frames and the
// scores.
//
// The pins and balls are ordinary BowlingPin01 / BowlingBall01 references we place ourselves, with their Havok off so
// the physics engine never fights our own (sim/Sim). The lane's own vanilla pins (and any balls on its return) are
// disabled while a game's on and enabled again when it ends; nothing in the cell is edited, so precombines and previs
// are untouched.

#include "Game.h"

#include "Audio.h"
#include "Hud.h"
#include "Settings.h"
#include "Util.h"

namespace fb
{
	namespace
	{
		constexpr float  kMaxFrame = 0.05f;        // longer frames (hitches) are clamped
		constexpr float  kPickupSeconds = 0.6f;    // the ball off the return and into your hand
		constexpr float  kResultSeconds = 1.8f;    // what a ball did stays up this long before the next one
		constexpr float  kMachineAfter = 0.8f;     // ...or, with the pinsetter, this long before it starts
		constexpr double kImpactLook = 0.12;       // the crash or the hit is picked this long after the ball meets the pins
		constexpr int    kCrashPins = 4;           // ...the crash when at least this many pins are going by then
		constexpr float  kOverSeconds = 3.0f;      // the final scores, then the game packs itself away
		constexpr float  kReturnDelay = 1.6f;      // a thrown ball spends this long in the machine before it comes back
		constexpr double kAllDownHold = 0.12;     // every pin down this long (not just one rocking past upright): it's called
		constexpr double kCallAfter = 2.4;         // the ball's called this long after it reaches the pins, whatever's still moving
		constexpr float  kQuitWindow = 3.0f;       // the quit key twice within this many seconds stops the game
		constexpr float  kAiStandSeconds = 0.5f;   // an NPC stands in the middle with the ball this long, then walks to their spot
		constexpr float  kAiSettleSeconds = 0.8f;  // ...and stands there this long before throwing
		constexpr double kPi = 3.14159265358979323846;

		RE::TESBoundObject* Base(RE::TESFormID a_id)
		{
			auto* form = RE::TESForm::GetFormByID(a_id);
			return form && form->IsBoundObject() ? static_cast<RE::TESBoundObject*>(form) : nullptr;
		}

		RE::TESObjectREFR* Ref(RE::TESFormID a_id)
		{
			return RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_id);
		}

		std::string PinList(std::uint32_t a_mask)
		{
			std::string out;
			for (int i = 0; i < bowl::Sim::kPins; ++i) {
				if (a_mask & (1u << i)) {
					out += (out.empty() ? "" : "-") + std::to_string(i + 1);
				}
			}
			return out;
		}
	}

	// ---------------------------------------------------------------- render thread

	void Game::OnPresent()
	{
		if (_tickPending.exchange(true)) {
			return;
		}
		auto* tasks = F4SE::GetTaskInterface();
		if (!tasks) {
			_tickPending = false;
			return;
		}
		tasks->AddTask([this]() {
			Tick();
			_tickPending = false;
		});
	}

	Game::HudState Game::Hud() const
	{
		std::scoped_lock lock(_hudMutex);
		auto h = _hud;
		h.charging = _charging.load();
		h.power = _power.load();
		return h;
	}

	// ---------------------------------------------------------------- main thread

	void Game::Tick()
	{
		const auto now = std::chrono::steady_clock::now();
		const float dt = _haveLastTick ? std::min(kMaxFrame, std::chrono::duration<float>(now - _lastTick).count()) : 0.0f;
		_lastTick = now;
		_haveLastTick = true;

		const bool paused = util::GamePaused();
		UpdateDisplay();
		if (_phase == Phase::kNone) {
			UpdateLook();
			if (!paused) {
				PollInput(dt);
			}
		} else {
			if (!StillHere()) {
				EndGame("Bowling: game stopped");
				return;
			}
			if (!paused) {
				PollInput(dt);
				_phaseTime += dt;
				switch (_phase) {
				case Phase::kPickup:
					if (_phaseTime >= kPickupSeconds) {
						_phase = Phase::kAiming;
						_phaseTime = 0.0f;
					}
					break;
				case Phase::kAiming:
					if (AiTurn()) {
						// an NPC's turn: they stand in the middle with the ball, step over to their spot (or stay), settle
						// and throw
						if (!_aiPlanned) {
							PlanAiThrow();
						} else if (_phaseTime >= kAiStandSeconds) {
							const double to = _aiPlan.release.y;
							const double step = Settings::Get().moveSpeed * dt;
							const bool   there = std::abs(to - _releaseY) <= step;
							_releaseY = there ? to : _releaseY + (to > _releaseY ? step : -step);
							if (there && _aiThereAt < 0.0f) {
								_aiThereAt = _phaseTime;
							}
							if (_aiThereAt >= 0.0f && _phaseTime - _aiThereAt >= kAiSettleSeconds) {
								Throw(static_cast<float>(_aiPlan.power), true);
							}
						}
					}
					break;
				case Phase::kRolling:
					{
						_sim->Advance(dt);
						PlaySounds(_sim->TakeEvents());
						// timed, like a real machine: a moment after the ball reaches the pins (or the pit) it's called
						const auto& sb = _sim->GetBall();
						if (_reachedAt < 0.0 && (sb.pos.x > _sim->GetSpec().kickbackStart || sb.state == bowl::BallState::kPit ||
													sb.state == bowl::BallState::kTaken)) {
							_reachedAt = _sim->TimeSinceThrow();
						}
						// the last pin down: the X and the call straight away, while the pins are still flying
						if (!_called && _sim->StandingCount() == 0) {
							if (_allDownAt < 0.0) {
								_allDownAt = _sim->TimeSinceThrow();
							} else if (_sim->TimeSinceThrow() - _allDownAt >= kAllDownHold) {
								CallBall(_match.CardOf(_match.Bowler()).PinsUp() == 10, _sim->DownThisBall(), 0);
							}
						} else {
							_allDownAt = -1.0;
						}
						const bool due = _reachedAt >= 0.0 && _sim->TimeSinceThrow() - _reachedAt >= kCallAfter;
						if (_sim->Settled() || due) {
							if (!_sim->Settled()) {
								_sim->ForceSettle();
							}
							Audio::StopRoll();
							BallSettled();
						}
					}
					break;
				case Phase::kResult:
					// (the ball's still rolling about in the pit meanwhile; the pins have stopped. Only in the pit, where it
					// can't touch them: one still on the deck stays put)
					if (_sim->GetBall().state == bowl::BallState::kPit) {
						_sim->Advance(dt);
						PlaySounds(_sim->TakeEvents());
					}
					if (!_match.Over() && MachineOn()) {
						if (_phaseTime >= kMachineAfter) {
							StartSetting();
						}
					} else if (_phaseTime >= kResultSeconds) {
						NextBall();
					}
					break;
				case Phase::kSetting:
					SettingSounds(_phaseTime - dt, _phaseTime);
					if (_phaseTime >= SettingLength()) {
						NextBall(true);
					}
					break;
				case Phase::kOver:
					if (_phaseTime >= kOverSeconds) {
						EndGame("");
						return;
					}
					break;
				default:
					break;
				}
				if (_bannerTime > 0.0f) {
					_bannerTime = std::max(0.0f, _bannerTime - dt);
				}
				// (the free camera can be taken back by the game, a scene say: it's asked for again)
				if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && _inCamera) {
					auto* free = camera->cameraStates[RE::CameraStates::kFree].get();
					if (free && camera->currentState.get() != free && now - _camRetryAt > std::chrono::seconds(1)) {
						_camRetryAt = now;
						log::info("the free camera was taken away; asking for it again");
						{
							std::scoped_lock lock(_camMutex);
							_camMapped = false;
							_mapFrames = 0;
						}
						camera->ToggleFreeCameraMode(false);
					}
				}
			}
			if (_phase == Phase::kNone) {
				return;  // (the game ended this tick)
			}
			DrawPins();
			DrawMachine();
			DimLights();
			DrawBalls(paused ? 0.0f : dt);
			UpdateCameraGoal(paused ? 0.0f : dt);
			UpdateGuide();
		}

		// what the HUD shows
		HudState h;
		{
			std::scoped_lock lock(_hudMutex);
			h.prompt = _hud.prompt;
			h.promptSub = _hud.promptSub;
			h.promptVs = _hud.promptVs;
		}
		if (_phase != Phase::kNone) {
			const auto& s = Settings::Get();
			h.prompt.clear();
			h.promptSub.clear();
			h.game = true;
			h.lane = _lane ? _lane->number : 0;
			h.names = _names;
			for (int p = 0; p < _match.Players(); ++p) {
				h.cards.push_back(_match.CardOf(p));
			}
			h.bowler = _match.Bowler();
			h.aiming = _phase == Phase::kAiming || _phase == Phase::kPickup;
			h.hook = HookNow();
			h.maxHook = 100.0f;  // (the hook's a percentage of full)
			// 39 boards across the lane, counted from the right gutter
			const double half = s.spec.laneHalfWidth;
			h.board = std::clamp(static_cast<int>(std::floor((_releaseY + half) / (2.0 * half) * 39.0)) + 1, 1, 39);
			h.banner = _bannerTime > 0.0f ? _banner : std::string();
			h.bannerSub = _bannerSub;
			h.bannerAlpha = _bannerLength > 0.0f ? std::clamp(std::min(_bannerTime, _bannerLength - _bannerTime) * 4.0f, 0.0f, 1.0f) : 0.0f;
			h.bannerAge = std::max(0.0f, _bannerLength - _bannerTime);
			h.over = _phase == Phase::kOver;
			h.guide = _guide;
			if (AiTurn()) {
				h.keys = std::format("{} IS BOWLING   {} TWICE: QUIT", _names[static_cast<std::size_t>(_match.Bowler())], util::Upper(Settings::Key(s.keyQuit)));
			} else
			{
				h.keys = std::format("MOUSE: AIM   {}/{}: MOVE   HOLD LEFT MOUSE: PULL BACK FOR POWER, SIDEWAYS FOR SPIN   {} TWICE: QUIT",
					util::Upper(Settings::Key(s.keyLeft)), util::Upper(Settings::Key(s.keyRight)), util::Upper(Settings::Key(s.keyQuit)));
			}
		}
		std::scoped_lock lock(_hudMutex);
		_hud = std::move(h);
	}

	void Game::PollInput(float a_dt)
	{
		const auto& s = Settings::Get();
		const bool  focus = util::GameHasFocus();
		const std::array<int, 5> keys{ s.keyLeft, s.keyRight, s.keyQuit, s.keyFewer, s.keyMore };
		std::array<bool, 5>      down{};
		std::array<bool, 5>      pressed{};
		for (std::size_t i = 0; i < keys.size(); ++i) {
			down[i] = focus && util::KeyDown(keys[i]);
			pressed[i] = down[i] && !_keyWasDown[i];
			_keyWasDown[i] = down[i];
		}
		const bool activate = focus && util::KeyDown(_activateVk);
		const bool activatePressed = activate && !_activateWasDown;
		_activateWasDown = activate;
		const bool lmb = focus && util::KeyDown(VK_LBUTTON);
		const bool lmbPressed = lmb && !_lmbWasDown;
		const bool lmbReleased = !lmb && _lmbWasDown;
		_lmbWasDown = lmb;

		if (_phase == Phase::kNone) {
			// at a ball return: the arrows pick how many play (you against the people nearby), F starts
			if (_lookLane && !_modes.empty()) {
				if (pressed[3] || pressed[4]) {
					const int n = static_cast<int>(_modes.size());
					_mode = ((_mode + (pressed[4] ? 1 : -1)) % n + n) % n;
					_lookTime = {};  // (the prompt shows it at once)
				}
				if (activatePressed) {
					const auto& mode = _modes[static_cast<std::size_t>(std::clamp(_mode, 0, static_cast<int>(_modes.size()) - 1))];
					if (mode.missing > 0) {
						Say(std::format("Bowling: {} needs {} people nearby to play against", mode.label, mode.missing + mode.npcs.size()));
					} else {
						StartGame(*_lookLane, mode);
					}
				}
			}
			return;
		}

		// the quit key, twice
		if (pressed[2]) {
			const auto now = std::chrono::steady_clock::now();
			if (_quitArmed && now - _quitAt < std::chrono::duration<float>(kQuitWindow)) {
				EndGame("Bowling: game stopped");
				return;
			}
			_quitArmed = true;
			_quitAt = now;
			Say(std::format("Bowling: press {} again to stop the game", Settings::Key(s.keyQuit)));
		}

		if ((_phase != Phase::kAiming && _phase != Phase::kPickup) || AiTurn()) {
			_charging = false;
			return;
		}
		// along the foul line (held keys move smoothly)
		const double edge = s.spec.laneHalfWidth - s.spec.ballRadius;
		if (down[0] != down[1]) {
			_releaseY = std::clamp(_releaseY + (down[0] ? 1.0 : -1.0) * s.moveSpeed * a_dt, -edge, edge);
		}
		// the throw: hold the left button and pull back, let go
		if (_phase == Phase::kAiming) {
			if (lmbPressed) {
				_power = 0.0f;
				_swingHook = 0.0f;
				_charging = true;
			} else if (lmbReleased && _charging) {
				_charging = false;
				if (_power.load() < 0.02f) {
					Say("Bowling: pull the mouse back while you hold the button to put power on it");
				} else {
					Throw(_power.load(), false);
				}
			}
		}
	}

	void Game::UpdateLook()
	{
		// a few times a second: the activate key's binding, and whether you're looking at a ball return
		const auto now = std::chrono::steady_clock::now();
		if (now - _lookTime < std::chrono::milliseconds(150)) {
			return;
		}
		_lookTime = now;
		if (auto* map = RE::ControlMap::GetSingleton()) {
			// (keyboard bindings are kept as Windows virtual-key codes: F = 0x46)
			const auto vk = map->GetMappedKey("Activate", RE::INPUT_DEVICE::kKeyboard);
			if (vk != RE::ControlMap::kInvalid && vk > 0 && vk < 0xFF) {
				_activateVk = static_cast<int>(vk);
			}
		}
		const auto* was = _lookLane;
		_lookLane = util::GamePaused() ? nullptr : LookedAtLane();
		if (_lookLane) {
			BuildModes();
		}
		if (_lookLane && _lookLane != was && !Hud::Installed()) {
			// (no Menu Framework: the game's own notification says what F does here)
			Say(std::format("Bowling: {} at the ball return starts a game on lane {}", Settings::Key(_activateVk), _lookLane->number));
		}
		// while F is ours (at a ball return) the game's own use of it is off: no picking up the balls lying there
		BlockActivate(_lookLane != nullptr);
		std::scoped_lock lock(_hudMutex);
		if (_lookLane) {
			_hud.prompt = std::format("{}) START BOWLING", util::Upper(Settings::Key(_activateVk)));
			const Mode* mode = _modes.empty() ? nullptr : &_modes[static_cast<std::size_t>(_mode)];
			_hud.promptSub = std::format("<  {}  >     LANE {}", mode ? mode->label : std::string("SOLO"), _lookLane->number);
			_hud.promptVs.clear();
			if (mode && mode->missing > 0) {
				const int found = static_cast<int>(mode->npcs.size());
				_hud.promptVs = found == 0 ? std::string("NO ONE NEARBY TO PLAY") : std::format("ONLY {} NEARBY TO PLAY", found);
			} else if (mode && !mode->names.empty()) {
				_hud.promptVs = "VS ";
				for (std::size_t i = 0; i < mode->names.size(); ++i) {
					_hud.promptVs += (i ? ", " : "") + mode->names[i];
				}
			}
		} else {
			_hud.prompt.clear();
			_hud.promptSub.clear();
			_hud.promptVs.clear();
		}
	}

	const lanes::Lane* Game::LookedAtLane() const
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell || cell->GetFormID() != lanes::kCell || !camera || !camera->cameraRoot || player->IsInCombat()) {
			return nullptr;
		}
		const auto&        s = Settings::Get();
		const auto         origin = camera->cameraRoot->GetWorldTranslate();
		const float        pitch = player->data.angle.x;
		const float        yaw = player->data.angle.z;
		const RE::NiPoint3 dir{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
		const lanes::Lane* best = nullptr;
		float              bestT = std::numeric_limits<float>::max();
		for (const auto& lane : lanes::kLanes) {
			auto* ret = Ref(lane.ballReturn);
			if (!ret || ret->IsDisabled() || ret->GetParentCell() != cell) {
				continue;
			}
			if ((ret->data.location - player->data.location).Length() > s.activateDistance + 150.0f) {
				continue;
			}
			// the return as a segment along its length, at the height of its track; the view line has to pass
			// close to it, within reach
			const float        a = ret->data.angle.z;
			const RE::NiPoint3 fwd{ std::sin(a), std::cos(a), 0.0f };
			const RE::NiPoint3 p0 = ret->data.location + fwd * -125.0f + RE::NiPoint3{ 0.0f, 0.0f, 45.0f };
			const RE::NiPoint3 p1 = ret->data.location + fwd * 155.0f + RE::NiPoint3{ 0.0f, 0.0f, 45.0f };
			// closest approach between the ray origin + t * dir and the segment p0 + u * (p1 - p0)
			const RE::NiPoint3 seg = p1 - p0;
			const RE::NiPoint3 w = origin - p0;
			const float        b = dir.Dot(seg);
			const float        c = seg.Dot(seg);
			const float        d = dir.Dot(w);
			const float        e = seg.Dot(w);
			const float        denom = c - b * b;  // (dir is a unit vector)
			float              u = denom > 1e-4f ? std::clamp((e - b * d) / denom, 0.0f, 1.0f) : 0.0f;
			float              t = std::max(0.0f, b * u - d);
			u = std::clamp((t * b + e) / c, 0.0f, 1.0f);
			t = std::max(0.0f, b * u - d);
			const RE::NiPoint3 onRay = origin + dir * t;
			const RE::NiPoint3 onSeg = p0 + seg * u;
			if ((onRay - onSeg).Length() < 40.0f && t < s.activateDistance && t < bestT) {
				best = &lane;
				bestT = t;
			}
		}
		return best;
	}

	bool Game::StillHere() const
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		return cell && cell->GetFormID() == lanes::kCell && !player->IsDead(false) && !player->IsInCombat();
	}

	void Game::Say(const std::string& a_message)
	{
		log::info("{}", a_message);
		RE::SendHUDMessage::ShowHUDMessage(a_message.c_str(), nullptr, false, false);
	}

	void Game::Banner(const std::string& a_title, const std::string& a_sub, float a_seconds)
	{
		_banner = a_title;
		_bannerSub = a_sub;
		_bannerTime = a_seconds;
		_bannerLength = a_seconds;
		log::info("{}  {}", a_title, a_sub);
		if (Settings::Get().notifications || !Hud::Installed()) {
			RE::SendHUDMessage::ShowHUDMessage(a_sub.empty() ? a_title.c_str() : std::format("{} {}", a_title, a_sub).c_str(), nullptr, false, false);
		}
	}

	// ---------------------------------------------------------------- a game

	void Game::StartGame(const lanes::Lane& a_lane, const Mode& a_mode)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* pinBase = Base(lanes::kPinBase);
		auto* ballBase = Base(lanes::kBallBase);
		if (!player || !pinBase || !ballBase || !Ref(a_lane.ballReturn)) {
			Say("Bowling: the lane isn't ready");
			return;
		}
		if (player->IsInCombat()) {
			Say("Bowling: not in the middle of a fight");
			return;
		}
		auto& s = Settings::Get();
		s.Load();
		_lane = &a_lane;
		BlockActivate(false);
		{
			std::scoped_lock lock(_hudMutex);
			_hud.prompt.clear();
			_hud.promptSub.clear();
			_hud.promptVs.clear();
		}

		// who's bowling: you (by name), then the people you're playing; each bowls their own way, the same every game
		_bowlers.clear();
		Bowler me;
		me.name = PlayerName();
		_bowlers.push_back(me);
		const auto addAi = [&](const std::string& a_name, std::uint32_t a_id, RE::ObjectRefHandle a_actor) {
			const std::uint32_t seed = a_id * 2654435761u;
			Bowler b;
			b.name = a_name;
			b.ai = true;
			b.skill = 0.35f + static_cast<float>((seed >> 8) % 45) / 100.0f;
			b.leftHanded = (seed >> 20) % 8 == 0;
			b.actor = a_actor;
			_bowlers.push_back(b);
			log::info("{} ({:08X}) bowls: skill {:.2f}, {}-handed", b.name, a_id, b.skill, b.leftHanded ? "left" : "right");
		};
		for (const auto& h : a_mode.npcs) {
			auto  ref = h.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (actor && !actor->IsDead(false) && !actor->IsDisabled() && _bowlers.size() < 4) {
				addAi(NameOf(actor), actor->GetFormID(), h);
			}
		}
		HoldOpponents(true);
		const int players = static_cast<int>(_bowlers.size());
		_names.clear();
		for (const auto& b : _bowlers) {
			_names.push_back(b.name);
		}
		{
			auto spec = s.spec;
			spec.returnSide = s.gutterReturn ? ReturnSide(a_lane) : 0.0;  // (off the back wall, the ball heads for the return's side)
			_sim = std::make_unique<bowl::Sim>(spec);
		}
		_sim->Rack(s.pinJitter, _rng());
		_match = bowl::Match(players);
		_strikeRun.fill(0);
		_aiPlanned = false;

		// the lane's own pins (and any balls on its return) out of the way; ours in their place
		for (int l = 0; l < static_cast<int>(lanes::kLanes.size()); ++l) {
			if (&lanes::kLanes[static_cast<std::size_t>(l)] == &a_lane) {
				RemoveDisplay(l);  // (the game's own two balls take their place)
			}
		}
		HideVanilla(a_lane);
		SpawnMachine(a_lane);
		for (int i = 0; i < bowl::Sim::kPins; ++i) {
			const auto spot = _sim->Spot(i);
			_pins[static_cast<std::size_t>(i)] = Spawn(pinBase, lanes::ToWorld(a_lane, spot.x, spot.y, 0.0));
			_pinPrepared[static_cast<std::size_t>(i)] = false;
		}
		for (int b = 0; b < 2; ++b) {
			auto& ball = _balls[static_cast<std::size_t>(b)];
			ball = {};
			ball.at = BallAt::kReturn;
			ball.along = ball.target = b == 0 ? s.returnSpot1 : s.returnSpot2;
			ball.pos = ReturnPoint(ball.along, s.returnZ);
			ball.ref = Spawn(ballBase, ball.pos);
		}

		_releaseY = 0.0;
		_hook = 0.0f;
		_aimAngle = 0.0f;
		_aimYaw = lanes::kDownLaneYaw;
		_quitArmed = false;
		_charging = false;
		_phase = Phase::kPickup;
		_phaseTime = 0.0f;
		_active = 0;
		_balls[0].at = BallAt::kHand;
		_balls[1].target = s.returnSpot1;  // the other one rolls up behind it
		EnterCamera();
		BlockControls(true);
		log::info("game on lane {}: {}", a_lane.number, a_mode.label);
	}

	void Game::HoldOpponents(bool a_hold)
	{
		// the people you're playing stay put for the game (restrained: they stand where they are, still turning and
		// idling), and are let go after. Only those we held are let go
		const auto run = [](RE::TESObjectREFR* a_ref, std::string_view a_command) {
			auto* factory = RE::ConcreteFormFactory<RE::Script>::GetFormFactory();
			auto* script = factory ? factory->Create() : nullptr;
			if (!script) {
				return;
			}
			RE::ScriptCompiler compiler;
			script->SetText(a_command);
			script->CompileAndRun(&compiler, RE::COMPILER_NAME::kSystemWindow, a_ref);
			delete script;
		};
		if (a_hold) {
			_held.clear();
			for (const auto& b : _bowlers) {
				auto  ref = b.actor.get();
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (actor && !actor->IsDead(false)) {
					run(actor, "SetRestrained 1");
					_held.push_back(b.actor);
				}
			}
		} else {
			for (const auto& h : _held) {
				if (auto ref = h.get()) {
					run(ref.get(), "SetRestrained 0");
				}
			}
			_held.clear();
		}
	}

	void Game::EndGame(const std::string& a_why)
	{
		if (_phase == Phase::kNone) {
			return;
		}
		auto remove = [](RE::ObjectRefHandle& a_handle) {
			if (auto ref = a_handle.get()) {
				ref->Disable();
				ref->SetWantsDelete(true);
			}
			a_handle = {};
		};
		for (auto& h : _pins) {
			remove(h);
		}
		for (auto& b : _balls) {
			remove(b.ref);
		}
		RestoreVanilla();
		RemoveMachine();
		HoldOpponents(false);
		Audio::StopRoll();
		_charging = false;
		BlockControls(false);
		LeaveCamera();
		_phase = Phase::kNone;
		_phaseTime = 0.0f;
		_sim.reset();
		_lane = nullptr;
		_bannerTime = 0.0f;
		_lookTime = {};
		_activateWasDown = true;  // (a held F doesn't start another game at once)
		if (!a_why.empty()) {
			Say(a_why);
		}
		log::info("game over");
	}

	void Game::Throw(float a_power, bool a_ai)
	{
		if (_phase != Phase::kAiming || !_sim) {
			return;
		}
		const auto&   s = Settings::Get();
		const float   power = std::clamp(a_power, 0.0f, 1.0f);
		bowl::Release r;
		float         hook = HookNow();
		if (a_ai) {
			r = _aiPlan.release;  // (their own pace and spin, worked out for the line)
			hook = s.maxHook > 0.0f ? static_cast<float>(r.axis * 180.0 / kPi / s.maxHook * 100.0) : 0.0f;
		} else {
			r.y = _releaseY;
			r.angle = _aimAngle;
			r.speed = s.minSpeed + (s.maxSpeed - s.minSpeed) * std::pow(power, 1.3f);
			r.revs = s.revs;
			r.axis = hook / 100.0 * s.maxHook * kPi / 180.0;
		}
		// nobody throws exactly what they meant to: a little off in line, pace, spin and where it leaves the hand,
		// and more of it the harder the throw (and a companion's misses depend on how good they are)
		if (s.inaccuracy > 0.0f) {
			std::normal_distribution<double> n(0.0, 1.0);
			const float  skill = a_ai ? _bowlers[static_cast<std::size_t>(_match.Bowler())].skill : 0.5f;
			const double k = s.inaccuracy * (0.5 + power) * (a_ai ? (1.75 - 1.5 * skill) : 1.0);
			r.angle += n(_rng) * 0.08 * kPi / 180.0 * k;
			r.speed *= 1.0 + n(_rng) * 0.008 * k;
			r.revs *= 1.0 + n(_rng) * 0.03 * k;
			r.axis += n(_rng) * 1.0 * kPi / 180.0 * k;
			r.y += n(_rng) * 0.25 * k;
		}
		// and the lane's never quite the same twice: a little drift left or right on the way down
		if (s.laneDrift > 0.0f) {
			std::normal_distribution<double>       n(0.0, 1.0);
			std::uniform_real_distribution<double> phase(0.0, 2.0 * kPi);
			r.drift = n(_rng) * 6.0 * s.laneDrift;
			r.driftWave = n(_rng) * 12.0 * s.laneDrift;
			r.driftPhase = phase(_rng);
		}
		if (!a_ai) {
			_lastPower = power;
			auto& me = _bowlers[static_cast<std::size_t>(_match.Bowler())];
			me.releaseY = _releaseY;
			me.hook = _hook;
			me.aim = _aimAngle;
		}
		_aiPlanned = false;
		_rackFullAtThrow = _sim->StandingCount() >= 6;
		_impactAt = -1.0;
		_reachedAt = -1.0;
		_allDownAt = -1.0;
		_called = false;
		_impactDone = false;
		_lastPinSound = -1.0;
		_sim->Throw(r);
		Audio::Play(Audio::Bank::kRelease, HeldPoint(), 0.35f + 0.65f * power);
		_balls[static_cast<std::size_t>(_active)].at = BallAt::kLane;
		_lastBallGutter = false;
		_phase = Phase::kRolling;
		_phaseTime = 0.0f;
		_quitArmed = false;
		log::info("throw ({}): from {:.1f} angle {:.2f} deg, power {:.0f}% = {:.0f} u/s, hook {:.0f}% (axis {:.1f} deg, {:.0f} rad/s), drift {:.1f}/{:.1f}",
			_names[static_cast<std::size_t>(_match.Bowler())], r.y, r.angle * 180.0 / kPi, power * 100.0f, r.speed, hook, r.axis * 180.0 / kPi, r.revs,
			r.drift, r.driftWave);
		_swingHook = 0.0f;
	}

	void Game::CallBall(bool a_freshRack, int a_down, int a_standing)
	{
		if (_called) {
			return;
		}
		_called = true;
		const int  bowler = _match.Bowler();
		const bool strike = a_freshRack && a_standing == 0;
		// a strike puts up a big X
		if (strike) {
			Banner("X", _names.size() > 1 ? _names[static_cast<std::size_t>(bowler)] : std::string(), 2.2f);
		}
		// and the PA calls it
		auto& run = _strikeRun[static_cast<std::size_t>(bowler) % _strikeRun.size()];
		run = strike ? run + 1 : 0;
		if (run > 0) {
			Audio::Announce(run == 3 ? Audio::Call::kTurkey : Audio::Call::kStrike);
		} else if (!a_freshRack && a_standing == 0) {
			Audio::Announce(Audio::Call::kSpare);
		} else if (a_down == 0 && _lastBallGutter) {
			Audio::Announce(Audio::Call::kGutter);
		}
	}

	void Game::BallSettled()
	{
		auto&      card = _match.CardOf(_match.Bowler());
		const int  bowler = _match.Bowler();
		const bool freshRack = card.PinsUp() == 10;
		const int  down = _sim->DownThisBall();
		const int  standing = _sim->StandingCount();
		const int  frame = card.Frame();
		const std::string split = freshRack && down < 10 ? SplitName() : std::string();
		_match.Add(down);

		std::string title;
		if (freshRack && down == 10) {
			title = "STRIKE!";
		} else if (!freshRack && standing == 0) {
			title = "SPARE!";
		} else if (down == 0) {
			title = _lastBallGutter ? "GUTTER BALL" : "MISS";
		} else if (!split.empty()) {
			title = split;
		} else {
			title = std::format("{} PIN{}", down, down == 1 ? "" : "S");
		}
		std::string sub = _names.size() > 1 ? _names[static_cast<std::size_t>(bowler)] + "  ·  " : std::string();
		sub += std::format("FRAME {}", frame + 1);
		if (const int total = _match.CardOf(bowler).Total(); total > 0 || card.Frame() > 0) {
			sub += std::format("  ·  {}", total);
		}
		CallBall(freshRack, down, standing);  // (unless the last pin going down already called it)
		// (the score sheet at the top says it all: the call only goes up as a notification without the HUD, or if asked for)
		log::info("{}  {}", title, sub);
		if (Settings::Get().notifications || !Hud::Installed()) {
			RE::SendHUDMessage::ShowHUDMessage(std::format("{} {}", title, sub).c_str(), nullptr, false, false);
		}
		log::info("ball: {} down ({} standing: {}){}", down, standing, PinList(_sim->StandingMask()), split.empty() ? "" : " " + split);

		// the ball goes round the machine and comes back up the return
		// a ball in the pit goes back up the gutter on the return's side and into the return; one that stopped short
		// stays where it is until the sweep takes it
		auto&       ball = _balls[static_cast<std::size_t>(_active)];
		const auto& sb = _sim->GetBall();
		if (Settings::Get().gutterReturn && sb.pos.x > _sim->GetSpec().deckEnd - 2.0 * _sim->GetSpec().ballRadius) {
			ball.at = BallAt::kGutter;
			ball.runX = sb.pos.x;
			ball.runY = sb.pos.y;
			ball.runZ = sb.z;
			_ballLingers = false;
		} else {
			ball.at = BallAt::kGone;
			ball.hidden = kReturnDelay;
			_ballLingers = true;
		}
		_phase = Phase::kResult;
		_phaseTime = 0.0f;
	}

	void Game::NextBall(bool a_racked)
	{
		_ballLingers = false;  // (the sweep has had the thrown ball by now)
		const auto& s = Settings::Get();
		if (_match.Over()) {
			_phase = Phase::kOver;
			_phaseTime = 0.0f;
			const int winner = _match.Winner();
			if (_match.Players() == 1) {
				Banner("GAME OVER", std::format("FINAL SCORE {}", _match.CardOf(0).Total()), kOverSeconds);
			} else if (winner < 0) {
				Banner("GAME OVER", "A TIE!", kOverSeconds);
			} else {
				Banner("GAME OVER", std::format("{} WINS WITH {}", _names[static_cast<std::size_t>(winner)], _match.CardOf(winner).Total()), kOverSeconds);
			}
			return;
		}
		const auto& card = _match.CardOf(_match.Bowler());
		if (a_racked) {
			// (the pinsetter's done it)
		} else if (card.FreshRack()) {
			_sim->Rack(Settings::Get().pinJitter, _rng());
		} else {
			_sim->Sweep();
		}
		// the ball at the front of the return is the one picked up; the other rolls up behind it
		int pick = -1;
		for (int b = 0; b < 2; ++b) {
			const auto& ball = _balls[static_cast<std::size_t>(b)];
			if (ball.at == BallAt::kReturn && (pick < 0 || ball.target < _balls[static_cast<std::size_t>(pick)].target)) {
				pick = b;
			}
		}
		if (pick < 0) {
			pick = 1 - _active;  // (both somehow away: take the other one out of the machine)
		}
		_active = pick;
		// every ball starts in the middle, aimed straight down the lane (an NPC's too: they walk it over themselves)
		_releaseY = 0.0;
		_hook = 0.0f;
		_aimAngle = 0.0f;
		_aiPlanned = false;
		auto& ball = _balls[static_cast<std::size_t>(_active)];
		ball.at = BallAt::kHand;
		auto& other = _balls[static_cast<std::size_t>(1 - _active)];
		if (other.at == BallAt::kReturn) {
			other.target = s.returnSpot1;
		}
		_phase = Phase::kPickup;
		_phaseTime = 0.0f;
	}

	bool Game::AiTurn() const
	{
		const int b = _match.Bowler();
		return _phase != Phase::kNone && b >= 0 && b < static_cast<int>(_bowlers.size()) && _bowlers[static_cast<std::size_t>(b)].ai;
	}

	void Game::PlanAiThrow()
	{
		const auto&      s = Settings::Get();
		const auto&      who = _bowlers[static_cast<std::size_t>(_match.Bowler())];
		bowl::ThrowLimits limits;
		limits.minSpeed = s.minSpeed;
		limits.maxSpeed = s.maxSpeed;
		limits.revs = s.revs;
		limits.maxAxis = s.maxHook * kPi / 180.0;
		limits.maxAngle = s.maxAim * kPi / 180.0;
		_aiPlan = bowl::PlanThrow(_sim->GetSpec(), _sim->Pins(), who.skill, who.leftHanded, limits);
		_aiPlanned = true;
		_phaseTime = 0.0f;
		// they start in the middle and walk over to where they'll throw from (the tick moves them)
		_releaseY = 0.0;
		_aiThereAt = -1.0f;
		_aimAngle = static_cast<float>(_aiPlan.release.angle);
		_hook = s.maxHook > 0.0f ? static_cast<float>(_aiPlan.release.axis * 180.0 / kPi / s.maxHook * 100.0) : 0.0f;
		_swingHook = 0.0f;
		log::info("{} lines up: from {:.1f} at ({:.0f}, {:.1f}), power {:.0f}%", who.name, _aiPlan.release.y, _aiPlan.target.x, _aiPlan.target.y,
			_aiPlan.power * 100.0);
	}

	std::string Game::PlayerName()
	{
		auto*       player = RE::PlayerCharacter::GetSingleton();
		const char* own = player ? player->GetDisplayFullName() : nullptr;
		return own && *own ? util::Upper(own) : std::string("PLAYER 1");
	}

	std::string Game::NameOf(RE::Actor* a_actor)
	{
		const char* name = a_actor ? a_actor->GetDisplayFullName() : nullptr;
		std::string n = name && *name ? util::Upper(name) : std::string("COMPANION");
		if (n.size() > 14) {
			n = n.substr(0, 13) + ".";
		}
		return n;
	}

	void Game::BuildModes()
	{
		// solo, or you against 1 to 3 others: your companions first, then the nearest friendly people (and robots)
		// around the lanes. A mode with too few people nearby is shown but can't start
		static auto* companionFaction = RE::TESForm::GetFormByID<RE::TESFaction>(0x00023C01);  // CurrentCompanionFaction
		static auto* npcType = RE::TESForm::GetFormByID<RE::BGSKeyword>(0x00013794);           // ActorTypeNPC
		static auto* robotType = RE::TESForm::GetFormByID<RE::BGSKeyword>(0x0002CB73);         // ActorTypeRobot
		auto*        player = RE::PlayerCharacter::GetSingleton();
		auto*        cell = player ? player->GetParentCell() : nullptr;
		struct Near
		{
			RE::Actor* actor;
			bool       companion;
			float      distance;
		};
		std::vector<Near> nearby;
		if (cell) {
			const auto at = player->data.location;
			cell->ForEachReferenceInRange(at, kOpponentRange, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref && a_ref != player ? a_ref->As<RE::Actor>() : nullptr;
				if (!actor || actor->IsDisabled() || actor->IsDead(false) || actor->IsChild() || actor->IsInCombat() ||
					actor->GetHostileToActor(player)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				// people and robots bowl; a dog doesn't
				const bool person = (npcType && actor->HasKeywordHelper(npcType, nullptr)) || (robotType && actor->HasKeywordHelper(robotType, nullptr));
				const char* name = actor->GetDisplayFullName();
				if (person && name && *name) {
					const auto d = actor->data.location - at;
					nearby.push_back({ actor, companionFaction && actor->IsInFaction(companionFaction), std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) });
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		std::stable_sort(nearby.begin(), nearby.end(), [](const Near& a_l, const Near& a_r) {
			return a_l.companion != a_r.companion ? a_l.companion : a_l.distance < a_r.distance;
		});

		std::vector<Mode> modes;
		for (int players = 1; players <= 4; ++players) {
			Mode m;
			m.label = players == 1 ? std::string("SOLO") : std::format("{} PLAYERS", players);
			for (int o = 0; o < players - 1; ++o) {
				if (o < static_cast<int>(nearby.size())) {
					m.npcs.push_back(nearby[static_cast<std::size_t>(o)].actor->GetHandle());
					m.names.push_back(NameOf(nearby[static_cast<std::size_t>(o)].actor));
				} else {
					++m.missing;
				}
			}
			modes.push_back(std::move(m));
		}
		_mode = std::clamp(_mode, 0, static_cast<int>(modes.size()) - 1);
		_modes = std::move(modes);
	}

	void Game::PlaySounds(const std::vector<bowl::Event>& a_events)
	{
		// what the physics did this tick, as sounds where it happened
		const auto& b = _sim->GetBall();
		const bool  rolling = b.state == bowl::BallState::kLane || b.state == bowl::BallState::kGutter;
		if (rolling) {
			const float speed = static_cast<float>(b.vel.Length());
			const float k = std::clamp(speed / 800.0f, 0.0f, 1.0f);
			const bool  gutter = b.state == bowl::BallState::kGutter;
			Audio::Roll(lanes::ToWorld(*_lane, b.pos.x, b.pos.y, b.z), (0.25f + 0.75f * k) * (gutter ? 0.7f : 1.0f), (0.8f + 0.35f * k) * (gutter ? 0.85f : 1.0f));
		} else {
			Audio::StopRoll();
		}
		// a moment after the ball meets the pins: a lot of them going is the crash, a few the lighter hit
		if (_impactAt >= 0.0 && !_impactDone && _sim->TimeSinceThrow() >= _impactAt + kImpactLook) {
			_impactDone = true;
			int going = 0;
			for (const auto& p : _sim->Pins()) {
				going += (p.state != bowl::PinState::kSwept && !p.asleep && p.vel.Length() > 25.0) ? 1 : 0;
			}
			if (going >= kCrashPins) {
				Audio::Play(Audio::Bank::kCrash, _impactPos, std::min(1.0f, 0.7f + 0.04f * static_cast<float>(going)));
			} else if (going > 0) {
				Audio::Play(Audio::Bank::kHit, _impactPos, std::min(1.0f, 0.65f + 0.1f * static_cast<float>(going)));
			}
			log::info("the ball set {} pin{} going: {}", going, going == 1 ? "" : "s", going >= kCrashPins ? "crash" : "hit");
		}
		int pins = 0;  // (a few clatters a tick is plenty)
		for (const auto& e : a_events) {
			const auto  at = lanes::ToWorld(*_lane, e.pos.x, e.pos.y, 12.0);
			const float hard = static_cast<float>(e.strength);
			switch (e.kind) {
			case bowl::Event::Kind::kBallGutter:
				_lastBallGutter = true;
				break;
			case bowl::Event::Kind::kBallPin:
				// the ball into the rack: the crack of it now; the crash or the hit once it's clear how many pins it's
				// set going (below)
				if (_impactAt < 0.0) {
					_impactAt = _sim->TimeSinceThrow();
					_impactPos = at;
					Audio::Play(Audio::Bank::kPin, at, std::min(0.8f, 0.4f + hard / 1500.0f), 0.95f);
				}
				break;
			case bowl::Event::Kind::kPinPin:
			case bowl::Event::Kind::kPinDeck:
			case bowl::Event::Kind::kPinWall:
				// (a few a tick, at least a hundredth and a half of a second apart: a clatter, not a buzz)
				if (hard > 50.0f && pins < 3 && _sim->TimeSinceThrow() - _lastPinSound > 0.015) {
					++pins;
					_lastPinSound = _sim->TimeSinceThrow();
					const float pitch = e.kind == bowl::Event::Kind::kPinPin ? 1.05f : e.kind == bowl::Event::Kind::kPinDeck ? 0.9f : 0.85f;
					Audio::Play(Audio::Bank::kPin, at, std::min(0.8f, 0.15f + hard / 600.0f), pitch);
				}
				break;
			case bowl::Event::Kind::kPinPit:
				Audio::Play(Audio::Bank::kPin, at, 0.2f, 0.75f);
				break;
			case bowl::Event::Kind::kBallPit:
				Audio::Play(Audio::Bank::kPin, at, 0.35f, 0.6f);  // (a dull thump into the back of the machine)
				break;
			}
		}
	}

	void Game::UpdateGuide()
	{
		// the green line: from the ball, straight along your aim, as screen points (0..1). It shows the line you're
		// starting the ball on, not where the hook takes it
		_guide.clear();
		const auto& s = Settings::Get();
		auto*       camera = RE::Main::WorldRootCamera();
		if (!s.guide || _phase != Phase::kAiming || AiTurn() || !_lane || !_sim || !camera) {  // (an NPC's line is theirs to know)
			return;
		}
		const auto&  spec = _sim->GetSpec();
		const double dx = std::cos(_aimAngle);
		const double dy = std::sin(_aimAngle);
		// the ball on the screen: its middle and how big it looks. The line is drawn over everything, so the part of it
		// the ball would hide (the lane just in front of it, seen from behind) is left out: it starts past the ball
		const auto project = [&](const RE::NiPoint3& a_world, float& a_x, float& a_y) {
			float z = 0.0f;
			if (!camera->WorldPtToScreenPt3(a_world, a_x, a_y, z, 1e-5f) || z <= 0.0f) {
				return false;
			}
			if (s.guideFlipY) {
				a_y = 1.0f - a_y;
			}
			return true;
		};
		const auto centre = HeldPoint();
		float      bx = 0.0f, by = 0.0f, tx = 0.0f, ty = 0.0f;
		float      ballR = 0.0f;
		if (project(centre, bx, by) && project(centre + RE::NiPoint3{ 0.0f, 0.0f, static_cast<float>(spec.ballRadius) }, tx, ty)) {
			ballR = std::hypot(tx - bx, ty - by) * 1.08f;  // (a touch more: the screen's wider than tall, and its edge)
		}
		// (screen x and y both run 0..1, over a wider span across than down: x is scaled to y's terms)
		const auto& fr = camera->viewFrustum;
		const float aspect = std::abs(fr.top - fr.bottom) > 1e-6f ? std::abs((fr.right - fr.left) / (fr.top - fr.bottom)) : 16.0f / 9.0f;
		const auto  onBall = [&](float a_x, float a_y) {
			return ballR > 0.0f && std::hypot((a_x - bx) * aspect, a_y - by) < ballR;
		};
		constexpr double kFine = 2.0;  // (finely near the ball, to start right at its edge)
		double           t = 0.0;
		bool             started = false;
		while (t <= s.guideLength) {
			const double x = spec.ballRadius + 2.0 + t * dx;
			const double y = _releaseY + t * dy;
			if (std::abs(y) > spec.laneHalfWidth) {
				break;  // (off the edge: the gutter)
			}
			float sx = 0.0f, sy = 0.0f;
			if (project(lanes::ToWorld(*_lane, x, y, 0.6), sx, sy) && (started || !onBall(sx, sy))) {
				started = true;
				_guide.push_back({ sx, sy });
			}
			t += started ? std::max(kFine, s.guideLength / 24.0) : kFine;
		}
	}

	std::string Game::SplitName() const
	{
		// the head pin down, and the pins still standing in two or more groups with nothing between them
		const auto standing = _sim->StandingMask();
		if ((standing & 1u) || standing == 0) {
			return {};
		}
		const double touching = _sim->GetSpec().pinSpacing * 1.05;  // (not "near": a Windows macro)
		std::uint32_t seen = 0;
		int           groups = 0;
		for (int i = 0; i < bowl::Sim::kPins; ++i) {
			if (!(standing & (1u << i)) || (seen & (1u << i))) {
				continue;
			}
			++groups;
			std::vector<int> stack{ i };
			seen |= 1u << i;
			while (!stack.empty()) {
				const int a = stack.back();
				stack.pop_back();
				for (int b = 0; b < bowl::Sim::kPins; ++b) {
					if ((standing & (1u << b)) && !(seen & (1u << b)) && (_sim->Spot(a) - _sim->Spot(b)).Length() <= touching) {
						seen |= 1u << b;
						stack.push_back(b);
					}
				}
			}
		}
		if (groups < 2) {
			return {};
		}
		const int count = std::popcount(standing);
		return count <= 4 ? PinList(standing) + " SPLIT" : std::string("SPLIT");
	}

	// ---------------------------------------------------------------- the world

	RE::ObjectRefHandle Game::Spawn(RE::TESBoundObject* a_form, const RE::NiPoint3& a_pos) const
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!dh || !cell || !a_form) {
			return {};
		}
		RE::NEW_REFR_DATA data;
		data.location = a_pos;
		data.direction = { 0.0f, 0.0f, 0.0f };
		data.object = a_form;
		if (cell->IsInterior()) {
			data.interior = cell;
		} else {
			data.world = cell->worldSpace;
		}
		data.initializeScripts = false;
		return dh->CreateReferenceAtLocation(data);
	}

	void Game::Prepare(RE::TESObjectREFR* a_ref, bool& a_done)
	{
		// our pins and balls are only ever moved by us: their Havok off, and keyframed in case anything turns it on
		auto* root = a_ref ? a_ref->Get3D() : nullptr;
		if (a_done || !root) {
			return;
		}
		RE::bhkWorld::EnableCollision(root, false, true, true);
		RE::BSVisit::TraverseScenegraphCollision(root, [](RE::bhkNPCollisionObject* a_collision) {
			a_collision->SetMotionType(RE::hknpMotionPropertiesId::Preset::kKeyframed);
			return RE::BSVisitControl::kContinue;
		});
		a_done = true;
	}

	void Game::PlaceNode(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, bool a_hidden, float a_scale)
	{
		auto* root = a_ref ? a_ref->Get3D() : nullptr;
		if (!root) {
			return;
		}
		if (root->GetAppCulled() != a_hidden) {
			root->SetAppCulled(a_hidden);
		}
		if (a_hidden) {
			return;
		}
		// we know the world transform we want; the node keeps it relative to its parent
		RE::NiTransform local;
		if (auto* parent = root->parent) {
			const auto& p = parent->GetWorldTransform();
			const float ps = p.scale != 0.0f ? p.scale : 1.0f;
			local.rotate = a_rot * p.rotate.Transpose();
			local.translate = (p.rotate * (a_pos - p.translate)) * (1.0f / ps);
			local.scale = a_scale / ps;
		} else {
			local.rotate = a_rot;
			local.translate = a_pos;
			local.scale = a_scale;
		}
		root->SetLocalTransform(local);
		RE::NiUpdateData update;
		root->Update(update);
		a_ref->data.location.x = a_pos.x;  // (NiPoint3A: no assignment from a plain NiPoint3)
		a_ref->data.location.y = a_pos.y;
		a_ref->data.location.z = a_pos.z;
	}

	void Game::HideVanilla(const lanes::Lane& a_lane)
	{
		RestoreVanilla();
		auto hide = [this](RE::TESObjectREFR* a_ref) {
			if (a_ref && !a_ref->IsDisabled() && !a_ref->IsDeleted()) {
				a_ref->Disable();
				_hiddenVanilla.push_back(a_ref->GetHandle());
			}
		};
		for (const auto id : a_lane.pins) {
			hide(Ref(id));
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell) {
			return;
		}
		const auto& s = Settings::Get();
		// any other pin lying on this deck, and any ball sitting on this lane's return
		const auto deck = lanes::ToWorld(a_lane, s.spec.headPin + 50.0, 0.0, 0.0);
		const auto ret = ReturnPoint(10.0f, s.returnZ);
		std::vector<RE::TESObjectREFR*> found;
		cell->ForEachReferenceInRange(deck, 170.0f, [&](RE::TESObjectREFR* a_ref) {
			auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
			if (base && base->GetFormID() == lanes::kPinBase) {
				found.push_back(a_ref);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		cell->ForEachReferenceInRange(ret, 170.0f, [&](RE::TESObjectREFR* a_ref) {
			auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
			if (base && base->GetFormID() == lanes::kBallBase && std::abs(a_ref->data.location.z - ret.z) < 25.0f) {
				found.push_back(a_ref);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		for (auto* r : found) {
			hide(r);
		}
		log::info("hid {} of the lane's own pins and balls", _hiddenVanilla.size());
	}

	void Game::RestoreVanilla()
	{
		for (auto& h : _hiddenVanilla) {
			if (auto ref = h.get(); ref && ref->IsDisabled()) {
				ref->Enable(false);
			}
		}
		_hiddenVanilla.clear();
	}

	RE::NiPoint3 Game::ReturnPoint(float a_along, float a_height) const
	{
		return _lane ? ReturnPointOf(*_lane, a_along, a_height) : RE::NiPoint3{};
	}

	RE::NiPoint3 Game::ReturnPointOf(const lanes::Lane& a_lane, float a_along, float a_height)
	{
		// the return's own frame: y runs along it (towards the hood, by the lane), z up from its foot
		auto* ret = Ref(a_lane.ballReturn);
		if (!ret) {
			return {};
		}
		const float a = ret->data.angle.z;
		return ret->data.location + RE::NiPoint3{ std::sin(a), std::cos(a), 0.0f } * a_along + RE::NiPoint3{ 0.0f, 0.0f, a_height };
	}

	double Game::ReturnSide(const lanes::Lane& a_lane)
	{
		// (lane 3's return is on its right, lane 6's on its left: worked out from where they stand)
		auto* ret = Ref(a_lane.ballReturn);
		if (!ret) {
			return 0.0;
		}
		double x = 0.0, y = 0.0, z = 0.0;
		lanes::ToLane(a_lane, ret->data.location, x, y, z);
		return y >= 0.0 ? 1.0 : -1.0;
	}

	void Game::UpdateDisplay()
	{
		// two balls on each lane's return while you're in the alley and no game's on that lane
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell || cell->GetFormID() != lanes::kCell) {
			RemoveDisplay(-1);
			return;
		}
		auto* ballBase = Base(lanes::kBallBase);
		const auto& s = Settings::Get();
		for (int l = 0; l < static_cast<int>(lanes::kLanes.size()); ++l) {
			const auto& lane = lanes::kLanes[static_cast<std::size_t>(l)];
			auto&       d = _display[static_cast<std::size_t>(l)];
			if (_phase != Phase::kNone && _lane == &lane) {
				RemoveDisplay(l);  // (the game's own two are there)
				continue;
			}
			if (!ballBase || !Ref(lane.ballReturn)) {
				continue;
			}
			if (!d.balls[0].get()) {
				// the lane's own balls on the return, if it has any, out of the way first
				const auto ret = ReturnPointOf(lane, 10.0f, s.returnZ);
				cell->ForEachReferenceInRange(ret, 170.0f, [&](RE::TESObjectREFR* a_ref) {
					auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
					if (base && base->GetFormID() == lanes::kBallBase && !a_ref->IsDisabled() && std::abs(a_ref->data.location.z - ret.z) < 25.0f &&
						a_ref->GetFormID() < 0xFF000000) {
						a_ref->Disable();
						d.hidden.push_back(a_ref->GetHandle());
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				for (int b = 0; b < 2; ++b) {
					d.balls[static_cast<std::size_t>(b)] = Spawn(ballBase, ReturnPointOf(lane, b == 0 ? s.returnSpot1 : s.returnSpot2, s.returnZ));
					d.prepared[static_cast<std::size_t>(b)] = false;
				}
			}
			for (int b = 0; b < 2; ++b) {
				auto ref = d.balls[static_cast<std::size_t>(b)].get();
				if (!ref) {
					continue;
				}
				Prepare(ref.get(), d.prepared[static_cast<std::size_t>(b)]);
				// (each turned its own way, so the two don't look stamped out)
				const double turn = b == 0 ? 0.6 : 2.3;
				const double m[3][3]{ { std::cos(turn), -std::sin(turn), 0.0 }, { std::sin(turn), std::cos(turn), 0.0 }, { 0.0, 0.0, 1.0 } };
				PlaceNode(ref.get(), ReturnPointOf(lane, b == 0 ? s.returnSpot1 : s.returnSpot2, s.returnZ), util::NodeRotation(m), false);
			}
		}
	}

	void Game::RemoveDisplay(int a_lane)
	{
		for (int l = 0; l < static_cast<int>(_display.size()); ++l) {
			if (a_lane >= 0 && l != a_lane) {
				continue;
			}
			auto& d = _display[static_cast<std::size_t>(l)];
			for (auto& h : d.balls) {
				if (auto ref = h.get()) {
					ref->Disable();
					ref->SetWantsDelete(true);
				}
				h = {};
			}
			for (auto& h : d.hidden) {
				if (auto ref = h.get(); ref && ref->IsDisabled()) {
					ref->Enable(false);
				}
			}
			d.hidden.clear();
		}
	}

	RE::NiPoint3 Game::HeldPoint() const
	{
		return lanes::ToWorld(*_lane, 0.0, _releaseY, Settings::Get().spec.ballRadius);
	}

	void Game::DrawPins()
	{
		if (!_sim || !_lane) {
			return;
		}
		for (int i = 0; i < bowl::Sim::kPins; ++i) {
			auto ref = _pins[static_cast<std::size_t>(i)].get();
			if (!ref) {
				continue;
			}
			Prepare(ref.get(), _pinPrepared[static_cast<std::size_t>(i)]);
			const auto& p = _sim->Pins()[static_cast<std::size_t>(i)];
			// swept away, or down at the bottom of the pit: out of sight
			bool       hidden = p.state == bowl::PinState::kSwept || p.frozen;
			bowl::Vec3 base;
			double     lane[3][3], world[3][3];
			if (_phase == Phase::kSetting) {
				hidden = !SettingPinPose(i, base, lane);  // (where the pinsetter has it)
			} else {
				_sim->PinPose(i, base, lane);
			}
			for (int r = 0; r < 3; ++r) {
				const double flip = r < 2 ? -1.0 : 1.0;  // the lane frame is the world turned half round
				for (int c = 0; c < 3; ++c) {
					world[r][c] = flip * lane[r][c];
				}
			}
			PlaceNode(ref.get(), lanes::ToWorld(*_lane, base.x, base.y, base.z), util::NodeRotation(world), hidden);
		}
	}

	void Game::DrawBalls(float a_dt)
	{
		if (!_lane) {
			return;
		}
		const auto& s = Settings::Get();
		const double R = s.spec.ballRadius;
		auto*       ret = Ref(_lane->ballReturn);
		const float a = ret ? ret->data.angle.z : 0.0f;
		const RE::NiPoint3 fwd{ std::sin(a), std::cos(a), 0.0f };
		for (int b = 0; b < 2; ++b) {
			auto& ball = _balls[static_cast<std::size_t>(b)];
			auto  ref = ball.ref.get();
			if (!ref) {
				continue;
			}
			bool hidden = false;
			switch (ball.at) {
			case BallAt::kReturn:
				{
					// rolling down the track to its spot
					const float step = s.returnSpeed * a_dt;
					const float before = ball.along;
					if (ball.along > ball.target) {
						ball.along = std::max(ball.target, ball.along - step);
					} else if (ball.along < ball.target) {
						ball.along = std::min(ball.target, ball.along + step);
					}
					const float moved = ball.along - before;
					if (a_dt > 0.0f && std::abs(moved) > 1e-4f) {
						// rolling: spin about the axis across its travel (up x velocity / radius)
						const RE::NiPoint3 v = fwd * (moved / a_dt);
						bowl::RotateBy(ball.orient, { -v.y / R, v.x / R, 0.0 }, a_dt);
					}
					ball.pos = ReturnPoint(ball.along, s.returnZ);
					break;
				}
			case BallAt::kHand:
				{
					const auto held = HeldPoint();
					if (_phase == Phase::kPickup) {
						// off the return in an arc to the foul line
						const float t = std::clamp(_phaseTime / kPickupSeconds, 0.0f, 1.0f);
						const float k = t * t * (3.0f - 2.0f * t);
						const auto  from = ReturnPoint(ball.along, s.returnZ);
						ball.pos = from + (held - from) * k + RE::NiPoint3{ 0.0f, 0.0f, std::sin(k * 3.1415927f) * 30.0f };
					} else {
						ball.pos = held;
					}
					break;
				}
			case BallAt::kLane:
				{
					if (!_sim) {
						break;
					}
					const auto& sb = _sim->GetBall();
					ball.pos = lanes::ToWorld(*_lane, sb.pos.x, sb.pos.y, sb.z);
					const auto w = lanes::DirToWorld(sb.spin.x, sb.spin.y, sb.spin.z);
					bowl::RotateBy(ball.orient, { w.x, w.y, w.z }, a_dt);
					break;
				}
			case BallAt::kGutter:
				{
					// along the slot to the gutter on the return's side, then back up it towards the foul line, where it
					// drops out of sight into the return
					const auto&  spec = s.spec;
					const double side = _sim ? (_sim->GetSpec().returnSide >= 0.0 ? 1.0 : -1.0) : 1.0;
					const double gy = side * spec.GutterLine();
					const double gz = R - spec.gutterDepth;
					const double speed = 420.0;
					double       step = speed * a_dt;
					double       vx = 0.0, vy = 0.0;
					// first across to the gutter's line (in the slot), then along it
					if (std::abs(ball.runY - gy) > 0.5) {
						const double d = std::min(step, std::abs(gy - ball.runY));
						vy = gy > ball.runY ? 1.0 : -1.0;
						ball.runY += vy * d;
						ball.runX = std::min(ball.runX, spec.deckEnd + R);
						step -= d;
					}
					if (step > 0.0) {
						vx = -1.0;
						ball.runX -= step;
					}
					ball.runZ += (gz - ball.runZ) * std::min(1.0, 6.0 * static_cast<double>(a_dt));
					ball.pos = lanes::ToWorld(*_lane, ball.runX, ball.runY, ball.runZ);
					// rolling: turning about the axis across its path
					const auto spin = lanes::DirToWorld(-vy * speed / R, vx * speed / R, 0.0);
					bowl::RotateBy(ball.orient, { spin.x, spin.y, spin.z }, a_dt);
					if (ball.runX <= 0.0) {
						ball.at = BallAt::kGone;  // (into the return: out of its hood in a moment)
						ball.hidden = 0.5f;
						hidden = true;
					}
					break;
				}
			case BallAt::kGone:
				if (_ballLingers && b == _active) {
					// still where it ended up (rolling about in the pit while the sim runs) until the sweep takes it
					const bool swept = _phase == Phase::kSetting ? _phaseTime >= SweepTakesBall() :
					                   (_phase != Phase::kResult);
					if (!swept && _sim) {
						if (_phase == Phase::kResult) {
							const auto& sb = _sim->GetBall();
							ball.pos = lanes::ToWorld(*_lane, sb.pos.x, sb.pos.y, sb.z);
							const auto w = lanes::DirToWorld(sb.spin.x, sb.spin.y, sb.spin.z);
							bowl::RotateBy(ball.orient, { w.x, w.y, w.z }, a_dt);
						}
						break;
					}
					_ballLingers = false;
				}
				hidden = true;
				ball.hidden -= a_dt;
				if (ball.hidden <= 0.0f) {
					// out of the hood and down the return, to the front if that's free
					const auto& other = _balls[static_cast<std::size_t>(1 - b)];
					const bool  frontTaken = other.at == BallAt::kReturn && other.target <= s.returnSpot1 + 0.5f;
					ball.at = BallAt::kReturn;
					ball.along = s.returnHood;
					ball.target = frontTaken ? s.returnSpot2 : s.returnSpot1;
					ball.pos = ReturnPoint(ball.along, s.returnZ);
					hidden = false;
				}
				break;
			}
			Prepare(ref.get(), ball.prepared);
			PlaceNode(ref.get(), ball.pos, util::NodeRotation(ball.orient), hidden);
		}
	}

	// ---------------------------------------------------------------- loading and saving

	void Game::Forget()
	{
		// a save's being loaded: the old game's references and camera went with it
		_phase = Phase::kNone;
		_sim.reset();
		_lane = nullptr;
		_pins.fill({});
		for (auto& b : _balls) {
			b = {};
		}
		_hiddenVanilla.clear();
		_deck = {};
		_sweep = {};
		for (auto& d : _display) {
			d = {};  // (gone with the old game's references)
		}
		_table = {};
		_lights = {};
		_held.clear();  // (they were let go before any save, so the loaded game has them free)
		_charging = false;
		_inCamera = false;
		_haveEntryView = false;
		{
			std::scoped_lock lock(_camMutex);
			_camMapped = false;
			_camPlaced = false;
		}
		BlockControls(false);
		BlockActivate(false);
		_lookLane = nullptr;
		_activateWasDown = true;
		_bannerTime = 0.0f;
		std::scoped_lock lock(_hudMutex);
		_hud = {};
	}

	void Game::ClearForSave()
	{
		// nothing of a game in a save: our pins and balls go, the lane's own come back
		if (_phase != Phase::kNone) {
			EndGame("Bowling: game stopped for the save");
		}
		RemoveDisplay(-1);  // (back the next moment)
		BlockActivate(false);
	}
}
