# fo4bowling

Playable bowling for Fallout 4, at Back Alley Bowling in the General Atomics Galleria. An F4SE plugin: no esp, and
nothing in the cell is edited (its precombines and previs stay as they are).

Lanes 3 and 6 are the two without wreckage on them. Walk up to either one's ball return, look at it and press the
activate key (F). The left and right arrows there pick how many play: solo, or 2 to 4 players, which is you against
1 to 3 others. Your companions are asked first, then the nearest friendly people (and robots) around the lanes; if
there aren't enough, the alley's regulars (Sal, Dot and Hank) make up the numbers. The prompt shows who you'd be
playing. The people you're playing stay where they are until the game's over.

## Playing

- **Aim** by moving the mouse sideways. The camera stays still behind the ball; a green line shows your aim.
- **A / D** move you along the foul line (the HUD shows which board you're on).
- **Hold the left mouse button**, pull the mouse back for power and move it sideways for spin (left hooks left, a
  right-hander's hook; right hooks right), then let go. Letting go at 0% cancels.
- **X twice** stops the game.

The ball skids through the oil on the front of the lane and hooks once it reaches the dry back end, so a hook ball
wants to start out wide and come back into the pocket (the 1-3 for a right-hander, the 1-2 for a left-hander). No
two balls are quite the same: every throw strays a little from what you set up (more when you throw it hard), the
lane drifts each ball a touch left or right, and the pinsetter never spots the pins exactly.

The pins are real rigid bodies: they fly, tumble, roll on their sides, wobble on their bases, bounce off the
kickbacks and drop into the gutters and the pit. Ten frames with strikes, spares and the tenth frame's extra balls,
scored on the HUD's sheet under everyone's names.

The sounds are real bowling alley recordings (CC0, from Freesound; see `Data\Sound\fo4bowling\CREDITS.txt`): the
ball landing and rolling down the lane, the crash into the rack and pins clattering, each heard from where it
happens.

Your opponents bowl with their own skill and hand (the same every game): each picks the ball up in the middle, walks
over to their spot, lines up the pocket for a strike or the key pin for a spare, and misses the way people do.

You need [F4SE](https://f4se.silverlock.org/) and the Address Library. The HUD (score sheet, meters, calls) needs
F4SE Menu Framework; without it the game still plays and the calls come up as the game's own notifications.

Settings are in `Data\F4SE\Plugins\fo4bowling.ini`, re-read at the start of every game.

## How it works

- `src/sim/Sim` is the physics, plain C++: the ball is a solid sphere with sliding and rolling friction on an oiled
  lane; the pins are rigid bodies (spheres along their axis, a ring for the foot) solved with impulses, friction and
  restitution against the ball, each other, the deck and its edges, the gutters, kickbacks and pit.
  `tests/sim_test.cpp` checks it (straight balls go straight, hooks hook after the oil, gutter balls touch nothing,
  pocket hits strike often but not always, left and right hands play alike).
- `src/sim/Score` is ten-pin scoring and turn order, tested in `tests/score_test.cpp`.
- `src/sim/Ai` picks a companion's throw: it works out the ball's path ahead with the same physics and finds the
  line to the pocket or the key pin. Tested in `tests/ai_test.cpp`.
- `src/Lanes.h` has the two lanes as they are in Fallout4.esm: lane 3's middle at y 1024 and lane 6's at 1792, the
  foul line at x -1664, the head pin 1064 down the lane, the ball returns 0015BDB0 and 0015BDB2.
- `src/Game.cpp` runs a game: it hides the lane's own pins and the balls on its return, places vanilla BowlingPin01
  and BowlingBall01 references with their Havok off, and moves them from the physics every frame. Balls come back
  out of the return's hood and roll down it to wait their turn.
- `src/Camera.cpp` drives the game's free camera: behind the ball while you aim, rolling along behind it, then
  watching the pins.

## Building

GitHub Actions builds it: the Linux job runs the tests, the Windows job builds the DLL with xmake and Dear-Modding
CommonLibF4 (submodule), targeting Fallout 4 1.10.163. Locally:

```
g++ -std=c++20 -O2 -Isrc tests/sim_test.cpp src/sim/Sim.cpp -o sim_test && ./sim_test
g++ -std=c++20 -O2 -Isrc tests/score_test.cpp src/sim/Score.cpp -o score_test && ./score_test
g++ -std=c++20 -O2 -Isrc tests/ai_test.cpp src/sim/Ai.cpp src/sim/Sim.cpp -o ai_test && ./ai_test
xmake f -m releasedbg -y && xmake build -y fo4bowling
```

GPL-3.0.
