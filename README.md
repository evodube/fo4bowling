# fo4bowling

Playable bowling for Fallout 4, at Back Alley Bowling in the General Atomics Galleria. An F4SE plugin: no esp, and
nothing in the cell is edited (its precombines and previs stay as they are).

Lanes 3 and 6 are the two without wreckage on them. Walk up to either one's ball return, look at it and press the
activate key (F). Left and right there pick 1 to 4 players, taking turns at one keyboard.

## Playing

- **Aim** with the mouse: the camera sits behind the ball at the foul line and turns a few degrees either way.
- **Left / Right arrows** move you along the foul line (the HUD shows which board you're on).
- **Up / Down arrows** set the hook: Up hooks left (a right-hander's hook), Down hooks right.
- **Hold the left mouse button and pull the mouse back** for power; let go to throw. Letting go at 0% cancels.
- **X twice** stops the game.

The ball skids through the oil on the front of the lane and hooks once it reaches the dry back end, so a hook ball
wants to start out wide and come back into the pocket (the 1-3 for a right-hander, the 1-2 for a left-hander).
Pins fall the way they do in a real alley: they knock each other down, bounce off the kickbacks and slide into the
gutters. Ten frames with strikes, spares and the tenth frame's extra balls, scored on the HUD's sheet.

You need [F4SE](https://f4se.silverlock.org/) and the Address Library. The HUD (score sheet, meters, calls) needs
F4SE Menu Framework; without it the game still plays and the calls come up as the game's own notifications.

Settings are in `Data\F4SE\Plugins\fo4bowling.ini`, re-read at the start of every game.

## How it works

- `src/sim/Sim` is the physics, plain C++: the ball is a solid sphere with sliding and rolling friction on an oiled
  lane; the pins rock about the rim of their base, tip past it, and fall as capsules that knock into each other.
  `tests/sim_test.cpp` checks it (straight balls go straight, hooks hook after the oil, gutter balls touch nothing,
  pocket hits strike often but not always, left and right hands play alike).
- `src/sim/Score` is ten-pin scoring and turn order, tested in `tests/score_test.cpp`.
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
xmake f -m releasedbg -y && xmake build -y fo4bowling
```

GPL-3.0.
