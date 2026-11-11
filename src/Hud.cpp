// The HUD, drawn through F4SE Menu Framework when it's installed: the "F) START BOWLING" prompt at a ball return, and
// in a game the score sheet, the hook and power meters, where you're standing, and the calls ("STRIKE!").
//
// Laid out on a 1280x720 design screen and scaled to the real one: scale = screen height / 720 x [HUD] fScale.

#include "Hud.h"

#include "F4SEMenuFramework.h"
#include "Game.h"
#include "Settings.h"

namespace fb::Hud
{
	namespace
	{
		using namespace ImGuiMCP;
		namespace DL = ImGuiMCP::ImDrawListManager;

		constexpr ImU32 Rgba(int r, int g, int b, float a = 1.0f)
		{
			return static_cast<ImU32>(r) | (static_cast<ImU32>(g) << 8) | (static_cast<ImU32>(b) << 16) |
			       (static_cast<ImU32>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
		}
		ImU32 Fade(ImU32 a_col, float a_alpha)
		{
			const float a = static_cast<float>(a_col >> 24) / 255.0f * std::clamp(a_alpha, 0.0f, 1.0f);
			return (a_col & 0x00FFFFFFu) | (static_cast<ImU32>(a * 255.0f + 0.5f) << 24);
		}

		constexpr ImU32 kBright = Rgba(170, 255, 170);
		constexpr ImU32 kText = Rgba(134, 255, 134);
		constexpr ImU32 kDim = Rgba(88, 168, 98);
		constexpr ImU32 kLine = Rgba(70, 140, 80, 0.9f);
		constexpr ImU32 kPanel = Rgba(6, 16, 9, 0.78f);
		constexpr ImU32 kShade = Rgba(0, 0, 0, 0.55f);
		constexpr ImU32 kHot = Rgba(255, 196, 90);
		constexpr ImU32 kHotPanel = Rgba(60, 44, 10, 0.55f);

		// the design screen mapped onto the real one
		struct View
		{
			ImDrawList* dl{ nullptr };
			ImVec2      origin{};
			float       s{ 1.0f };  // pixels per design unit
			float       w{ 1280.0f };
			float       h{ 720.0f };

			ImVec2 P(float a_x, float a_y) const { return { origin.x + a_x * s, origin.y + a_y * s }; }
		};

		enum class Align
		{
			kLeft,
			kCentre,
			kRight
		};

		float TextWidth(std::string_view a_text, float a_size)
		{
			const std::string t(a_text);
			ImVec2            out{};
			CalcTextSize(&out, t.c_str(), nullptr, false, -1.0f);
			const float base = GetFontSize();
			return base > 0.0f ? out.x * a_size / base : out.x;
		}

		// a_y is the top of the line; a_size in design units
		void Print(const View& a_v, std::string_view a_text, float a_x, float a_y, float a_size, ImU32 a_col, Align a_align = Align::kLeft,
			bool a_shadow = true)
		{
			if (a_text.empty()) {
				return;
			}
			const std::string t(a_text);
			const float       w = TextWidth(t, a_size);
			const float       x = a_align == Align::kLeft ? a_x : a_align == Align::kCentre ? a_x - w * 0.5f : a_x - w;
			auto*             font = GetFont();
			if (a_shadow) {
				DL::AddText(a_v.dl, font, a_size * a_v.s, a_v.P(x + 1.2f, a_y + 1.2f), Fade(kShade, static_cast<float>(a_col >> 24) / 255.0f), t.c_str());
			}
			DL::AddText(a_v.dl, font, a_size * a_v.s, a_v.P(x, a_y), a_col, t.c_str());
		}

		void Box(const View& a_v, float a_x, float a_y, float a_w, float a_h, ImU32 a_fill, ImU32 a_edge, float a_round = 3.0f)
		{
			DL::AddRectFilled(a_v.dl, a_v.P(a_x, a_y), a_v.P(a_x + a_w, a_y + a_h), a_fill, a_round * a_v.s, 0);
			if (a_edge >> 24) {
				DL::AddRect(a_v.dl, a_v.P(a_x, a_y), a_v.P(a_x + a_w, a_y + a_h), a_edge, a_round * a_v.s, 0, 1.0f * a_v.s);
			}
		}

		View MakeView(ImGuiViewport* a_viewport)
		{
			View v;
			v.dl = GetBackgroundDrawList();
			v.s = std::max(0.1f, a_viewport->Size.y / 720.0f * Settings::Get().hudScale);
			v.origin = a_viewport->Pos;
			v.w = a_viewport->Size.x / v.s;
			v.h = a_viewport->Size.y / v.s;
			return v;
		}

		// ------------------------------------------------------------ the pieces

		// "F) START BOWLING" beside the crosshair, like the game's own prompt
		void Prompt(const View& a_v, const Game::HudState& a_st)
		{
			const float x = a_v.w * 0.5f + 40.0f;
			const float y = a_v.h * 0.5f + 22.0f;
			Print(a_v, a_st.prompt, x, y, 24.0f, kBright);
			Print(a_v, a_st.promptSub, x + 2.0f, y + 30.0f, 15.0f, kDim);
			if (!a_st.promptVs.empty()) {
				Print(a_v, a_st.promptVs, x + 2.0f, y + 50.0f, 13.0f, kDim);
			}
		}

		// the score sheet: a row per player, ten frames each
		void Sheet(const View& a_v, const Game::HudState& a_st)
		{
			constexpr float kName = 128.0f;
			constexpr float kFrame = 50.0f;
			constexpr float kTenth = 70.0f;
			constexpr float kRow = 44.0f;
			constexpr float kMark = 16.0f;
			constexpr float kTotalW = kName + 9.0f * kFrame + kTenth + 60.0f;
			const int       players = static_cast<int>(a_st.cards.size());
			const float     x0 = (a_v.w - kTotalW) * 0.5f;
			float           y = 16.0f;
			Box(a_v, x0 - 8.0f, y - 8.0f, kTotalW + 16.0f, kRow * players + 16.0f + 14.0f, kPanel, kLine, 5.0f);
			Print(a_v, std::format("LANE {}", a_st.lane), x0, y - 6.0f, 11.0f, kDim, Align::kLeft, false);
			y += 10.0f;
			for (int p = 0; p < players; ++p) {
				const auto& card = a_st.cards[static_cast<std::size_t>(p)];
				const bool  up = p == a_st.bowler && !a_st.over;
				float       x = x0;
				if (up) {
					Box(a_v, x - 4.0f, y - 2.0f, kTotalW + 8.0f, kRow - 2.0f, kHotPanel, 0, 4.0f);
				}
				std::string name = p < static_cast<int>(a_st.names.size()) ? a_st.names[static_cast<std::size_t>(p)] : std::string("?");
				if (name.size() > 12) {
					name = name.substr(0, 11) + ".";
				}
				Print(a_v, (up ? "> " : "  ") + name, x, y + 12.0f, 15.0f, up ? kHot : kText);
				x += kName;
				const int current = card.Done() ? -1 : card.Frame();
				for (int f = 0; f < bowl::Card::kFrames; ++f) {
					const float fw = f == 9 ? kTenth : kFrame;
					const bool  here = up && f == current;
					Box(a_v, x, y, fw - 2.0f, kRow - 6.0f, here ? Rgba(255, 196, 90, 0.10f) : Rgba(0, 0, 0, 0.25f), here ? kHot : kLine, 2.0f);
					const auto marks = card.Marks(f);
					const int  boxes = f == 9 ? 3 : 2;
					for (int k = 0; k < boxes; ++k) {
						const float mx = x + fw - 2.0f - (boxes - k) * kMark;
						DL::AddRect(a_v.dl, a_v.P(mx, y), a_v.P(mx + kMark, y + kMark), kLine, 0.0f, 0, 1.0f * a_v.s);
						const auto& m = marks[static_cast<std::size_t>(k)];
						const ImU32 col = m == "X" || m == "/" ? kHot : kBright;
						Print(a_v, m, mx + kMark * 0.5f, y + 1.0f, 13.0f, col, Align::kCentre, false);
					}
					const int total = card.Total(f);
					if (total >= 0) {
						Print(a_v, std::to_string(total), x + fw * 0.5f - 1.0f, y + kMark + 3.0f, 15.0f, kBright, Align::kCentre, false);
					}
					Print(a_v, std::to_string(f + 1), x + 3.0f, y + 1.0f, 9.0f, kDim, Align::kLeft, false);
					x += fw;
				}
				// the total so far, and the most it can still be
				Print(a_v, std::to_string(card.Total()), x + 30.0f, y + 6.0f, 20.0f, up ? kHot : kBright, Align::kCentre);
				if (!card.Done()) {
					Print(a_v, std::format("MAX {}", card.Best()), x + 30.0f, y + 28.0f, 9.0f, kDim, Align::kCentre, false);
				}
				y += kRow;
			}
		}

		// bottom middle while aiming: the hook, the power, where you're standing
		void Meters(const View& a_v, const Game::HudState& a_st)
		{
			const float cx = a_v.w * 0.5f;
			const float y = a_v.h - 118.0f;
			Box(a_v, cx - 230.0f, y - 10.0f, 460.0f, 84.0f, kPanel, kLine, 5.0f);

			// hook: a scale from full right to full left, with a needle
			const float hw = 170.0f;
			const float hy = y + 14.0f;
			Print(a_v, "HOOK", cx - 222.0f, hy - 7.0f, 12.0f, kDim, Align::kLeft, false);
			DL::AddLine(a_v.dl, a_v.P(cx - hw * 0.5f + 30.0f, hy), a_v.P(cx + hw * 0.5f + 30.0f, hy), kLine, 2.0f * a_v.s);
			for (int t = -4; t <= 4; ++t) {
				const float tx = cx + 30.0f + t * hw / 8.0f;
				DL::AddLine(a_v.dl, a_v.P(tx, hy - (t == 0 ? 7.0f : 4.0f)), a_v.P(tx, hy + (t == 0 ? 7.0f : 4.0f)), kLine, 1.0f * a_v.s);
			}
			const float k = a_st.maxHook > 0.0f ? std::clamp(a_st.hook / a_st.maxHook, -1.0f, 1.0f) : 0.0f;
			const float nx = cx + 30.0f - k * hw * 0.5f;  // (left on screen is the hook to the left)
			DL::AddTriangleFilled(a_v.dl, a_v.P(nx, hy - 2.0f), a_v.P(nx - 6.0f, hy - 12.0f), a_v.P(nx + 6.0f, hy - 12.0f), kHot);
			const std::string hook = std::abs(a_st.hook) < 0.5f ? std::string("STRAIGHT") :
			                         std::format("{} {:.0f}%", a_st.hook > 0.0f ? "LEFT" : "RIGHT", std::abs(a_st.hook));
			Print(a_v, hook, cx + 222.0f, hy - 7.0f, 12.0f, kBright, Align::kRight, false);

			// power: 20 segments, filled as you pull
			const float py = y + 38.0f;
			Print(a_v, "POWER", cx - 222.0f, py, 12.0f, kDim, Align::kLeft, false);
			const int   lit = static_cast<int>(std::round(a_st.power * 20.0f));
			const float segW = 12.0f;
			const float px0 = cx - 150.0f;
			for (int i = 0; i < 20; ++i) {
				const float sx = px0 + i * (segW + 3.0f);
				const bool  on = a_st.charging && i < lit;
				const ImU32 col = on ? (i >= 16 ? Rgba(255, 120, 90) : i >= 11 ? kHot : kBright) : Rgba(40, 70, 45, 0.7f);
				DL::AddRectFilled(a_v.dl, a_v.P(sx, py), a_v.P(sx + segW, py + 16.0f), col, 2.0f * a_v.s, 0);
			}
			Print(a_v, a_st.charging ? std::format("{:.0f}%", a_st.power * 100.0f) : std::format("BOARD {}", a_st.board), cx + 222.0f, py, 12.0f,
				kBright, Align::kRight, false);

			Print(a_v, a_st.keys, cx, a_v.h - 26.0f, 11.0f, kDim, Align::kCentre);
		}

		// the aim line: green from the ball out along your aim, fading as it goes
		void Guide(ImGuiViewport* a_viewport, const Game::HudState& a_st)
		{
			auto* dl = GetBackgroundDrawList();
			if (!dl || a_st.guide.size() < 2) {
				return;
			}
			const float  scale = std::max(0.1f, a_viewport->Size.y / 720.0f);
			const auto   at = [&](const std::array<float, 2>& a_p) {
				return ImVec2{ a_viewport->Pos.x + a_p[0] * a_viewport->Size.x, a_viewport->Pos.y + a_p[1] * a_viewport->Size.y };
			};
			const std::size_t n = a_st.guide.size();
			for (std::size_t i = 1; i < n; ++i) {
				const float  fade = 1.0f - static_cast<float>(i) / static_cast<float>(n);
				const ImVec2 a = at(a_st.guide[i - 1]);
				const ImVec2 b = at(a_st.guide[i]);
				DL::AddLine(dl, a, b, Rgba(40, 255, 90, 0.25f * fade), 7.0f * scale);  // (a soft glow under it)
				DL::AddLine(dl, a, b, Rgba(120, 255, 140, 0.95f * fade + 0.05f), 2.5f * scale);
			}
		}

		// "STRIKE!" in the middle of the screen
		void Banner(const View& a_v, const Game::HudState& a_st)
		{
			const float a = a_st.bannerAlpha;
			if (a <= 0.0f || a_st.banner.empty()) {
				return;
			}
			const float cx = a_v.w * 0.5f;
			const float y = a_v.h * 0.30f;
			const bool  big = a_st.banner == "STRIKE!" || a_st.banner == "SPARE!" || a_st.banner == "GAME OVER";
			const float size = big ? 60.0f : 42.0f;
			const float w = std::max(TextWidth(a_st.banner, size), TextWidth(a_st.bannerSub, 18.0f)) + 60.0f;
			Box(a_v, cx - w * 0.5f, y - 12.0f, w, size + 46.0f, Fade(kPanel, a), Fade(kLine, a), 6.0f);
			Print(a_v, a_st.banner, cx, y, size, Fade(big ? kHot : kBright, a), Align::kCentre);
			Print(a_v, a_st.bannerSub, cx, y + size + 6.0f, 18.0f, Fade(kText, a), Align::kCentre);
		}

		void __stdcall Draw()
		{
			auto* viewport = GetMainViewport();
			if (!viewport) {
				return;
			}
			const auto st = Game::Get().Hud();
			if (!st.game && st.prompt.empty()) {
				return;
			}
			const View v = MakeView(viewport);
			if (!v.dl) {
				return;
			}
			if (!st.game) {
				Prompt(v, st);
				return;
			}
			Guide(viewport, st);
			Sheet(v, st);
			if (st.aiming) {
				Meters(v, st);
			}
			Banner(v, st);
		}

		F4SEMenuFramework::Model::HudElement* g_element{ nullptr };
	}

	void Install()
	{
		if (g_element) {
			return;
		}
		if (!F4SEMenuFramework::IsInstalled()) {
			log::warn("F4SE Menu Framework not found: no HUD (the game still plays, with notifications)");
			return;
		}
		g_element = F4SEMenuFramework::AddHudElement(Draw);
		log::info("HUD element registered");
	}

	bool Installed()
	{
		return g_element != nullptr;
	}
}
