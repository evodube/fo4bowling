// The pinsetter: our own sweep bar and setting deck over the lane's pin deck. The deck is a copy of the alley's own
// machine (Machine_Bowling02, the rollers-and-pistons unit that sits low behind each lane's pit): the ones in the
// cell are part of its precombined mesh and can't move, so ours is an ordinary reference we place ourselves, like
// the pins. The sweep bar is a machine-kit bar to match.
//
// After every ball (unless the game's over):
//   second ball to come: the sweep comes down in front of the pins, the deck comes down and lifts the standing pins,
//                        the sweep pushes the fallen ones back into the pit, the deck sets the standing ones down
//                        again and everything goes back up
//   a fresh rack:        the sweep comes down and clears everything into the pit, then the deck brings down a new
//                        rack and sets it
//
// The physics isn't running meanwhile: the sim's Rack/Sweep is done at the start, and the pins are drawn where the
// machine has them until it's done.

#include "Game.h"

#include "Settings.h"
#include "Util.h"

namespace fb
{
	namespace
	{
		constexpr RE::TESFormID kDeckBase = 0x0023E75B;   // Machine_Bowling02 (a static collection): 209 x 206, z -21..194
		constexpr RE::TESFormID kSweepBase = 0x00136005;  // MacDetailGreeble26: a bar 118 long (y), 10 deep, z -4..4
		constexpr float         kDeckScale = 0.6f;        // -> 125 x 124, 129 tall: over the whole rack
		constexpr double        kDeckBottom = 21.0 * kDeckScale;  // (its underside below its origin)
		constexpr float         kSweepScale = 1.84f;      // -> 217: just inside the side walls
		constexpr double        kSweepBottom = 4.0 * kSweepScale;
		constexpr double        kSweepHalfDepth = 5.0 * kSweepScale;
		// the plate that holds the pins: the first pinsetter's deck (the yellow-lit steel panel), under the machine
		constexpr RE::TESFormID kTableBase = 0x00135F6C;  // MacBasePanelBMed01: 98 x 84 x 9, its foot at z 0
		constexpr float         kTableScale = 1.15f;      // -> 113 down the lane, 97 across: over the whole rack
		constexpr double        kTableBottom = 0.0;
		constexpr double        kTableHeight = 9.0 * kTableScale;  // (the machine sits on top of it)
		constexpr double        kGrab = 0.8;              // the deck's underside this far above a standing pin's head

		using Keys = std::vector<std::array<double, 2>>;  // (time, value), eased between

		double At(const Keys& a_keys, double a_t)
		{
			if (a_keys.empty()) {
				return 0.0;
			}
			if (a_t <= a_keys.front()[0]) {
				return a_keys.front()[1];
			}
			for (std::size_t i = 1; i < a_keys.size(); ++i) {
				if (a_t <= a_keys[i][0]) {
					const auto&  a = a_keys[i - 1];
					const auto&  b = a_keys[i];
					const double u = b[0] > a[0] ? (a_t - a[0]) / (b[0] - a[0]) : 1.0;
					const double e = u * u * (3.0 - 2.0 * u);
					return a[1] + (b[1] - a[1]) * e;
				}
			}
			return a_keys.back()[1];
		}

		// the machine's moves, per kind of cycle. Times in seconds
		struct Plan
		{
			Keys   sweepZ;      // the bar's foot above the deck
			Keys   sweepX;      // its middle, down the lane
			Keys   deck;        // the deck's underside above the deck
			double pushFrom;    // the sweep's pass back to the pit
			double pushTo;
			double holdFrom;    // (second ball) the standing pins ride with the deck
			double holdTo;
			double newFrom;     // (fresh rack) the new pins come down with the deck
			double newTo;
			double length;
		};

