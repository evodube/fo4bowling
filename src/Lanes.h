#pragma once

// The playable lanes, as they are in the game's masters. Every alley here is built from the same kit: the
// GeneralAtomicsBowlingLane static collections (five 256-long floor plates from the pit to the foul line), the
// pinsetter machine Machine_Bowling02 behind the deck and a BowlingBallReturn01 on the approach. Each lane runs towards
// -x, so its frame's +y (the bowler's left) is the world's -y.
//
// Back Alley Bowling (Fallout4.esm, GeneralAtomicsGalleria01 000E791A): lanes 3 and 6, the two without wreckage. Foul
// line at x -1664, the surface at z -48.
//
// Beaver Creek Lanes (Far Harbor, DLCCoast.esm, DLC03BeaverCreekLanes01 xx0054DA): lane 5, with a full rack and
// nothing on its wood. Foul line at x 1920, the surface at z 272. Its pinsetter machine and back wall stand 32 further
// back than Back Alley's. Its ball return (shared with lane 6) is on its right.

#include "Settings.h"

namespace fb::lanes
{
	inline constexpr RE::TESFormID kPinBase = 0x000DF266;       // BowlingPin01 (MISC)
	inline constexpr RE::TESFormID kBallBase = 0x000DF265;      // BowlingBall01 (MISC)
	inline constexpr RE::TESFormID kReturnBase = 0x0015BDAF;    // BowlingBallReturn01 (STAT)

	struct Lane
	{
		int                           number;
		const char*                   alley;      // (for the log)
		const char*                   file;       // the master the cell and its references are in
		RE::TESFormID                 cell;       // (form ids here as in that file; Resolve() makes them the game's)
		float                         foulX;      // world x of the foul line
		float                         surfaceZ;   // world z of the lane's surface
		float                         centreY;    // world y of the lane's middle
		float                         backWall;   // how much further back the alley's back wall is than Back Alley's
		RE::TESFormID                 ballReturn; // its BowlingBallReturn01
		std::array<RE::TESFormID, 10> pins;       // the vanilla pins standing on its deck (hidden while you play)
		std::array<RE::TESFormID, 2>  strays;     // anything else of the lane's own lying on its wood (hidden too; 0 = none)
		bool                          present{ false };  // its master is loaded (set by Resolve)
	};

	inline std::array<Lane, 3> kLanes{ {
		{ 3, "Back Alley Bowling", "Fallout4.esm", 0x000E791A, -1664.0f, -48.0f, 1024.0f, 0.0f, 0x0015BDB0,
			{ 0x000E7B32, 0x000E7B33, 0x000E7B34, 0x000E7B35, 0x000E7B36, 0x000E7B37, 0x000E7B38, 0x000E7B39, 0x000E7B3A, 0x000E7B3B }, {} },
		{ 6, "Back Alley Bowling", "Fallout4.esm", 0x000E791A, -1664.0f, -48.0f, 1792.0f, 0.0f, 0x0015BDB2,
			{ 0x000E7964, 0x000E7965, 0x000E7966, 0x000E7967, 0x000E7968, 0x000E7969, 0x000E796A, 0x000E796B, 0x000E796C, 0x000E796D }, {} },
		{ 5, "Beaver Creek Lanes", "DLCCoast.esm", 0x000054DA, 1920.0f, 272.0f, 4080.0f, 32.0f, 0x0001BF61,
			{ 0x0002C24A, 0x0002C24B, 0x0002C24C, 0x0002C24D, 0x0002C24E, 0x0002C24F, 0x0002C250, 0x0002C251, 0x0002C252, 0x0002C253 },
			{ 0x0002C260, 0 } },  // (a pin lying on its side halfway down the lane)
	} };

	// the form ids as the game has them, from the masters' load order; a lane whose master isn't loaded is left out
	inline void Resolve()
	{
		static bool done = false;  // (the ids are rewritten in place: once)
		if (done) {
			return;
		}
		done = true;
		auto* data = RE::TESDataHandler::GetSingleton();
		for (auto& lane : kLanes) {
			if (std::string_view(lane.file) == "Fallout4.esm") {
				lane.present = true;
				continue;
			}
			if (!data || !data->LookupModByName(lane.file)) {
				lane.present = false;
				continue;
			}
			auto id = [&](RE::TESFormID& a_id) {
				if (a_id) {
					a_id = data->LookupFormID(a_id & 0x00FFFFFF, lane.file);
				}
			};
			id(lane.cell);
			id(lane.ballReturn);
			for (auto& p : lane.pins) {
				id(p);
			}
			for (auto& p : lane.strays) {
				id(p);
			}
			lane.present = lane.cell != 0;
		}
	}

	// is this cell one with a playable lane in it
	inline bool IsAlley(const RE::TESObjectCELL* a_cell)
	{
		if (!a_cell) {
			return false;
		}
		for (const auto& lane : kLanes) {
			if (lane.present && lane.cell == a_cell->GetFormID()) {
				return true;
			}
		}
		return false;
	}

	// lane frame (x down the lane from the foul line, y to the bowler's left, z up from the surface) -> world
	inline RE::NiPoint3 ToWorld(const Lane& a_lane, double a_x, double a_y, double a_z)
	{
		const auto& s = Settings::Get();
		return { a_lane.foulX - static_cast<float>(a_x) - s.laneOffsetX, a_lane.centreY - static_cast<float>(a_y) - s.laneOffsetY,
			a_lane.surfaceZ + static_cast<float>(a_z) + s.laneOffsetZ };
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
		a_x = static_cast<double>(a_lane.foulX - s.laneOffsetX - a_world.x);
		a_y = static_cast<double>(a_lane.centreY - s.laneOffsetY - a_world.y);
		a_z = static_cast<double>(a_world.z - a_lane.surfaceZ - s.laneOffsetZ);
	}

	// the heading (the game's angle.z: 0 = +y, turning towards +x) of looking straight down the lane
	inline constexpr float kDownLaneYaw = -1.5707963f;
}
