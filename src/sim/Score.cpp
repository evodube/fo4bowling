#include "sim/Score.h"

#include <algorithm>

namespace bowl
{
	Card::FrameRolls Card::FrameAt(int a_frame) const
	{
		const int n = static_cast<int>(_rolls.size());
		int       idx = 0;
		for (int f = 0; f < kFrames; ++f) {
			FrameRolls fr;
			fr.first = idx < n ? idx : -1;
			if (f < kFrames - 1) {
				if (idx >= n) {
					fr.count = 0;
				} else if (Pins(idx) == 10) {
					fr.count = 1;
				} else {
					fr.count = std::min(2, n - idx);
				}
				if (f == a_frame) {
					return fr;
				}
				idx += fr.count == 1 && Pins(idx) == 10 ? 1 : 2;
			} else {
				fr.count = std::clamp(n - idx, 0, 3);
				if (f == a_frame) {
					return fr;
				}
			}
		}
		return {};
	}

	bool Card::Done() const
	{
		const auto fr = FrameAt(kFrames - 1);
		if (fr.count < 2) {
			return false;
		}
		const int a = Pins(fr.first);
		const int b = Pins(fr.first + 1);
		const bool bonus = a == 10 || a + b == 10;
		return bonus ? fr.count == 3 : true;
	}

	int Card::Frame() const
	{
		for (int f = 0; f < kFrames - 1; ++f) {
			const auto fr = FrameAt(f);
			const bool complete = fr.count == 2 || (fr.count == 1 && Pins(fr.first) == 10);
			if (!complete) {
				return f;
			}
		}
		return kFrames - 1;
	}

	int Card::BallInFrame() const
	{
		if (Done()) {
			return 0;
		}
		return FrameAt(Frame()).count;
	}

	int Card::PinsUp() const
	{
		if (Done()) {
			return 0;
		}
		const int  f = Frame();
		const auto fr = FrameAt(f);
		if (fr.count == 0) {
			return 10;
		}
		const int a = Pins(fr.first);
		if (f < kFrames - 1) {
			return 10 - a;
		}
		// the tenth: a strike or spare brings a fresh rack for the next ball
		if (fr.count == 1) {
			return a == 10 ? 10 : 10 - a;
		}
		const int b = Pins(fr.first + 1);
		if (a == 10) {
			return b == 10 ? 10 : 10 - b;
		}
		return 10;  // (a spare: the third ball at a full rack)
	}

	bool Card::FreshRack() const
	{
		return !Done() && PinsUp() == 10;
	}

	void Card::Add(int a_pins)
	{
		if (Done()) {
			_frameEnded = false;
			return;
		}
		const int f = Frame();
		_rolls.push_back(std::clamp(a_pins, 0, PinsUp()));
		_frameEnded = Done() || Frame() != f;
	}

	int Card::Total(int a_frame) const
	{
		int total = 0;
		for (int f = 0; f <= std::min(a_frame, kFrames - 1); ++f) {
			const auto fr = FrameAt(f);
			if (fr.count == 0) {
				return -1;
			}
			const int a = Pins(fr.first);
			if (f == kFrames - 1) {
				if (!Done()) {
					return -1;
				}
				for (int k = 0; k < fr.count; ++k) {
					total += Pins(fr.first + k);
				}
				continue;
			}
			if (a == 10) {
				const int b1 = Pins(fr.first + 1);
				const int b2 = Pins(fr.first + 2);
				if (b1 < 0 || b2 < 0) {
					return -1;
				}
				total += 10 + b1 + b2;
			} else {
				if (fr.count < 2) {
					return -1;
				}
				const int b = Pins(fr.first + 1);
				if (a + b == 10) {
					const int bonus = Pins(fr.first + 2);
					if (bonus < 0) {
						return -1;
					}
					total += 10 + bonus;
				} else {
					total += a + b;
				}
			}
		}
		return total;
	}

	int Card::Total() const
	{
		int last = 0;
		for (int f = 0; f < kFrames; ++f) {
			const int t = Total(f);
			if (t < 0) {
				break;
			}
			last = t;
		}
		return last;
	}

	int Card::Best() const
	{
		Card c = *this;
		while (!c.Done()) {
			c.Add(c.PinsUp());
		}
		return c.Total(kFrames - 1);
	}

	std::array<std::string, 3> Card::Marks(int a_frame) const
	{
		std::array<std::string, 3> m;
		const auto                 fr = FrameAt(a_frame);
		auto digit = [](int a_p) { return a_p == 0 ? std::string("-") : std::to_string(a_p); };
		if (fr.count == 0) {
			return m;
		}
		const int a = Pins(fr.first);
		if (a_frame < kFrames - 1) {
			if (a == 10) {
				m[1] = "X";
				return m;
			}
			m[0] = digit(a);
			if (fr.count > 1) {
				const int b = Pins(fr.first + 1);
				m[1] = a + b == 10 ? "/" : digit(b);
			}
			return m;
		}
		// the tenth: each ball in its own box, a fresh rack after a strike or spare
		int standing = 10;
		for (int k = 0; k < fr.count; ++k) {
			const int p = Pins(fr.first + k);
			if (p == standing && standing == 10) {
				m[static_cast<std::size_t>(k)] = "X";
				standing = 10;
			} else if (p == standing) {
				m[static_cast<std::size_t>(k)] = "/";
				standing = 10;
			} else {
				m[static_cast<std::size_t>(k)] = digit(p);
				standing -= p;
			}
		}
		return m;
	}

	Match::Match(int a_players) :
		_cards(static_cast<std::size_t>(std::clamp(a_players, 1, 4)))
	{}

	bool Match::Over() const
	{
		return std::ranges::all_of(_cards, [](const Card& a_c) { return a_c.Done(); });
	}

	bool Match::Add(int a_pins)
	{
		if (Over()) {
			return true;
		}
		auto& card = _cards[static_cast<std::size_t>(_bowler)];
		card.Add(a_pins);
		if (!card.FrameJustEnded()) {
			return false;
		}
		// the next player round who still has frames to bowl
		for (int k = 1; k <= Players(); ++k) {
			const int next = (_bowler + k) % Players();
			if (!_cards[static_cast<std::size_t>(next)].Done()) {
				_bowler = next;
				return true;
			}
		}
		return true;  // (everyone's done)
	}

	int Match::Winner() const
	{
		int best = -1;
		int who = -1;
		bool tie = false;
		for (int p = 0; p < Players(); ++p) {
			const int t = _cards[static_cast<std::size_t>(p)].Total();
			if (t > best) {
				best = t;
				who = p;
				tie = false;
			} else if (t == best) {
				tie = true;
			}
		}
		return tie ? -1 : who;
	}
}