		Plan MakePlan(bool a_fresh, const bowl::Spec& a_spec, const Settings& a_s)
		{
			const double rest = a_s.deckHeight;
			const double bar = a_s.sweepHeight;
			const double grab = a_spec.pinHeight + kGrab;
			const double front = a_spec.kickbackStart + kSweepHalfDepth + 8.0;
			const double back = std::min(a_spec.deckEnd + 10.0, a_spec.deckEnd + a_spec.pitBack - kSweepHalfDepth - 2.0);  // (short of the back wall)
			Plan         p;
			if (a_fresh) {
				p.sweepZ = { { 0.0, bar }, { 0.5, 1.0 }, { 2.9, 1.0 }, { 3.4, bar } };
				p.sweepX = { { 0.0, front }, { 0.6, front }, { 1.4, back }, { 1.9, front } };
				p.deck = { { 0.0, rest }, { 1.9, rest }, { 2.6, grab }, { 2.75, grab }, { 3.3, rest } };
				p.pushFrom = 0.6;
				p.pushTo = 1.4;
				p.holdFrom = p.holdTo = -1.0;
				p.newFrom = 1.9;
				p.newTo = 2.6;
				p.length = 3.4;
			} else {
				p.sweepZ = { { 0.0, bar }, { 0.5, 1.0 }, { 3.7, 1.0 }, { 4.2, bar } };
				p.sweepX = { { 0.0, front }, { 1.8, front }, { 2.4, back }, { 2.9, front } };
				p.deck = { { 0.0, rest }, { 0.5, rest }, { 1.1, grab }, { 1.25, grab }, { 1.8, rest }, { 2.9, rest }, { 3.5, grab }, { 3.6, grab },
					{ 4.1, rest } };
				p.pushFrom = 1.8;
				p.pushTo = 2.4;
				p.holdFrom = 1.25;
				p.holdTo = 3.5;
				p.newFrom = p.newTo = -1.0;
				p.length = 4.2;
			}
			return p;
		}

		RE::TESBoundObject* Piece(RE::TESFormID a_id)
		{
			auto* form = RE::TESForm::GetFormByID(a_id);
			return form && form->IsBoundObject() ? static_cast<RE::TESBoundObject*>(form) : nullptr;
		}

		// the machine turned as the alley's own are (a quarter turn: its front towards the bowler)
		RE::NiMatrix3 MachineFacing()
		{
			const double m[3][3]{ { 0.0, 1.0, 0.0 }, { -1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 } };
			return util::NodeRotation(m);
		}

