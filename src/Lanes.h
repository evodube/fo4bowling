#pragma once

// Back Alley Bowling's two playable lanes, as they are in Fallout4.esm (cell GeneralAtomicsGalleria01, 000E791A).
//
// Lanes 3 and 6 are the two without wreckage on them. Their wood runs from the foul line at x -1664 down to the pit
// at -2944 (the GeneralAtomicsBowlingLane04 / 01 static collections), the pins stand behind the masking wall at
// -2688, and each has its own ball return beside it on the approach. The lane runs towards -x, so its frame's +y (the
// bowler's left) is the world's -y.

#include "Settings.h"

namespace fb::lanes
{
	inline constexpr RE::TESFormID kCell = 0x000E791A;          // GeneralAtomicsGalleria01
	inline constexpr RE::TESFormID kPinBase = 0x000DF266;       // BowlingPin01 (MISC)
	inline constexpr RE::TESFormID kBallBase = 0x000DF265;      // BowlingBall01 (MISC)
	inline constexpr RE::TESFormID kReturnBase = 0x0015BDAF;    // BowlingBallReturn01 (STAT)
	inline constexpr float         kFoulX = -1664.0f;
	inline constexpr float         kSurfaceZ = -48.0f;

	struct Lane
	{
		int                           number;
		float                         centreY;    // world y of the lane's middle
		RE::TESFormID                 ballReturn; // its BowlingBallReturn01
		std::array<RE::TESFormID, 10> pins;       // the vanilla pins standing on its deck (hidden while you play)
	};

	inline constexpr std::array<Lane, 2> kLanes{ {
		{ 3, 1024.0f, 0x0015BDB0, { 0x000E7B32, 0x000E7B33, 0x000E7B34, 0x000E7B35, 0x000E7B36, 0x000E7B37, 0x000E7B38, 0x000E7B39, 0x000E7B3A, 0x000E7B3B } },
		{ 6, 1792.0f, 0x0015BDB2, { 0x000E7964, 0x000E7965, 0x000E7966, 0x000E7967, 0x000E7968, 0x000E7969, 0x000E796A, 0x000E796B, 0x000E796C, 0x000E796D } },
	} };

	// lane frame (x down the lane from the foul line, y to the bowler's left, z up from the surface) -> world
	inline RE::NiPoint3 ToWorld(const Lane& a_lane, double a_x, double a_y, double a_z)
	{
		const auto& s = Settings::Get();
		return { kFoulX - static_cast<float>(a_x) - s.laneOffsetX, a_lane.centreY - static_cast<float>(a_y) - s.laneOffsetY,
			kSurfaceZ + static_cast<float>(a_z) + s.laneOffsetZ };
	}

	// a direction in the lane frame -> world (the lane frame is the world turned half round)
	inline RE::NiPoint3 DirToWorld(double a_x, double a_y, double a_z)
	{
		return { -static_cast<float>(a_x), -static_cast<float>(a_y), static_cast<float>(a_z) };
	}

	// world -> lane frame (x, y; z above the surface)
	inline void ToLane(const Lane& a_lane, const RE::NiPoint3& a_world, double& a_x, double& a_y, double& a_z)
	{
		const auto& s = Settings::Get();
		a_x = static_cast<double>(kFoulX - s.laneOffsetX - a_world.x);
		a_y = static_cast<double>(a_lane.centreY - s.laneOffsetY - a_world.y);
		a_z = static_cast<double>(a_world.z - kSurfaceZ - s.laneOffsetZ);
	}

	// the heading (the game's angle.z: 0 = +y, turning towards +x) of looking straight down the lane
	inline constexpr float kDownLaneYaw = -1.5707963f;
}
