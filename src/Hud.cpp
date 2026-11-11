// The HUD, drawn through F4SE Menu Framework when it's installed: the "F  START BOWLING" prompt at a ball return, and
// in a game the score sheet, the hook and power meters, where you're standing, the controls, and the calls (the X for
// a strike, GAME OVER).
//
// The look: dark smoked-glass panels with a soft shadow, an amber accent, and our own typefaces. Text is drawn from
// texture atlases of Oswald (two sizes: a small one for the score sheet and labels, a large one for headings and
// totals), and the strike X is Monoton's neon-tube X with its glow baked in. Both fonts are OFL (Data\F4SE\Plugins\
// fo4bowling\OFL-*.txt). If the textures can't be loaded, everything falls back to the framework's own font.
//
// Laid out on a 1280x720 design screen and scaled to the real one: scale = screen height / 720 x [HUD] fScale.

#include "Hud.h"

#include "F4SEMenuFramework.h"
#include "Game.h"
#include "HudGlyphs.h"
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
		float Ease(float a_t) { return 1.0f - std::pow(1.0f - std::clamp(a_t, 0.0f, 1.0f), 3.0f); }

		// the palette: warm whites on smoked glass, amber for what matters
		constexpr ImU32 kWhite = Rgba(244, 238, 226);
		constexpr ImU32 kSoft = Rgba(196, 188, 174);
		constexpr ImU32 kDim = Rgba(132, 124, 112);
		constexpr ImU32 kFaint = Rgba(255, 255, 255, 0.07f);
		constexpr ImU32 kRule = Rgba(255, 255, 255, 0.10f);
		constexpr ImU32 kAmber = Rgba(255, 178, 64);
		constexpr ImU32 kAmberSoft = Rgba(255, 178, 64, 0.16f);
		constexpr ImU32 kGlassTop = Rgba(26, 24, 28, 0.86f);
		constexpr ImU32 kGlassBottom = Rgba(10, 9, 12, 0.90f);
		constexpr ImU32 kInk = Rgba(18, 14, 8);

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

		// ------------------------------------------------------------ textures

		struct Texture
		{
			ImTextureID id{ nullptr };
			bool        tried{ false };
		};
		Texture g_small, g_large, g_x;

		ImTextureID Load(Texture& a_tex, const char* a_file)
		{
			if (!a_tex.tried) {
				a_tex.tried = true;
				wchar_t exe[MAX_PATH]{};
				::GetModuleFileNameW(nullptr, exe, MAX_PATH);
				const auto path = std::filesystem::path(exe).parent_path() / L"Data" / L"F4SE" / L"Plugins" / L"fo4bowling" / a_file;
				if (std::filesystem::exists(path)) {
					a_tex.id = F4SEMenuFramework::LoadTexture(path.string());
				}
				log::info("HUD texture {}: {}", a_file, a_tex.id ? "loaded" : "not loaded (falling back)");
			}
			return a_tex.id;
		}

		// (the draw-list calls the framework exports, looked up once)
		void Image(ImDrawList* a_dl, ImTextureID a_tex, ImVec2 a_min, ImVec2 a_max, ImVec2 a_uv0, ImVec2 a_uv1, ImU32 a_col)
		{
			using func_t = void (*)(ImDrawList*, ImTextureID, const ImVec2, const ImVec2, const ImVec2, const ImVec2, ImU32);
			static auto func = reinterpret_cast<func_t>(GetProcAddress(menuFramework, "ImDrawList_AddImage"));
			if (func) {
				func(a_dl, a_tex, a_min, a_max, a_uv0, a_uv1, a_col);
			}
		}
		void Gradient(ImDrawList* a_dl, ImVec2 a_min, ImVec2 a_max, ImU32 a_tl, ImU32 a_tr, ImU32 a_br, ImU32 a_bl)
		{
			using func_t = void (*)(ImDrawList*, const ImVec2, const ImVec2, ImU32, ImU32, ImU32, ImU32);
			static auto func = reinterpret_cast<func_t>(GetProcAddress(menuFramework, "ImDrawList_AddRectFilledMultiColor"));
			if (func) {
				func(a_dl, a_min, a_max, a_tl, a_tr, a_br, a_bl);
			}
		}

		// ------------------------------------------------------------ text

		struct Face
		{
			const Glyphs::Atlas*           atlas;
			Texture*                       tex;
			std::array<const Glyphs::Glyph*, 256> map{};
		};
		Face& FaceFor(float a_px)
		{
			// (not "small": a Windows macro)
			static Face faceSmall{ &Glyphs::kSmall, &g_small };
			static Face faceLarge{ &Glyphs::kLarge, &g_large };
			static bool built = false;
			if (!built) {
				built = true;
				for (Face* f : { &faceSmall, &faceLarge }) {
					for (int i = 0; i < f->atlas->count; ++i) {
						const auto& g = f->atlas->glyphs[i];
						if (g.code < 256) {
							f->map[g.code] = &g;
						}
					}
				}
			}
			return a_px <= 34.0f ? faceSmall : faceLarge;
		}

		// the characters of a UTF-8 string, as codes (only what the HUD uses: ASCII and the middle dot)
		template <class F>
		void EachCode(std::string_view a_text, F&& a_fn)
		{
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const auto c = static_cast<unsigned char>(a_text[i]);
				if (c == 0xC2 && i + 1 < a_text.size()) {
					a_fn(static_cast<unsigned char>(a_text[++i]));
				} else if (c < 0x80) {
					a_fn(c);
				}
			}
		}

		bool OwnFont(float a_px)
		{
			auto& f = FaceFor(a_px);
			return Load(*f.tex, f.atlas->file) != nullptr;
		}

		float TextWidth(const View& a_v, std::string_view a_text, float a_size, float a_track = 0.0f)
		{
			const float px = a_size * a_v.s;
			if (OwnFont(px)) {
				auto&       f = FaceFor(px);
				const float k = a_size / f.atlas->px;
				float       w = 0.0f;
				int         n = 0;
				EachCode(a_text, [&](unsigned a_c) {
					if (const auto* g = f.map[a_c]) {
						w += g->advance * k;
						++n;
					}
				});
				return w + a_track * static_cast<float>(std::max(0, n - 1));
			}
			const std::string t(a_text);
			ImVec2            out{};
			CalcTextSize(&out, t.c_str(), nullptr, false, -1.0f);
			const float base = GetFontSize();
			return base > 0.0f ? out.x * a_size / base : out.x;
		}

		// a_y is the top of the capitals; a_size is the size of the type in design units. a_track: extra space between
		// letters (headings are set wide)
		void Print(const View& a_v, std::string_view a_text, float a_x, float a_y, float a_size, ImU32 a_col, Align a_align = Align::kLeft,
			float a_track = 0.0f, bool a_shadow = true)
		{
			if (a_text.empty() || !(a_col >> 24)) {
				return;
			}
			const float w = TextWidth(a_v, a_text, a_size, a_track);
			const float x = a_align == Align::kLeft ? a_x : a_align == Align::kCentre ? a_x - w * 0.5f : a_x - w;
			const float px = a_size * a_v.s;
			if (OwnFont(px)) {
				auto&       f = FaceFor(px);
				const float k = a_size / f.atlas->px;
				const float baseline = a_y + a_size * 0.84f;  // (Oswald's capitals stand 0.84 of the size)
				const float iw = 1.0f / static_cast<float>(f.atlas->width);
				const float ih = 1.0f / static_cast<float>(f.atlas->height);
				const auto  run = [&](float a_dx, float a_dy, ImU32 a_c) {
					float pen = x + a_dx;
					EachCode(a_text, [&](unsigned a_code) {
						const auto* g = f.map[a_code];
						if (!g) {
							return;
						}
						if (g->w > 1 && g->h > 1) {
							const float gx = pen + g->ox * k;
							const float gy = baseline + a_dy + g->oy * k;
							Image(a_v.dl, f.tex->id, a_v.P(gx, gy), a_v.P(gx + g->w * k, gy + g->h * k), { g->x * iw, g->y * ih },
								{ (g->x + g->w) * iw, (g->y + g->h) * ih }, a_c);
						}
						pen += g->advance * k + a_track;
					});
				};
				if (a_shadow) {
					const float a = static_cast<float>(a_col >> 24) / 255.0f;
					run(0.0f, std::max(1.0f, a_size * 0.06f), Rgba(0, 0, 0, 0.55f * a));
				}
				run(0.0f, 0.0f, a_col);
				return;
			}
			// (no textures: the framework's own font)
			const std::string t(a_text);
			auto*             font = GetFont();
			if (a_shadow) {
				DL::AddText(a_v.dl, font, a_size * a_v.s, a_v.P(x + 1.2f, a_y + 1.2f), Fade(Rgba(0, 0, 0, 0.55f), static_cast<float>(a_col >> 24) / 255.0f), t.c_str());
			}
			DL::AddText(a_v.dl, font, a_size * a_v.s, a_v.P(x, a_y), a_col, t.c_str());
		}

		// ------------------------------------------------------------ shapes

		void Fill(const View& a_v, float a_x, float a_y, float a_w, float a_h, ImU32 a_col, float a_round = 0.0f)
		{
			DL::AddRectFilled(a_v.dl, a_v.P(a_x, a_y), a_v.P(a_x + a_w, a_y + a_h), a_col, a_round * a_v.s, 0);
		}
		void Outline(const View& a_v, float a_x, float a_y, float a_w, float a_h, ImU32 a_col, float a_round = 0.0f, float a_thick = 1.0f)
		{
			DL::AddRect(a_v.dl, a_v.P(a_x, a_y), a_v.P(a_x + a_w, a_y + a_h), a_col, a_round * a_v.s, 0, a_thick * a_v.s);
		}

		// a soft shadow under a panel: layers of faint rounded rects, each a little bigger
		void Shadow(const View& a_v, float a_x, float a_y, float a_w, float a_h, float a_alpha = 1.0f, float a_round = 8.0f)
		{
			for (int i = 6; i >= 1; --i) {
				const float g = static_cast<float>(i) * 2.2f;
				Fill(a_v, a_x - g, a_y - g + 4.0f, a_w + 2.0f * g, a_h + 2.0f * g, Rgba(0, 0, 0, 0.055f * a_alpha), a_round + g);
			}
		}

		// smoked glass: a shadow, a dark gradient, a hairline highlight along the top and a faint edge
		void Glass(const View& a_v, float a_x, float a_y, float a_w, float a_h, float a_alpha = 1.0f, float a_round = 8.0f)
		{
			Shadow(a_v, a_x, a_y, a_w, a_h, a_alpha, a_round);
			Fill(a_v, a_x, a_y, a_w, a_h, Fade(kGlassBottom, a_alpha), a_round);
			Gradient(a_v.dl, a_v.P(a_x + a_round * 0.3f, a_y), a_v.P(a_x + a_w - a_round * 0.3f, a_y + a_h * 0.55f), Fade(kGlassTop, a_alpha),
				Fade(kGlassTop, a_alpha), Fade(Rgba(26, 24, 28, 0.0f), a_alpha), Fade(Rgba(26, 24, 28, 0.0f), a_alpha));
			Outline(a_v, a_x, a_y, a_w, a_h, Fade(kRule, a_alpha), a_round);
			DL::AddLine(a_v.dl, a_v.P(a_x + a_round, a_y + 0.5f), a_v.P(a_x + a_w - a_round, a_y + 0.5f), Fade(Rgba(255, 255, 255, 0.16f), a_alpha), 1.0f * a_v.s);
		}

		// a key, as a little keycap: "F", "A/D", "MOUSE"
		float Keycap(const View& a_v, std::string_view a_key, float a_x, float a_y, float a_size, float a_alpha = 1.0f)
		{
			const float tw = TextWidth(a_v, a_key, a_size, 0.6f);
			const float w = std::max(a_size * 1.5f, tw + a_size * 0.9f);
			const float h = a_size * 1.55f;
			Fill(a_v, a_x, a_y + 2.0f, w, h, Fade(Rgba(0, 0, 0, 0.5f), a_alpha), 4.0f);
			Gradient(a_v.dl, a_v.P(a_x, a_y), a_v.P(a_x + w, a_y + h), Fade(Rgba(250, 244, 232), a_alpha), Fade(Rgba(250, 244, 232), a_alpha),
				Fade(Rgba(206, 198, 184), a_alpha), Fade(Rgba(206, 198, 184), a_alpha));
			Outline(a_v, a_x, a_y, w, h, Fade(Rgba(255, 255, 255, 0.6f), a_alpha), 4.0f);
			Print(a_v, a_key, a_x + w * 0.5f, a_y + (h - a_size) * 0.5f, a_size, Fade(kInk, a_alpha), Align::kCentre, 0.6f, false);
			return w;
		}

		// ------------------------------------------------------------ the pieces

		// at a ball return: [F] START BOWLING beside the crosshair, who you'd play and the lane underneath
		void Prompt(const View& a_v, const Game::HudState& a_st)
		{
			const float x = a_v.w * 0.5f + 42.0f;
			const float y = a_v.h * 0.5f + 18.0f;
			// "F) START BOWLING": the key, then the action
			std::string key = "F";
			std::string action = a_st.prompt;
			if (const auto p = a_st.prompt.find(") "); p != std::string::npos) {
				key = a_st.prompt.substr(0, p);
				action = a_st.prompt.substr(p + 2);
			}
			const float kw = TextWidth(a_v, key, 15.0f, 0.6f);
			const float cap = std::max(15.0f * 1.5f, kw + 15.0f * 0.9f);
			const float aw = TextWidth(a_v, action, 22.0f, 1.2f);
			float       lines = 0.0f;
			for (const auto* l : { &a_st.promptSub, &a_st.promptVs }) {
				lines = std::max(lines, TextWidth(a_v, *l, 13.0f, 0.8f));
			}
			const float w = std::max(cap + 14.0f + aw, lines) + 36.0f;
			const float h = a_st.promptVs.empty() ? 76.0f : 96.0f;
			Glass(a_v, x - 14.0f, y - 14.0f, w, h, 1.0f, 6.0f);
			Fill(a_v, x - 14.0f, y - 14.0f, 3.0f, h, kAmber, 1.5f);
			Keycap(a_v, key, x + 4.0f, y - 1.0f, 15.0f);
			Print(a_v, action, x + 4.0f + cap + 12.0f, y, 22.0f, kWhite, Align::kLeft, 1.2f);
			Print(a_v, a_st.promptSub, x + 4.0f, y + 34.0f, 13.0f, kSoft, Align::kLeft, 0.8f);
			if (!a_st.promptVs.empty()) {
				Print(a_v, a_st.promptVs, x + 4.0f, y + 54.0f, 13.0f, kAmber, Align::kLeft, 0.8f);
			}
		}

		// the score sheet across the top: a row per player, ten frames each, and the totals
		void Sheet(const View& a_v, const Game::HudState& a_st)
		{
			constexpr float kName = 150.0f;
			constexpr float kFrame = 48.0f;
			constexpr float kTenth = 66.0f;
			constexpr float kTotal = 76.0f;
			constexpr float kRow = 46.0f;
			constexpr float kHead = 20.0f;
			constexpr float kMark = 15.0f;
			constexpr float kW = kName + 9.0f * kFrame + kTenth + kTotal;
			const int       players = static_cast<int>(a_st.cards.size());
			const float     x0 = (a_v.w - kW) * 0.5f;
			const float     y0 = 14.0f;
			const float     ph = kHead + kRow * static_cast<float>(players) + 8.0f;
			Glass(a_v, x0 - 10.0f, y0 - 6.0f, kW + 20.0f, ph + 6.0f);

			// the heading: the lane on the left, frame numbers over their columns, TOTAL
			Print(a_v, std::format("LANE {}", a_st.lane), x0 + 6.0f, y0 + 4.0f, 10.0f, kAmber, Align::kLeft, 2.0f, false);
			float hx = x0 + kName;
			for (int f = 0; f < bowl::Card::kFrames; ++f) {
				const float fw = f == 9 ? kTenth : kFrame;
				Print(a_v, std::to_string(f + 1), hx + fw * 0.5f, y0 + 4.0f, 10.0f, kDim, Align::kCentre, 0.0f, false);
				hx += fw;
			}
			Print(a_v, "TOTAL", hx + kTotal * 0.5f, y0 + 4.0f, 10.0f, kDim, Align::kCentre, 2.0f, false);

			float y = y0 + kHead;
			for (int p = 0; p < players; ++p) {
				const auto& card = a_st.cards[static_cast<std::size_t>(p)];
				const bool  up = p == a_st.bowler && !a_st.over;
				// the row: a faint band, lit amber at the left for whoever's up
				if (up) {
					Gradient(a_v.dl, a_v.P(x0 - 10.0f, y), a_v.P(x0 + kW + 10.0f, y + kRow - 4.0f), Rgba(255, 178, 64, 0.20f), Rgba(255, 178, 64, 0.02f),
						Rgba(255, 178, 64, 0.02f), Rgba(255, 178, 64, 0.20f));
					Fill(a_v, x0 - 10.0f, y, 3.0f, kRow - 4.0f, kAmber);
				} else if (p % 2 == 1) {
					Fill(a_v, x0 - 10.0f, y, kW + 20.0f, kRow - 4.0f, kFaint);
				}
				std::string name = p < static_cast<int>(a_st.names.size()) ? a_st.names[static_cast<std::size_t>(p)] : std::string("?");
				while (name.size() > 3 && TextWidth(a_v, name, 17.0f, 0.6f) > kName - 18.0f) {
					name = name.substr(0, name.size() - 2) + ".";
				}
				Print(a_v, name, x0 + 6.0f, y + 13.0f, 17.0f, up ? kWhite : kSoft, Align::kLeft, 0.6f);

				float     x = x0 + kName;
				const int current = card.Done() ? -1 : card.Frame();
				for (int f = 0; f < bowl::Card::kFrames; ++f) {
					const float fw = f == 9 ? kTenth : kFrame;
					const bool  here = up && f == current;
					const float cx = x + 1.0f;
					const float cw = fw - 2.0f;
					const float ch = kRow - 6.0f;
					Fill(a_v, cx, y, cw, ch, here ? kAmberSoft : Rgba(0, 0, 0, 0.28f), 3.0f);
					if (here) {
						Outline(a_v, cx, y, cw, ch, kAmber, 3.0f, 1.5f);
					}
					// the marks, in their little boxes along the top right: a strike filled amber, a spare half filled
					const auto marks = card.Marks(f);
					const int  boxes = f == 9 ? 3 : 2;
					for (int k = 0; k < boxes; ++k) {
						const float mx = cx + cw - static_cast<float>(boxes - k) * kMark;
						const float my = y;
						const auto& m = marks[static_cast<std::size_t>(k)];
						DL::AddLine(a_v.dl, a_v.P(mx, my), a_v.P(mx, my + kMark), kRule, 1.0f * a_v.s);
						DL::AddLine(a_v.dl, a_v.P(mx, my + kMark), a_v.P(mx + kMark, my + kMark), kRule, 1.0f * a_v.s);
						if (m == "X") {
							Fill(a_v, mx + 1.5f, my + 1.5f, kMark - 3.0f, kMark - 3.0f, kAmber, 2.0f);
							Print(a_v, "X", mx + kMark * 0.5f, my + 3.2f, 9.5f, kInk, Align::kCentre, 0.0f, false);
						} else if (m == "/") {
							DL::AddTriangleFilled(a_v.dl, a_v.P(mx + 1.5f, my + kMark - 1.5f), a_v.P(mx + kMark - 1.5f, my + 1.5f),
								a_v.P(mx + kMark - 1.5f, my + kMark - 1.5f), kAmber);
						} else if (!m.empty()) {
							Print(a_v, m, mx + kMark * 0.5f, my + 3.2f, 9.5f, kSoft, Align::kCentre, 0.0f, false);
						}
					}
					const int total = card.Total(f);
					if (total >= 0) {
						Print(a_v, std::to_string(total), cx + cw * 0.5f, y + kMark + 6.0f, 15.0f, kWhite, Align::kCentre, 0.4f, false);
					}
					x += fw;
				}
				// the total so far, big, and the most it could still be
				Print(a_v, std::to_string(card.Total()), x + kTotal * 0.5f, y + 5.0f, 24.0f, up ? kAmber : kWhite, Align::kCentre, 0.5f);
				if (!card.Done()) {
					Print(a_v, std::format("MAX {}", card.Best()), x + kTotal * 0.5f, y + 32.0f, 8.0f, kDim, Align::kCentre, 1.2f, false);
				}
				y += kRow;
			}
		}

		// the controls along the bottom: "MOUSE: AIM   A/D: MOVE ..." as keycaps and labels
		void Keys(const View& a_v, const std::string& a_keys, float a_y)
		{
			struct Part
			{
				std::string key;
				std::string label;
			};
			std::vector<Part> parts;
			std::size_t       from = 0;
			while (from < a_keys.size()) {
				std::size_t to = a_keys.find("   ", from);
				std::string seg = a_keys.substr(from, to == std::string::npos ? std::string::npos : to - from);
				if (const auto c = seg.find(": "); c != std::string::npos) {
					parts.push_back({ seg.substr(0, c), seg.substr(c + 2) });
				} else if (!seg.empty()) {
					parts.push_back({ {}, seg });
				}
				if (to == std::string::npos) {
					break;
				}
				from = to + 3;
			}
			constexpr float kSize = 10.0f;
			float           total = 0.0f;
			for (const auto& p : parts) {
				const float kw = p.key.empty() ? 0.0f : std::max(kSize * 1.5f, TextWidth(a_v, p.key, kSize, 0.6f) + kSize * 0.9f) + 7.0f;
				total += kw + TextWidth(a_v, p.label, kSize, 1.0f) + 22.0f;
			}
			float x = (a_v.w - total + 22.0f) * 0.5f;
			for (const auto& p : parts) {
				if (!p.key.empty()) {
					x += Keycap(a_v, p.key, x, a_y - 2.0f, kSize) + 7.0f;
				}
				Print(a_v, p.label, x, a_y + 2.0f, kSize, kSoft, Align::kLeft, 1.0f);
				x += TextWidth(a_v, p.label, kSize, 1.0f) + 22.0f;
			}
		}

		// bottom middle while aiming: the hook, the power, the board you're on
		void Meters(const View& a_v, const Game::HudState& a_st)
		{
			const float cx = a_v.w * 0.5f;
			const float pw = 470.0f;
			const float y = a_v.h - 122.0f;
			Glass(a_v, cx - pw * 0.5f, y, pw, 78.0f);

			// hook: a track from full right to full left, a glowing knob where it is (left on screen is a hook left)
			const float tx0 = cx - 120.0f;
			const float tx1 = cx + 150.0f;
			const float tm = (tx0 + tx1) * 0.5f;
			const float hy = y + 22.0f;
			Print(a_v, "HOOK", cx - pw * 0.5f + 18.0f, hy - 5.0f, 10.0f, kDim, Align::kLeft, 2.0f, false);
			Fill(a_v, tx0, hy - 2.0f, tx1 - tx0, 4.0f, Rgba(255, 255, 255, 0.10f), 2.0f);
			for (int t = -4; t <= 4; ++t) {
				const float x = tm + static_cast<float>(t) * (tx1 - tx0) / 8.0f;
				DL::AddLine(a_v.dl, a_v.P(x, hy + 5.0f), a_v.P(x, hy + (t == 0 ? 11.0f : 8.0f)), t == 0 ? kSoft : kRule, 1.0f * a_v.s);
			}
			const float k = a_st.maxHook > 0.0f ? std::clamp(a_st.hook / a_st.maxHook, -1.0f, 1.0f) : 0.0f;
			const float nx = tm - k * (tx1 - tx0) * 0.5f;
			Fill(a_v, std::min(nx, tm), hy - 2.0f, std::abs(nx - tm), 4.0f, kAmber, 2.0f);
			DL::AddCircleFilled(a_v.dl, a_v.P(nx, hy), 9.0f * a_v.s, Rgba(255, 178, 64, 0.22f), 24);
			DL::AddCircleFilled(a_v.dl, a_v.P(nx, hy), 5.5f * a_v.s, kAmber, 24);
			DL::AddCircleFilled(a_v.dl, a_v.P(nx, hy), 2.2f * a_v.s, Rgba(255, 240, 210), 16);
			const std::string hook = std::abs(a_st.hook) < 0.5f ? std::string("STRAIGHT") :
			                         std::format("{} {:.0f}%", a_st.hook > 0.0f ? "LEFT" : "RIGHT", std::abs(a_st.hook));
			Print(a_v, hook, cx + pw * 0.5f - 18.0f, hy - 5.0f, 10.0f, kWhite, Align::kRight, 1.2f, false);

			// power: a bar that fills as you pull back, cool to hot
			const float py = y + 46.0f;
			const float bx0 = tx0;
			const float bx1 = tx1;
			Print(a_v, "POWER", cx - pw * 0.5f + 18.0f, py + 1.0f, 10.0f, kDim, Align::kLeft, 2.0f, false);
			Fill(a_v, bx0, py, bx1 - bx0, 12.0f, Rgba(255, 255, 255, 0.08f), 3.0f);
			if (a_st.charging && a_st.power > 0.001f) {
				const float fx = bx0 + (bx1 - bx0) * std::clamp(a_st.power, 0.0f, 1.0f);
				const float mid = bx0 + (bx1 - bx0) * 0.6f;
				const ImU32 c0 = Rgba(120, 220, 140);
				const ImU32 c1 = kAmber;
				const ImU32 c2 = Rgba(255, 92, 70);
				const auto  lerp = [](ImU32 a, ImU32 b, float t) {
					auto ch = [&](int sh) {
						const float va = static_cast<float>((a >> sh) & 0xFF), vb = static_cast<float>((b >> sh) & 0xFF);
						return static_cast<ImU32>(va + (vb - va) * t + 0.5f) << sh;
					};
					return ch(0) | ch(8) | ch(16) | (0xFFu << 24);
				};
				const float p = std::clamp(a_st.power, 0.0f, 1.0f);
				const ImU32 tip = p < 0.6f ? lerp(c0, c1, p / 0.6f) : lerp(c1, c2, (p - 0.6f) / 0.4f);
				Gradient(a_v.dl, a_v.P(bx0, py), a_v.P(std::min(fx, mid), py + 12.0f), c0, p < 0.6f ? tip : c1, p < 0.6f ? tip : c1, c0);
				if (fx > mid) {
					Gradient(a_v.dl, a_v.P(mid, py), a_v.P(fx, py + 12.0f), c1, tip, tip, c1);
				}
				Fill(a_v, fx - 1.5f, py - 3.0f, 3.0f, 18.0f, Rgba(255, 248, 230), 1.5f);
				DL::AddLine(a_v.dl, a_v.P(bx0, py + 1.0f), a_v.P(fx, py + 1.0f), Rgba(255, 255, 255, 0.35f), 1.0f * a_v.s);
			}
			Print(a_v, a_st.charging ? std::format("{:.0f}%", a_st.power * 100.0f) : std::format("BOARD {}", a_st.board), cx + pw * 0.5f - 18.0f,
				py + 1.0f, 10.0f, a_st.charging ? kWhite : kSoft, Align::kRight, 1.2f, false);

			Keys(a_v, a_st.keys, a_v.h - 30.0f);
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

		// the calls in the middle of the screen: the strike X, GAME OVER
		void Banner(const View& a_v, const Game::HudState& a_st)
		{
			const float a = a_st.bannerAlpha;
			if (a <= 0.0f || a_st.banner.empty()) {
				return;
			}
			const float cx = a_v.w * 0.5f;
			const float t = a_st.bannerAge;

			if (a_st.banner == "X") {
				// the neon X: slams in from big, settles, and its glow breathes
				const float in = Ease(t / 0.28f);
				const float size = 190.0f * (1.0f + 0.55f * (1.0f - in));
				const float cy = a_v.h * 0.34f;
				if (auto* tex = Load(g_x, "hud_strike_x.png")) {
					const float pulse = 0.85f + 0.15f * std::sin(t * 9.0f);
					// a dark vignette behind it so it reads over anything
					DL::AddCircleFilled(a_v.dl, a_v.P(cx, cy), size * 0.42f * a_v.s, Rgba(0, 0, 0, 0.35f * a), 48);
					Image(a_v.dl, tex, a_v.P(cx - size * 0.5f, cy - size * 0.5f), a_v.P(cx + size * 0.5f, cy + size * 0.5f), { 0.0f, 0.0f }, { 1.0f, 1.0f },
						Rgba(255, 255, 255, a * std::min(1.0f, in * 1.6f) * pulse + (1.0f - pulse) * a));
				} else {
					Print(a_v, "X", cx, cy - size * 0.3f, size * 0.6f, Fade(kAmber, a), Align::kCentre);
				}
				if (!a_st.bannerSub.empty()) {
					Print(a_v, a_st.bannerSub, cx, cy + 190.0f * 0.42f, 16.0f, Fade(kWhite, a * in), Align::kCentre, 3.0f);
				}
				return;
			}

			// a heading across a band that fades out at both ends, the line under it in amber
			const float in = Ease(t / 0.35f);
			const float y = a_v.h * 0.30f;
			const float size = 58.0f;
			const float w = std::max(TextWidth(a_v, a_st.banner, size, 6.0f), TextWidth(a_v, a_st.bannerSub, 16.0f, 2.0f)) + 260.0f;
			const float bh = a_st.bannerSub.empty() ? size + 34.0f : size + 62.0f;
			const float half = w * 0.5f * (0.6f + 0.4f * in);
			const ImU32 band = Rgba(8, 7, 10, 0.82f * a);
			const ImU32 none = Rgba(8, 7, 10, 0.0f);
			Gradient(a_v.dl, a_v.P(cx - half, y - 16.0f), a_v.P(cx, y - 16.0f + bh), none, band, band, none);
			Gradient(a_v.dl, a_v.P(cx, y - 16.0f), a_v.P(cx + half, y - 16.0f + bh), band, none, none, band);
			const ImU32 rule = Rgba(255, 178, 64, 0.9f * a);
			const ImU32 ruleNone = Rgba(255, 178, 64, 0.0f);
			Gradient(a_v.dl, a_v.P(cx - half, y - 17.0f), a_v.P(cx, y - 15.5f), ruleNone, rule, rule, ruleNone);
			Gradient(a_v.dl, a_v.P(cx, y - 17.0f), a_v.P(cx + half, y - 15.5f), rule, ruleNone, ruleNone, rule);
			Gradient(a_v.dl, a_v.P(cx - half, y - 17.5f + bh), a_v.P(cx, y - 16.0f + bh), ruleNone, rule, rule, ruleNone);
			Gradient(a_v.dl, a_v.P(cx, y - 17.5f + bh), a_v.P(cx + half, y - 16.0f + bh), rule, ruleNone, ruleNone, rule);
			Print(a_v, a_st.banner, cx, y, size, Fade(kWhite, a), Align::kCentre, 6.0f * in);
			Print(a_v, a_st.bannerSub, cx, y + size + 10.0f, 16.0f, Fade(kAmber, a), Align::kCentre, 2.0f);
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
			const View v = [&] {
				View r;
				r.dl = GetBackgroundDrawList();
				r.s = std::max(0.1f, viewport->Size.y / 720.0f * Settings::Get().hudScale);
				r.origin = viewport->Pos;
				r.w = viewport->Size.x / r.s;
				r.h = viewport->Size.y / r.s;
				return r;
			}();
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