		// the plate under the machine: it's a wedge, thick at its -x end and thin at +x. Lengthwise down the lane,
		// its thin end back under the machine and its thick edge facing the bowler, so it reads as one solid plate
		// rather than a blade tapering off to nothing at one side
		RE::NiMatrix3 DownTheLane()
		{
			const double m[3][3]{ { -1.0, 0.0, 0.0 }, { 0.0, -1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
			return util::NodeRotation(m);
		}

		// the bar as it comes: its length along the world's y, which is across the lane
		RE::NiMatrix3 Straight()
		{
			const double m[3][3]{ { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
			return util::NodeRotation(m);
		}
	}

	void Game::SpawnMachine(const lanes::Lane& a_lane)
	{
		RemoveMachine();
		const auto& cfg = Settings::Get();
		// lights in the machine, shining on the pins: one in front of the rack, one over it (non-shadowing, so the deck
		// doesn't block them)
		if (cfg.pinLights) {
			if (auto* light = Piece(cfg.pinLightForm); light && light->GetFormType() == RE::ENUM_FORM_ID::kLIGH) {
				const double front = cfg.spec.kickbackStart - 20.0;
				const double over = cfg.spec.headPin + cfg.spec.pinSpacing * 1.5;
				_lights[0] = Spawn(light, lanes::ToWorld(a_lane, front, 0.0, cfg.pinLightHeight));
				_lights[1] = Spawn(light, lanes::ToWorld(a_lane, over, 0.0, cfg.pinLightHeight + 20.0));
			} else {
				log::warn("pinsetter: {:08X} isn't a light; no lights over the pins", cfg.pinLightForm);
			}
		}
		if (!cfg.pinsetter) {
			return;
		}
		auto* deck = Piece(kDeckBase);
		auto* sweep = Piece(kSweepBase);
		if (!deck || !sweep) {
			log::warn("pinsetter: its pieces ({:08X}, {:08X}) aren't in the game data; no pinsetter", kDeckBase, kSweepBase);
			return;
		}
		const auto& s = Settings::Get();
		_deck = Spawn(deck, lanes::ToWorld(a_lane, s.spec.headPin + 36.0, 0.0, s.deckHeight));
		_sweep = Spawn(sweep, lanes::ToWorld(a_lane, s.spec.kickbackStart + 20.0, 0.0, s.sweepHeight));
		if (auto* table = Piece(kTableBase)) {
			_table = Spawn(table, lanes::ToWorld(a_lane, s.spec.headPin + 36.0, 0.0, s.deckHeight));
		}
		_deckPrepared = _sweepPrepared = _tablePrepared = false;
	}

	void Game::RemoveMachine()
	{
		for (auto* h : { &_deck, &_sweep, &_table, &_lights[0], &_lights[1] }) {
			if (auto ref = h->get()) {
				ref->Disable();
				ref->SetWantsDelete(true);
			}
			*h = {};
		}
	}

	bool Game::MachineOn() const
	{
		return Settings::Get().pinsetter && _deck.get() && _sweep.get();
	}

	void Game::StartSetting()
	{
		// what's on the deck now, before the sim moves on: the fallen pins get swept from where they lie
		const auto& spec = _sim->GetSpec();
		for (int i = 0; i < bowl::Sim::kPins; ++i) {
			auto&       sp = _setPins[static_cast<std::size_t>(i)];
			const auto& p = _sim->Pins()[static_cast<std::size_t>(i)];
			_sim->PinPose(i, sp.base, sp.rot);
			sp.shown = p.state != bowl::PinState::kSwept && !p.frozen;
			const auto head = sp.base + _sim->PinAxis(i) * spec.pinHeight;
			sp.minX = std::min(sp.base.x, head.x);
			sp.onDeck = sp.shown && std::abs(sp.base.y) < spec.kickback && sp.base.z < 25.0 &&
			            sp.minX > spec.kickbackStart + 2.0 * kSweepHalfDepth + 6.0;  // (in the slot at the back too: swept into the pit)
			sp.standing = p.state == bowl::PinState::kStanding;
		}
		const auto& card = _match.CardOf(_match.Bowler());
		_setFresh = card.FreshRack();
		if (_setFresh) {
			_sim->Rack(Settings::Get().pinJitter, _rng());
		} else {
			_sim->Sweep();
		}
		for (int i = 0; i < bowl::Sim::kPins; ++i) {
			// (a standing pin is carried; one that's still standing after the sweep is one of those)
			auto& sp = _setPins[static_cast<std::size_t>(i)];
			sp.standing = !_setFresh && _sim->Pins()[static_cast<std::size_t>(i)].state == bowl::PinState::kStanding;
		}
		_phase = Phase::kSetting;
		_phaseTime = 0.0f;
	}

	float Game::SweepTakesBall() const
	{
		const Plan plan = MakePlan(_setFresh, _sim->GetSpec(), Settings::Get());
		return static_cast<float>(plan.pushFrom + 0.6 * (plan.pushTo - plan.pushFrom));
	}

	float Game::SettingLength() const
	{
		return static_cast<float>(MakePlan(_setFresh, _sim->GetSpec(), Settings::Get()).length);
	}

	bool Game::SettingPinPose(int a_pin, bowl::Vec3& a_base, double a_rot[3][3]) const
	{
		const auto& s = Settings::Get();
		const auto& spec = _sim->GetSpec();
		const Plan  plan = MakePlan(_setFresh, spec, s);
		const double t = _phaseTime;
		const auto&  sp = _setPins[static_cast<std::size_t>(a_pin)];
		const double grab = spec.pinHeight + kGrab;

		// the new rack (or the standing pins) where the machine has them
		const bool carried = _setFresh ? t >= plan.newFrom : sp.standing;
		if (carried) {
			_sim->PinPose(a_pin, a_base, a_rot);
			const bool riding = _setFresh ? t < plan.newTo : (t > plan.holdFrom && t < plan.holdTo);
			if (riding) {
				a_base.z += std::max(0.0, At(plan.deck, t) - grab);
			}
			return true;
		}
		// a fallen pin (or the whole old rack): where it lay, pushed back by the sweep, gone into the pit
		if (!sp.shown || t >= plan.pushTo || (!sp.onDeck && t >= plan.pushFrom)) {
			return false;
		}
		std::copy(&sp.rot[0][0], &sp.rot[0][0] + 9, &a_rot[0][0]);
		a_base = sp.base;
		if (sp.onDeck) {
			const double lead = At(plan.sweepX, t) + kSweepHalfDepth;
			const double shift = std::max(0.0, lead - sp.minX);
			if (sp.minX + shift > spec.deckEnd + 4.0) {
				return false;  // over the edge
			}
			a_base.x += shift;
		}
		return true;
	}

	void Game::DimLights()
	{
		// the lights at [Pinsetter] fLightBrightness of their own strength: what the light's drawn at, and what it
		// fades towards (kept to every frame, in case the game sets it back)
		const float k = Settings::Get().pinLightBrightness;
		for (auto& h : _lights) {
			auto  ref = h.get();
			auto* base = ref ? ref->GetObjectReference() : nullptr;
			auto* extra = ref && ref->extraList ? ref->extraList->GetByType<RE::ExtraLight>() : nullptr;
			if (!base || base->GetFormType() != RE::ENUM_FORM_ID::kLIGH || !extra || !extra->lightdata.light) {
				continue;
			}
			const float want = static_cast<RE::TESObjectLIGH*>(base)->fade * k;
			extra->lightdata.wantDimmer = want;
			extra->lightdata.light->dimmer = want;
		}
	}

	void Game::DrawMachine()
	{
		if (!_lane || !_sim) {
			return;
		}
		auto deck = _deck.get();
		auto sweep = _sweep.get();
		if (!deck || !sweep) {
			return;
		}
		Prepare(deck.get(), _deckPrepared);
		Prepare(sweep.get(), _sweepPrepared);
		const auto& s = Settings::Get();
		const auto& spec = _sim->GetSpec();
		double      deckZ = s.deckHeight;
		double      sweepZ = s.sweepHeight;
		double      sweepX = spec.kickbackStart + kSweepHalfDepth + 8.0;
		if (_phase == Phase::kSetting) {
			const Plan plan = MakePlan(_setFresh, spec, s);
			deckZ = At(plan.deck, _phaseTime);
			sweepZ = At(plan.sweepZ, _phaseTime);
			sweepX = At(plan.sweepX, _phaseTime);
		}

		// the deck's middle over the middle of the rack. Its underside is the steel plate: what holds the pins as the
		// deck brings them down (they hang just under it), with the machine on top
		const double rackMiddle = spec.headPin + spec.pinSpacing * 3.0 * 0.866 * 0.5;
		double       machineZ = deckZ;
		if (auto table = _table.get()) {
			Prepare(table.get(), _tablePrepared);
			PlaceNode(table.get(), lanes::ToWorld(*_lane, rackMiddle, 0.0, deckZ + kTableBottom), DownTheLane(), false, kTableScale);
			machineZ += kTableHeight;
		}
		PlaceNode(deck.get(), lanes::ToWorld(*_lane, rackMiddle, 0.0, machineZ + kDeckBottom), MachineFacing(), false, kDeckScale);
		PlaceNode(sweep.get(), lanes::ToWorld(*_lane, sweepX, 0.0, sweepZ + kSweepBottom), Straight(), false, kSweepScale);
	}
}
