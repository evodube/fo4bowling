#include "Lanes.h"
#include "Audio.h"
#include "CameraHook.h"
#include "Game.h"
#include "Hook.h"
#include "Hud.h"
#include "MenuWatch.h"
#include "Settings.h"

namespace
{
	constexpr auto kName = "fo4bowling";

	void OnMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case F4SE::MessagingInterface::kPostPostLoad:
			fb::menus::Watch();  // (the UI may not exist yet: then at game data ready)
			fb::Hud::Install();  // F4SE Menu Framework is loaded by now, if it's installed
			break;
		case F4SE::MessagingInterface::kGameDataReady:
			fb::log::info("game data ready");
			fb::Settings::Get().Load();
			fb::lanes::Resolve();
			for (const auto& lane : fb::lanes::kLanes) {
				fb::log::info("lane {} at {}: {}", lane.number, lane.alley, lane.present ? "playable" : "its master isn't loaded");
			}
			fb::Hook::Install();
			fb::CameraHook::Install();
			fb::Audio::Init();
			fb::menus::Watch();
			break;
		case F4SE::MessagingInterface::kPreLoadGame:
			fb::Game::Get().Forget();
			break;
		case F4SE::MessagingInterface::kPreSaveGame:
			fb::Game::Get().ClearForSave();
			break;
		case F4SE::MessagingInterface::kPostLoadGame:
		case F4SE::MessagingInterface::kNewGame:
			fb::Hook::Install();  // no-op if already hooked
			break;
		default:
			break;
		}
	}
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = kName;
	a_info->version = 1;

	if (a_f4se->IsEditor()) {
		return false;
	}
	return a_f4se->RuntimeVersion() >= F4SE::RUNTIME_1_10_163;
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	// fo4bowling.log is this run; .1 to .3 the runs before
	F4SE::Init(a_f4se, { .log = true, .logName = kName, .logRotate = 3, .trampoline = false });

	fb::log::info("fo4bowling loading on runtime {} ({})", a_f4se->RuntimeVersion().string(),
		REX::FModule::IsRuntimeOG() ? "OG" : REX::FModule::IsRuntimeNG() ? "NG" : "AE");

	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		fb::log::error("could not register F4SE message listener");
		return false;
	}
	fb::log::info("loaded; waiting for game data");
	return true;
}
