#include "Settings.h"

namespace fb
{
	namespace
	{
		constexpr auto kIniPath = L"Data\\F4SE\\Plugins\\fo4bowling.ini";

		std::wstring ReadString(const wchar_t* a_section, const wchar_t* a_key, const std::wstring& a_default)
		{
			wchar_t buf[512]{};
			::GetPrivateProfileStringW(a_section, a_key, a_default.c_str(), buf, static_cast<DWORD>(std::size(buf)), kIniPath);
			std::wstring value = buf;
			// strip an inline "; comment" and the whitespace round it
			for (std::size_t i = 0; i < value.size(); ++i) {
				if (value[i] == L';' && (i == 0 || value[i - 1] == L' ' || value[i - 1] == L'\t')) {
					value.resize(i);
					break;
				}
			}
			while (!value.empty() && (value.back() == L' ' || value.back() == L'\t')) {
				value.pop_back();
			}
			return value;
		}

		long long ReadInt(const wchar_t* a_section, const wchar_t* a_key, long long a_default)
		{
			const auto str = ReadString(a_section, a_key, std::to_wstring(a_default));
			try {
				return std::stoll(str, nullptr, 0);  // (0x58 style too)
			} catch (...) {
				return a_default;
			}
		}

		float ReadFloat(const wchar_t* a_section, const wchar_t* a_key, float a_default)
		{
			const auto str = ReadString(a_section, a_key, std::to_wstring(a_default));
			try {
				return std::stof(str);
			} catch (...) {
				return a_default;
			}
		}

		double ReadDouble(const wchar_t* a_section, const wchar_t* a_key, double a_default)
		{
			return static_cast<double>(ReadFloat(a_section, a_key, static_cast<float>(a_default)));
		}

		bool ReadBool(const wchar_t* a_section, const wchar_t* a_key, bool a_default)
		{
			return ReadInt(a_section, a_key, a_default ? 1 : 0) != 0;
		}

		int ReadKey(const wchar_t* a_key, int a_default)
		{
			return static_cast<int>(std::clamp<long long>(ReadInt(L"Keys", a_key, a_default), 0, 0xFE));
		}
	}

