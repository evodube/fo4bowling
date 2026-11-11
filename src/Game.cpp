// A game of bowling: starting it at a ball return, the throw, the pins and balls in the world, the frames and the
// scores.
//
// The pins and balls are ordinary BowlingPin01 / BowlingBall01 references we place ourselves, with their Havok off so
// the physics engine never fights our own (sim/Sim). The lane's own vanilla pins (and any balls on its return) are
// disabled while a game's on and enabled again when it ends; nothing in the cell is edited, so precombines and previs
// are untouched.

#include "Game.h"

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
		constexpr float  kOverSeconds = 6.0f;      // the final scores, then the game packs itself away
		constexpr float  kReturnDelay = 1.6f;      // a thrown ball spends this long in the machine before it comes back
		constexpr float  kQuitWindow = 3.0f;       // the quit key twice within this many seconds stops the game
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
				case Phase::kRolling:
					_sim->Advance(dt);
					for (const auto& e : _sim->TakeEvents()) {
						if (e.kind == bowl::Event::Kind::kBallGutter) {
							_lastBallGutter = true;
						}
					}
					if (_sim->Settled()) {
						BallSettled();
					}
					break;
				case Phase::kResult:
					if (_phaseTime >= kResultSeconds) {
						NextBall();
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
			DrawBalls(paused ? 0.0f : dt);
			UpdateCameraGoal(paused ? 0.0f : dt);
		}

		// what the HUD shows
		HudState h;
		{
			std::scoped_lock lock(_hudMutex);
			h.prompt = _hud.prompt;
			h.promptSub = _hud.promptSub;
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
			h.hook = _hook;
			h.maxHook = s.maxHook;
			// 39 boards across the lane, counted from the right gutter
			const double half = s.spec.laneHalfWidth;
			h.board = std::clamp(static_cast<int>(std::floor((_releaseY + half) / (2.0 * half) * 39.0)) + 1, 1, 39);
			h.banner = _bannerTime > 0.0f ? _banner : std::string();
			h.bannerSub = _bannerSub;
			h.bannerAlpha = _bannerLength > 0.0f ? std::clamp(std::min(_bannerTime, _bannerLength - _bannerTime) * 4.0f, 0.0f, 1.0f) : 0.0f;
			h.over = _phase == Phase::kOver;
			h.keys = std::format("{}/{}: MOVE   {}/{}: HOOK   HOLD LEFT MOUSE + PULL BACK: THROW   {} TWICE: QUIT",
				util::Upper(Settings::Key(s.keyLeft)), util::Upper(Settings::Key(s.keyRight)), util::Upper(Settings::Key(s.keyHookLeft)),
				util::Upper(Settings::Key(s.keyHookRight)), util::Upper(Settings::Key(s.keyQuit)));
		}
		std::scoped_lock lock(_hudMutex);
		_hud = std::move(h);
	}

	void Game::PollInput(float a_dt)
	{
		const auto& s = Settings::Get();
		const bool  focus = util::GameHasFocus();
		const std::array<int, 5> keys{ s.keyLeft, s.keyRight, s.keyHookLeft, s.keyHookRight, s.keyQuit };
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
			// at a ball return: left/right picks how many play, F starts
			if (_lookLane) {
				if (pressed[0] || pressed[1]) {
					_players = std::clamp(_players + (pressed[1] ? 1 : -1), 1, 4);
					_lookTime = {};  // (the prompt shows it at once)
				}
				if (activatePressed) {
					StartGame(*_lookLane, _players);
				}
			}
			return;
		}

		// the quit key, twice
		if (pressed[4]) {
			const auto now = std::chrono::steady_clock::now();
			if (_quitArmed && now - _quitAt < std::chrono::duration<float>(kQuitWindow)) {
				EndGame("Bowling: game stopped");
				return;
			}
			_quitArmed = true;
			_quitAt = now;
			Say(std::format("Bowling: press {} again to stop the game", Settings::Key(s.keyQuit)));
		}

		if (_phase != Phase::kAiming && _phase != Phase::kPickup) {
			_charging = false;
			return;
		}
		// along the foul line (held keys move smoothly)
		const double edge = s.spec.laneHalfWidth - s.spec.ballRadius;
		if (down[0] != down[1]) {
			_releaseY = std::clamp(_releaseY + (down[0] ? 1.0 : -1.0) * s.moveSpeed * a_dt, -edge, edge);
		}
		// hook: a press steps it, holding repeats
		if (down[2] != down[3]) {
			const float dir = down[2] ? 1.0f : -1.0f;
			if (pressed[2] || pressed[3]) {
				_hook = std::clamp(_hook + dir * s.hookStep, -s.maxHook, s.maxHook);
				_hookRepeat = -0.35f;
			} else {
				_hookRepeat += a_dt;
				if (_hookRepeat >= 0.08f) {
					_hookRepeat = 0.0f;
					_hook = std::clamp(_hook + dir * s.hookStep, -s.maxHook, s.maxHook);
				}
			}
		}
		// the throw: hold the left button and pull back, let go
		if (_phase == Phase::kAiming) {
			if (lmbPressed) {
				_power = 0.0f;
				_charging = true;
			} else if (lmbReleased && _charging) {
				_charging = false;
				if (_power.load() < 0.02f) {
					Say("Bowling: pull the mouse back while you hold the button to put power on it");
				} else {
					Throw();
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
		if (_lookLane && _lookLane != was && !Hud::Installed()) {
			// (no Menu Framework: the game's own notification says what F does here)
			Say(std::format("Bowling: {} at the ball return starts a game on lane {}", Settings::Key(_activateVk), _lookLane->number));
		}
		// while F is ours (at a ball return) the game's own use of it is off: no picking up the balls lying there
		BlockActivate(_lookLane != nullptr);
		std::scoped_lock lock(_hudMutex);
		if (_lookLane) {
			_hud.prompt = std::format("{}) START BOWLING", util::Upper(Settings::Key(_activateVk)));
			_hud.promptSub = std::format("<  {} PLAYER{}  >     LANE {}", _players, _players == 1 ? "" : "S", _lookLane->number);
		} else {
			_hud.prompt.clear();
			_hud.promptSub.clear();
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

	void Game::StartGame(const lanes::Lane& a_lane, int a_players)
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
		}

		_sim = std::make_unique<bowl::Sim>(s.spec);
		_sim->Rack();
		_match = bowl::Match(a_players);
		_names.clear();
		const char* own = player->GetDisplayFullName();
		const std::string you = own && *own ? util::Upper(own) : std::string("YOU");
		for (int p = 0; p < a_players; ++p) {
			_names.push_back(a_players == 1 ? you : p == 0 ? you : std::format("PLAYER {}", p + 1));
		}

		// the lane's own pins (and any balls on its return) out of the way; ours in their place
		HideVanilla(a_lane);
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
		Banner(std::format("LANE {}", a_lane.number), a_players == 1 ? std::string("FRAME 1") : std::format("{} PLAYERS · FRAME 1", a_players), 2.0f);
		log::info("game on lane {} for {} player(s)", a_lane.number, a_players);
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

	void Game::Throw()
	{
		if (_phase != Phase::kAiming || !_sim) {
			return;
		}
		const auto&   s = Settings::Get();
		const float   power = std::clamp(_power.load(), 0.0f, 1.0f);
		bowl::Release r;
		r.y = _releaseY;
		r.angle = _aimAngle;
		r.speed = s.minSpeed + (s.maxSpeed - s.minSpeed) * std::pow(power, 1.3f);
		r.revs = s.revs;
		r.axis = _hook * kPi / 180.0;
		_sim->Throw(r);
		_balls[static_cast<std::size_t>(_active)].at = BallAt::kLane;
		_lastBallGutter = false;
		_phase = Phase::kRolling;
		_phaseTime = 0.0f;
		_quitArmed = false;
		log::info("throw: board {} angle {:.2f} deg, power {:.0f}% = {:.0f} u/s, hook {:.0f} deg", _hud.board, r.angle * 180.0 / kPi,
			power * 100.0f, r.speed, _hook);
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
		Banner(title, sub, kResultSeconds + 0.2f);
		log::info("ball: {} down ({} standing: {}){}", down, standing, PinList(_sim->StandingMask()), split.empty() ? "" : " " + split);

		// the ball goes round the machine and comes back up the return
		auto& ball = _balls[static_cast<std::size_t>(_active)];
		ball.at = BallAt::kGone;
		ball.hidden = kReturnDelay;
		_phase = Phase::kResult;
		_phaseTime = 0.0f;
	}

	void Game::NextBall()
	{
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
		if (card.FreshRack()) {
			_sim->Rack();
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
		auto& ball = _balls[static_cast<std::size_t>(_active)];
		ball.at = BallAt::kHand;
		auto& other = _balls[static_cast<std::size_t>(1 - _active)];
		if (other.at == BallAt::kReturn) {
			other.target = s.returnSpot1;
		}
		_phase = Phase::kPickup;
		_phaseTime = 0.0f;
		if (card.FreshRack() && card.BallInFrame() == 0) {
			const std::string who = _names.size() > 1 ? _names[static_cast<std::size_t>(_match.Bowler())] + "  ·  " : std::string();
			Banner(std::format("FRAME {}", card.Frame() + 1), who + std::format("{}", card.Total()), 1.4f);
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

	void Game::PlaceNode(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiMatrix3& a_rot, bool a_hidden)
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
			local.scale = 1.0f / ps;
		} else {
			local.rotate = a_rot;
			local.translate = a_pos;
			local.scale = 1.0f;
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
		// the return's own frame: y runs along it (towards the hood, by the lane), z up from its foot
		auto* ret = _lane ? Ref(_lane->ballReturn) : nullptr;
		if (!ret) {
			return {};
		}
		const float a = ret->data.angle.z;
		return ret->data.location + RE::NiPoint3{ std::sin(a), std::cos(a), 0.0f } * a_along + RE::NiPoint3{ 0.0f, 0.0f, a_height };
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
		const auto& spec = _sim->GetSpec();
		for (int i = 0; i < bowl::Sim::kPins; ++i) {
			auto ref = _pins[static_cast<std::size_t>(i)].get();
			if (!ref) {
				continue;
			}
			Prepare(ref.get(), _pinPrepared[static_cast<std::size_t>(i)]);
			const auto& p = _sim->Pins()[static_cast<std::size_t>(i)];
			bool        hidden = p.state == bowl::PinState::kSwept;
			double      tilt = p.tilt;
			double      z = p.z;
			if (p.state == bowl::PinState::kGone) {
				if (p.pos.x > spec.deckEnd || std::abs(p.pos.y) <= spec.laneHalfWidth) {
					hidden = p.z < -50.0;  // (into the pit: gone once it's dropped out of sight)
				} else {
					tilt = kPi * 0.5;  // in a gutter: lying in it
					z = std::max(p.z, -spec.gutterDepth);
				}
			}
			// the pin's own frame: tipped by tilt towards tiltDir, turned about its axis by yaw
			const double lift = spec.pinRadius * std::sin(tilt) * std::sin(tilt);
			double       tip[3][3], turn[3][3], lane[3][3], world[3][3];
			util::AxisAngle(-p.tiltDir.y, p.tiltDir.x, 0.0, tilt, tip);
			util::AxisAngle(0.0, 0.0, 1.0, p.yaw, turn);
			util::Multiply(tip, turn, lane);
			for (int r = 0; r < 3; ++r) {
				const double flip = r < 2 ? -1.0 : 1.0;  // the lane frame is the world turned half round
				for (int c = 0; c < 3; ++c) {
					world[r][c] = flip * lane[r][c];
				}
			}
			PlaceNode(ref.get(), lanes::ToWorld(*_lane, p.pos.x, p.pos.y, z + lift), util::NodeRotation(world), hidden);
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
					if (sb.state == bowl::BallState::kPit) {
						hidden = true;
						break;
					}
					ball.pos = lanes::ToWorld(*_lane, sb.pos.x, sb.pos.y, sb.z);
					const auto w = lanes::DirToWorld(sb.spin.x, sb.spin.y, sb.spin.z);
					bowl::RotateBy(ball.orient, { w.x, w.y, w.z }, a_dt);
					break;
				}
			case BallAt::kGone:
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
		BlockActivate(false);
	}
}
