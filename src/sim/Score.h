#pragma once

// Ten-pin scoring for up to four players taking turns a frame at a time. Plain C++, tested in tests/score_test.cpp.

#include <array>
#include <string>
#include <vector>

namespace bowl
{
	class Card
	{
	public:
		static constexpr int kFrames = 10;

		// pins down with this ball (0..10, never more than were standing)
		void Add(int a_pins);

		bool Done() const;
		int  Frame() const;      // the frame being bowled, 0..9 (9 when done)
		int  BallInFrame() const;  // 0, 1 or 2 (the third only in the tenth)
		// pins standing for the next ball: 10 after a strike or spare (or a new frame)
		int  PinsUp() const;
		// a fresh rack for the next ball (a new frame, or a strike/spare in the tenth)
		bool FreshRack() const;
		// the frame just finished with this ball (false while the frame's still going)
		bool FrameJustEnded() const { return _frameEnded; }

		// running total up to and including frame a_frame, or -1 if it can't be counted yet (strike/spare waiting)
		int Total(int a_frame) const;
		int Total() const;  // everything counted so far
		// the most it can still come to (all strikes from here)
		int Best() const;

		// the marks to print in frame a_frame's boxes: "X", "/", "-", digits; up to three
		std::array<std::string, 3> Marks(int a_frame) const;

		const std::vector<int>& Rolls() const { return _rolls; }

	private:
		struct FrameRolls
		{
			int first{ -1 };   // index into _rolls
			int count{ 0 };
		};
		FrameRolls FrameAt(int a_frame) const;
		int        Pins(int a_roll) const { return a_roll >= 0 && a_roll < static_cast<int>(_rolls.size()) ? _rolls[static_cast<std::size_t>(a_roll)] : -1; }

		std::vector<int> _rolls;
		bool             _frameEnded{ false };
	};

	// Whose turn it is in a game of up to four.
	class Match
	{
	public:
		explicit Match(int a_players = 1);

		int  Players() const { return static_cast<int>(_cards.size()); }
		int  Bowler() const { return _bowler; }
		bool Over() const;

		// pins down by the bowler's ball. Returns true if the turn passed on (to the next player, or the game's done).
		bool Add(int a_pins);

		Card&       CardOf(int a_player) { return _cards[static_cast<std::size_t>(a_player)]; }
		const Card& CardOf(int a_player) const { return _cards[static_cast<std::size_t>(a_player)]; }

		// the winner (the highest total), or -1 on a tie for first
		int Winner() const;

	private:
		std::vector<Card> _cards;
		int               _bowler{ 0 };
	};
}