	void Settings::Load()
	{
		keyLeft = ReadKey(L"iLeft", keyLeft);
		keyRight = ReadKey(L"iRight", keyRight);
		keyHookLeft = ReadKey(L"iHookLeft", keyHookLeft);
		keyHookRight = ReadKey(L"iHookRight", keyHookRight);
		keyFewer = ReadKey(L"iFewerPlayers", keyFewer);
		keyMore = ReadKey(L"iMorePlayers", keyMore);
		keyQuit = ReadKey(L"iQuit", keyQuit);
		activateDistance = std::clamp(ReadFloat(L"Keys", L"fActivateDistance", activateDistance), 60.0f, 600.0f);

		minSpeed = std::max(50.0f, ReadFloat(L"Throw", L"fMinSpeed", minSpeed));
		maxSpeed = std::max(minSpeed + 1.0f, ReadFloat(L"Throw", L"fMaxSpeed", maxSpeed));
		pullPerCount = std::clamp(ReadFloat(L"Throw", L"fPullPerCount", pullPerCount), 0.0001f, 0.1f);
		invertPull = ReadBool(L"Throw", L"bInvertPull", invertPull);
		aimPerCount = std::clamp(ReadFloat(L"Throw", L"fAimPerCount", aimPerCount), 0.0005f, 1.0f);
		maxAim = std::clamp(ReadFloat(L"Throw", L"fMaxAim", maxAim), 0.0f, 20.0f);
		revs = std::clamp(ReadFloat(L"Throw", L"fRevs", revs), 0.0f, 150.0f);
		maxHook = std::clamp(ReadFloat(L"Throw", L"fMaxHook", maxHook), 0.0f, 89.0f);
		hookStep = std::clamp(ReadFloat(L"Throw", L"fHookStep", hookStep), 1.0f, 50.0f);
		spinPerCount = std::clamp(ReadFloat(L"Throw", L"fSpinPerCount", spinPerCount), 0.0f, 10.0f);
		moveSpeed = std::clamp(ReadFloat(L"Throw", L"fMoveSpeed", moveSpeed), 5.0f, 400.0f);
		inaccuracy = std::clamp(ReadFloat(L"Throw", L"fInaccuracy", inaccuracy), 0.0f, 10.0f);
		laneDrift = std::clamp(ReadFloat(L"Throw", L"fLaneDrift", laneDrift), 0.0f, 10.0f);

		guide = ReadBool(L"Guide", L"bEnabled", guide);
		guideLength = std::clamp(ReadFloat(L"Guide", L"fLength", guideLength), 20.0f, 2000.0f);
		guideFlipY = ReadBool(L"Guide", L"bFlipY", guideFlipY);

		camDistance = std::clamp(ReadFloat(L"Camera", L"fDistance", camDistance), 20.0f, 400.0f);
		camHeight = std::clamp(ReadFloat(L"Camera", L"fHeight", camHeight), 0.0f, 200.0f);
		camPitch = std::clamp(ReadFloat(L"Camera", L"fPitch", camPitch), -20.0f, 80.0f);
		followBall = ReadBool(L"Camera", L"bFollowBall", followBall);
		followStop = ReadFloat(L"Camera", L"fFollowStop", followStop);

		laneOffsetX = ReadFloat(L"Lane", L"fOffsetX", laneOffsetX);
		laneOffsetY = ReadFloat(L"Lane", L"fOffsetY", laneOffsetY);
		laneOffsetZ = ReadFloat(L"Lane", L"fOffsetZ", laneOffsetZ);

		returnSpot1 = ReadFloat(L"Return", L"fSpot1", returnSpot1);
		returnSpot2 = ReadFloat(L"Return", L"fSpot2", returnSpot2);
		returnZ = ReadFloat(L"Return", L"fZ", returnZ);
		returnHood = ReadFloat(L"Return", L"fHood", returnHood);
		returnSpeed = std::clamp(ReadFloat(L"Return", L"fSpeed", returnSpeed), 5.0f, 1000.0f);

		hudScale = std::clamp(ReadFloat(L"HUD", L"fScale", hudScale), 0.3f, 3.0f);
		notifications = ReadBool(L"HUD", L"bNotifications", notifications);

		auto& s = spec;
		s.headPin = ReadDouble(L"Physics", L"fHeadPin", s.headPin);
		s.laneHalfWidth = ReadDouble(L"Physics", L"fLaneHalfWidth", s.laneHalfWidth);
		s.gutterWidth = ReadDouble(L"Physics", L"fGutterWidth", s.gutterWidth);
		s.gutterDepth = ReadDouble(L"Physics", L"fGutterDepth", s.gutterDepth);
		s.kickback = ReadDouble(L"Physics", L"fKickback", s.kickback);
		s.kickbackStart = ReadDouble(L"Physics", L"fKickbackStart", s.kickbackStart);
		s.deckEnd = ReadDouble(L"Physics", L"fDeckEnd", s.deckEnd);
		s.oilLength = ReadDouble(L"Physics", L"fOilLength", s.oilLength);
		s.oilFriction = ReadDouble(L"Physics", L"fOilFriction", s.oilFriction);
		s.dryFriction = ReadDouble(L"Physics", L"fDryFriction", s.dryFriction);
		s.rollResistance = ReadDouble(L"Physics", L"fRollResistance", s.rollResistance);
		s.ballRadius = std::max(1.0, ReadDouble(L"Physics", L"fBallRadius", s.ballRadius));
		s.pinSpacing = std::max(5.0, ReadDouble(L"Physics", L"fPinSpacing", s.pinSpacing));
		s.pinRestitution = ReadDouble(L"Physics", L"fPinRestitution", s.pinRestitution);
		s.pinPinRestitution = ReadDouble(L"Physics", L"fPinPinRestitution", s.pinPinRestitution);
		s.pinDeckFriction = ReadDouble(L"Physics", L"fPinDeckFriction", s.pinDeckFriction);
		s.pinFriction = ReadDouble(L"Physics", L"fPinFriction", s.pinFriction);
		pinJitter = std::clamp(ReadFloat(L"Physics", L"fPinJitter", pinJitter), 0.0f, 5.0f);

		log::info("settings: throw {:.0f}-{:.0f}, revs {:.0f}, hook up to {:.0f} deg, inaccuracy {:.2f} | lane offset ({:.1f}, {:.1f}, {:.1f}) | head pin {:.0f}, oil {:.0f}",
			minSpeed, maxSpeed, revs, maxHook, inaccuracy, laneOffsetX, laneOffsetY, laneOffsetZ, s.headPin, s.oilLength);
	}

	std::string Settings::Key(int a_vk)
	{
		switch (a_vk) {
		case 0: return "(none)";
		case VK_LEFT: return "Left";
		case VK_RIGHT: return "Right";
		case VK_UP: return "Up";
		case VK_DOWN: return "Down";
		case VK_SPACE: return "Space";
		case VK_RETURN: return "Enter";
		case VK_ESCAPE: return "Esc";
		case VK_TAB: return "Tab";
		case VK_BACK: return "Backspace";
		case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "Shift";
		case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "Ctrl";
		case VK_MENU: case VK_LMENU: case VK_RMENU: return "Alt";
		case VK_INSERT: return "Insert";
		case VK_DELETE: return "Delete";
		case VK_HOME: return "Home";
		case VK_END: return "End";
		case VK_PRIOR: return "Page Up";
		case VK_NEXT: return "Page Down";
		case VK_MULTIPLY: return "Numpad *";
		case VK_ADD: return "Numpad +";
		case VK_SUBTRACT: return "Numpad -";
		case VK_DECIMAL: return "Numpad .";
		case VK_DIVIDE: return "Numpad /";
		default: break;
		}
		if (a_vk >= VK_NUMPAD0 && a_vk <= VK_NUMPAD9) {
			return std::format("Numpad {}", a_vk - VK_NUMPAD0);
		}
		if (a_vk >= VK_F1 && a_vk <= VK_F24) {
			return std::format("F{}", a_vk - VK_F1 + 1);
		}
		if ((a_vk >= '0' && a_vk <= '9') || (a_vk >= 'A' && a_vk <= 'Z')) {
			return std::string(1, static_cast<char>(a_vk));
		}
		return std::format("key {:02X}", a_vk);
	}
}
