#pragma once

namespace fb::Hud
{
	// Registers the HUD with F4SE Menu Framework, if it's installed. Safe to call more than once.
	void Install();

	// the HUD's there to show calls and scores (otherwise they go to the game's corner notifications)
	bool Installed();
}
