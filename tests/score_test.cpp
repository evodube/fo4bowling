// Ten-pin scoring tests. Build and run on any machine:
//   g++ -std=c++20 -O2 -Isrc tests/score_test.cpp src/sim/Score.cpp -o score_test && ./score_test

#include "sim/Score.h"

#include <cstdio>
#include <initializer_list>
#include <string>

using namespace bowl;

namespace
{
	int g_failed = 0;
	int g_checks = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		++g_checks;
		if (!a_ok) {
			++g_failed;
			std::printf("    FAIL: %s\n", a_what.c_str());
		}
	}

	Card Bowl(std::initializer_list<int> a_rolls)
	{
		Card c;
		for (const int p : a_rolls) {
			c.Add(p);
		}
		return c;
	}

	std::string Marks(const Card& a_c, int a_frame)
	{
		const auto m = a_c.Marks(a_frame);
		return m[0] + "|" + m[1] + "|" + m[2];
	}
}

int main()
{
	std::printf("scoring\n");
	{
		Card c;
		for (int i = 0; i < 12; ++i) {
			c.Add(10);
		}
		Check(c.Done(), "twelve strikes finish the game");
		Check(c.Total() == 300, "perfect game is 300, got " + std::to_string(c.Total()));
		Check(Marks(c, 0) == "|X|", "a strike shows X: " + Marks(c, 0));
		Check(Marks(c, 9) == "X|X|X", "the tenth shows XXX: " + Marks(c, 9));
	}
	{
		Card c;
		for (int i = 0; i < 20; ++i) {
			c.Add(0);
		}
		Check(c.Done() && c.Total() == 0, "all gutters: 0 after 20 balls");
		Check(Marks(c, 3) == "-|-|", "zeros show as dashes: " + Marks(c, 3));
	}
	{
		Card c;
		for (int i = 0; i < 21; ++i) {
			c.Add(5);
		}
		Check(c.Done(), "all 5/5 spares: 21 balls");
		Check(c.Total() == 150, "all 5-spares is 150, got " + std::to_string(c.Total()));
		Check(Marks(c, 0) == "5|/|", "a spare shows /: " + Marks(c, 0));
		Check(Marks(c, 9) == "5|/|5", "tenth 5/5: " + Marks(c, 9));
	}
	{
		const Card c = Bowl({ 10, 7, 3, 9, 0, 10, 0, 8, 8, 2, 0, 6, 10, 10, 10, 8, 1 });
		Check(c.Done(), "a sample game finishes");
		Check(c.Total(0) == 20, "strike then 7/3: 20, got " + std::to_string(c.Total(0)));
		Check(c.Total(1) == 39, "spare then 9: 39, got " + std::to_string(c.Total(1)));
		Check(c.Total(2) == 48, "9-: 48, got " + std::to_string(c.Total(2)));
		Check(c.Total() == 167, "the sample game is 167, got " + std::to_string(c.Total()));
	}
	{
		Card c;
		c.Add(10);
		Check(c.Total(0) == -1, "a strike waits for two more balls");
		c.Add(3);
		Check(c.Total(0) == -1, "...still waiting after one");
		Check(c.PinsUp() == 7, "3 down: 7 standing for the second ball");
		Check(!c.FreshRack(), "no fresh rack mid-frame");
		c.Add(4);
		Check(c.Total(0) == 17 && c.Total(1) == 24, "strike, 3, 4: 17 then 24");
		Check(c.FrameJustEnded() && c.Frame() == 2, "the frame ended; on to the third");
		Check(c.FreshRack(), "a new frame is a fresh rack");
	}
	{
		Card c;
		c.Add(9);
		c.Add(5);  // only one was standing
		Check(c.Rolls().back() == 1, "a ball can't knock down more than stood: " + std::to_string(c.Rolls().back()));
	}
	{
		// the tenth frame's racks
		Card c;
		for (int i = 0; i < 18; ++i) {
			c.Add(0);
		}
		Check(c.Frame() == 9 && c.PinsUp() == 10, "into the tenth with a full rack");
		c.Add(10);
		Check(!c.Done() && c.PinsUp() == 10 && c.FreshRack(), "a strike in the tenth: a fresh rack");
		c.Add(4);
		Check(!c.Done() && c.PinsUp() == 6, "then 4 down: 6 standing for the third");
		c.Add(6);
		Check(c.Done() && c.Total() == 20, "X 4 / in the tenth is 20, got " + std::to_string(c.Total()));
		Check(Marks(c, 9) == "X|4|/", "tenth marks X4/: " + Marks(c, 9));
	}
	{
		Card c;
		for (int i = 0; i < 18; ++i) {
			c.Add(0);
		}
		c.Add(3);
		c.Add(4);
		Check(c.Done() && c.Total() == 7, "an open tenth ends after two balls");
	}
	{
		Card c;
		for (int i = 0; i < 18; ++i) {
			c.Add(0);
		}
		c.Add(6);
		c.Add(4);
		Check(!c.Done() && c.PinsUp() == 10, "a spare in the tenth: a third ball at a full rack");
	}
	{
		Card c;
		c.Add(10);
		c.Add(10);
		Check(c.Best() == 300, "two strikes: 300 still possible, got " + std::to_string(c.Best()));
		c.Add(0);
		Check(c.Best() == 270, "then a 0: 270 best, got " + std::to_string(c.Best()));
	}

	std::printf("turns\n");
	{
		Match m(2);
		Check(m.Bowler() == 0, "player 1 starts");
		Check(m.Add(10), "a strike passes the turn");
		Check(m.Bowler() == 1, "player 2's frame");
		Check(!m.Add(3), "a first ball keeps the turn");
		Check(m.Add(2), "a second ball passes it");
		Check(m.Bowler() == 0, "back to player 1");
		for (int i = 0; i < 9 * 2 * 2; ++i) {
			m.Add(0);
		}
		Check(m.Over(), "two players, all frames: over");
		Check(m.Winner() == 0, "15 + 0s beats 5 + 0s");
	}
	{
		Match m(3);
		for (int f = 0; f < 10; ++f) {
			for (int p = 0; p < 3; ++p) {
				Check(m.Bowler() == p, "frame " + std::to_string(f + 1) + ": player " + std::to_string(p + 1) + "'s turn");
				m.Add(4);
				m.Add(4);
			}
		}
		Check(m.Over(), "three players finish together");
		Check(m.Winner() == -1, "a three-way tie has no winner");
	}

	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed == 0 ? 0 : 1;
}
