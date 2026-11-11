// The HUD, drawn through F4SE Menu Framework when it's installed: the "F) START BOWLING" prompt at a ball return, and
// in a game the score sheet, the hook and power meters, where you're standing, the controls, and the calls (the X for
// a strike, GAME OVER).
//
// Four looks, picked with [HUD] sStyle or on the framework's settings page (Playable Bowling > Settings):
//   Modern    : smoked glass, amber, Oswald, Monoton's neon X
//   Pip-Boy   : Fallout 4's own HUD: its green, Roboto Condensed, bracketed panels, a segmented power bar, a compass
//               strip for the hook, faint scanlines
//   Vault-Tec : 1950s blue and yellow, rounded panels, Roboto Condensed Black Italic, a navy X on a starburst
//   RobCo     : a green-phosphor terminal in VT323, text meters, a blinking cursor, a blocky X, scanlines
// Text is drawn from texture atlases of those typefaces (all SIL Open Font License, see Data\F4SE\Plugins\
// fo4bowling\OFL-*.txt), and each X is a texture with its glow baked in. If the textures can't be loaded,
// everything falls back to the framework's own font.
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
		ImU32 Lerp(ImU32 a_a, ImU32 a_b, float a_t)
		{
			const auto ch = [&](int a_sh) {
				const float va = static_cast<float>((a_a >> a_sh) & 0xFF), vb = static_cast<float>((a_b >> a_sh) & 0xFF);
				return static_cast<ImU32>(va + (vb - va) * std::clamp(a_t, 0.0f, 1.0f) + 0.5f) << a_sh;
			};
			return ch(0) | ch(8) | ch(16) | ch(24);
		}
		float Ease(float a_t) { return 1.0f - std::pow(1.0f - std::clamp(a_t, 0.0f, 1.0f), 3.0f); }

		// ------------------------------------------------------------ the looks

		enum class Style
		{
			kModern,
			kPipBoy,
			kVaultTec,
			kRobCo
		};

		struct Theme
		{
			Style                style;
			const Glyphs::Atlas* body[2];  // small, large
			const Glyphs::Atlas* head[2];  // (names, totals, headings)
			const char*          xFile;
			ImU32                text, soft, dim, accent, ink, panelTop, panelBottom, edge, rule, cell, guide;
			float                radius;
			bool                 shadow;
			bool                 corners;  // (Pip-Boy brackets at the panel corners)
			float                scan;     // scanlines over panels: their strength
		};

		const Theme& ThemeNow()
		{
			static const Theme modern{ Style::kModern, { &Glyphs::kOswaldSmall, &Glyphs::kOswaldLarge }, { &Glyphs::kOswaldSmall, &Glyphs::kOswaldLarge },
				"hud_x_modern.png", Rgba(244, 238, 226), Rgba(196, 188, 174), Rgba(132, 124, 112), Rgba(255, 178, 64), Rgba(18, 14, 8),
				Rgba(26, 24, 28, 0.86f), Rgba(10, 9, 12, 0.90f), Rgba(255, 255, 255, 0.10f), Rgba(255, 255, 255, 0.10f), Rgba(0, 0, 0, 0.28f),
				Rgba(120, 255, 140), 8.0f, true, false, 0.0f };
			static const Theme pip{ Style::kPipBoy, { &Glyphs::kRobotoSmall, &Glyphs::kRobotoLarge }, { &Glyphs::kRobotoSmall, &Glyphs::kRobotoLarge },
				"hud_x_pipboy.png", Rgba(26, 255, 90), Rgba(26, 255, 90, 0.85f), Rgba(26, 255, 90, 0.55f), Rgba(26, 255, 90), Rgba(3, 26, 6),
				Rgba(0, 22, 5, 0.74f), Rgba(0, 10, 2, 0.64f), Rgba(26, 255, 90, 0.55f), Rgba(26, 255, 90, 0.16f), Rgba(0, 0, 0, 0.0f),
				Rgba(60, 255, 110), 0.0f, false, true, 0.10f };
			static const Theme vault{ Style::kVaultTec, { &Glyphs::kRobotoSmall, &Glyphs::kRobotoLarge }, { &Glyphs::kRobotoiSmall, &Glyphs::kRobotoiLarge },
				"hud_x_vaulttec.png", Rgba(255, 244, 214), Rgba(225, 216, 190), Rgba(160, 176, 205), Rgba(248, 198, 43), Rgba(18, 48, 95),
				Rgba(29, 76, 143), Rgba(18, 48, 95), Rgba(248, 198, 43), Rgba(248, 198, 43, 0.30f), Rgba(0, 0, 0, 0.22f), Rgba(255, 212, 60), 12.0f,
				true, false, 0.0f };
			static const Theme robco{ Style::kRobCo, { &Glyphs::kVt323Small, &Glyphs::kVt323Large }, { &Glyphs::kVt323Small, &Glyphs::kVt323Large },
				"hud_x_robco.png", Rgba(77, 255, 122), Rgba(77, 255, 122, 0.85f), Rgba(77, 255, 122, 0.5f), Rgba(77, 255, 122), Rgba(2, 18, 8),
				Rgba(0, 14, 5, 0.86f), Rgba(0, 8, 3, 0.86f), Rgba(77, 255, 122, 0.5f), Rgba(77, 255, 122, 0.22f), Rgba(0, 0, 0, 0.0f),
				Rgba(77, 255, 122), 0.0f, false, false, 0.22f };
			switch (std::clamp(Settings::Get().hudStyle, 0, 3)) {
			case 0: return modern;
			case 2: return vault;
			case 3: return robco;
			default: return pip;
			}
		}

		// the design screen mapped onto the real one
		struct View
		{
			ImDrawList*  dl{ nullptr };
			ImVec2       origin{};
			float        s{ 1.0f };  // pixels per design unit
			float        w{ 1280.0f };
			float        h{ 720.0f };
			float        time{ 0.0f };
			const Theme* th{ nullptr };

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

		ImTextureID Load(const char* a_file)
		{
			static std::unordered_map<std::string, Texture> textures;
			auto& tex = textures[a_file];
			if (!tex.tried) {
				tex.tried = true;
				wchar_t exe[MAX_PATH]{};
				::GetModuleFileNameW(nullptr, exe, MAX_PATH);
				const auto path = std::filesystem::path(exe).parent_path() / L"Data" / L"F4SE" / L"Plugins" / L"fo4bowling" / a_file;
				if (std::filesystem::exists(path)) {
					tex.id = F4SEMenuFramework::LoadTexture(path.string());
				}
				log::info("HUD texture {}: {}", a_file, tex.id ? "loaded" : "not loaded (falling back)");
			}
			return tex.id;
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
			const Glyphs::Atlas*                  atlas{ nullptr };
			std::array<const Glyphs::Glyph*, 256> map{};
		};
		const Face& FaceOf(const Glyphs::Atlas* a_atlas)
		{
			static std::unordered_map<const Glyphs::Atlas*, Face> faces;
			auto& f = faces[a_atlas];
			if (!f.atlas) {
				f.atlas = a_atlas;
				for (int i = 0; i < a_atlas->count; ++i) {
					const auto& g = a_atlas->glyphs[i];
					if (g.code < 256) {
						f.map[g.code] = &g;
					}
				}
			}
			return f;
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

		struct Type
		{
			const Face* face{ nullptr };  // (none: the framework's font)
			float       k{ 1.0f };         // design units per atlas pixel
			float       capTop{ 0.0f };    // the baseline, below the top of the capitals
		};

		// a_size is the height of the capitals over 0.83: every typeface's capitals stand the same at a size
		Type TypeFor(const View& a_v, float a_size, bool a_head)
		{
			const auto&  atl = a_head ? a_v.th->head : a_v.th->body;
			const auto*  atlas = a_size * a_v.s <= 34.0f ? atl[0] : atl[1];
			Type         t;
			if (!Load(atlas->file)) {
				return t;
			}
			t.face = &FaceOf(atlas);
			const float px = a_size * 0.83f / atlas->cap;  // (the size it's drawn at)
			t.k = px / atlas->px;
			t.capTop = atlas->cap * px;
			return t;
		}

		float TextWidth(const View& a_v, std::string_view a_text, float a_size, float a_track = 0.0f, bool a_head = false)
		{
			const auto t = TypeFor(a_v, a_size, a_head);
			if (t.face) {
				float w = 0.0f;
				int   n = 0;
				EachCode(a_text, [&](unsigned a_c) {
					if (const auto* g = t.face->map[a_c]) {
						w += g->advance * t.k;
						++n;
					}
				});
				return w + a_track * static_cast<float>(std::max(0, n - 1));
			}
			const std::string s(a_text);
			ImVec2            out{};
			CalcTextSize(&out, s.c_str(), nullptr, false, -1.0f);
			const float base = GetFontSize();
			return base > 0.0f ? out.x * a_size / base : out.x;
		}

		// a_y is the top of the capitals
		void Print(const View& a_v, std::string_view a_text, float a_x, float a_y, float a_size, ImU32 a_col, Align a_align = Align::kLeft,
			float a_track = 0.0f, bool a_head = false, bool a_shadow = true)
		{
			if (a_text.empty() || !(a_col >> 24)) {
				return;
			}
			const float w = TextWidth(a_v, a_text, a_size, a_track, a_head);
			const float x = a_align == Align::kLeft ? a_x : a_align == Align::kCentre ? a_x - w * 0.5f : a_x - w;
			const auto  t = TypeFor(a_v, a_size, a_head);
			const float alpha = static_cast<float>(a_col >> 24) / 255.0f;
			const Style st = a_v.th->style;
			if (t.face) {
				const auto* atlas = t.face->atlas;
				const float baseline = a_y + t.capTop;
				const float iw = 1.0f / static_cast<float>(atlas->width);
				const float ih = 1.0f / static_cast<float>(atlas->height);
				const auto  run = [&](float a_dx, float a_dy, ImU32 a_c) {
					float pen = x + a_dx;
					EachCode(a_text, [&](unsigned a_code) {
						const auto* g = t.face->map[a_code];
						if (!g) {
							return;
						}
						if (g->w > 1 && g->h > 1) {
							const float gx = pen + g->ox * t.k;
							const float gy = baseline + a_dy + g->oy * t.k;
							Image(a_v.dl, Load(atlas->file), a_v.P(gx, gy), a_v.P(gx + g->w * t.k, gy + g->h * t.k), { g->x * iw, g->y * ih },
								{ (g->x + g->w) * iw, (g->y + g->h) * ih }, a_c);
						}
						pen += g->advance * t.k + a_track;
					});
				};
				if (a_shadow) {
					if (st == Style::kPipBoy || st == Style::kRobCo) {
						// (glowing type: a soft halo of its own colour)
						const ImU32 halo = Fade(a_col, 0.22f);
						for (const auto& o : { std::pair{ -0.7f, 0.0f }, std::pair{ 0.7f, 0.0f }, std::pair{ 0.0f, -0.7f }, std::pair{ 0.0f, 0.7f } }) {
							run(o.first, o.second, halo);
						}
					} else {
						run(0.0f, std::max(1.0f, a_size * (st == Style::kVaultTec ? 0.09f : 0.06f)), Rgba(0, 0, 0, 0.55f * alpha));
					}
				}
				run(0.0f, 0.0f, a_col);
				return;
			}
			// (no textures: the framework's own font)
			const std::string s(a_text);
			auto*             font = GetFont();
			if (a_shadow) {
				DL::AddText(a_v.dl, font, a_size * a_v.s, a_v.P(x + 1.2f, a_y + 1.2f), Rgba(0, 0, 0, 0.55f * alpha), s.c_str());
			}
			DL::AddText(a_v.dl, font, a_size * a_v.s, a_v.P(x, a_y), a_col, s.c_str());
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
		void Line(const View& a_v, float a_x0, float a_y0, float a_x1, float a_y1, ImU32 a_col, float a_thick = 1.0f)
		{
			DL::AddLine(a_v.dl, a_v.P(a_x0, a_y0), a_v.P(a_x1, a_y1), a_col, a_thick * a_v.s);
		}

		void Shadow(const View& a_v, float a_x, float a_y, float a_w, float a_h, float a_alpha, float a_round)
		{
			for (int i = 6; i >= 1; --i) {
				const float g = static_cast<float>(i) * 2.2f;
				Fill(a_v, a_x - g, a_y - g + 4.0f, a_w + 2.0f * g, a_h + 2.0f * g, Rgba(0, 0, 0, 0.055f * a_alpha), a_round + g);
			}
		}

		void Scanlines(const View& a_v, float a_x, float a_y, float a_w, float a_h, float a_alpha)
		{
			const float strength = a_v.th->scan * a_alpha;
			if (strength <= 0.0f) {
				return;
			}
			const float step = 3.0f / a_v.s;  // (every third pixel)
			for (float y = a_y + step; y < a_y + a_h; y += step) {
				Line(a_v, a_x, y, a_x + a_w, y, Rgba(0, 0, 0, strength), 1.0f / a_v.s);
			}
		}

		// a panel in the current look
		void Panel(const View& a_v, float a_x, float a_y, float a_w, float a_h, float a_alpha = 1.0f)
		{
			const auto& th = *a_v.th;
			const float r = th.radius;
			if (th.shadow) {
				Shadow(a_v, a_x, a_y, a_w, a_h, a_alpha, r);
			}
			switch (th.style) {
			case Style::kVaultTec:
				Fill(a_v, a_x, a_y + 5.0f, a_w, a_h, Rgba(0, 0, 0, 0.35f * a_alpha), r);  // (the chunky drop under it)
				Fill(a_v, a_x, a_y, a_w, a_h, Fade(th.panelBottom, a_alpha), r);
				Gradient(a_v.dl, a_v.P(a_x + r * 0.4f, a_y + 2.0f), a_v.P(a_x + a_w - r * 0.4f, a_y + a_h * 0.6f), Fade(th.panelTop, a_alpha), Fade(th.panelTop, a_alpha),
					Fade(th.panelBottom, a_alpha), Fade(th.panelBottom, a_alpha));
				Outline(a_v, a_x, a_y, a_w, a_h, Fade(th.edge, a_alpha), r, 3.0f);
				break;
			case Style::kModern:
				Fill(a_v, a_x, a_y, a_w, a_h, Fade(th.panelBottom, a_alpha), r);
				Gradient(a_v.dl, a_v.P(a_x + r * 0.3f, a_y), a_v.P(a_x + a_w - r * 0.3f, a_y + a_h * 0.55f), Fade(th.panelTop, a_alpha), Fade(th.panelTop, a_alpha),
					Fade(th.panelTop & 0x00FFFFFFu, a_alpha), Fade(th.panelTop & 0x00FFFFFFu, a_alpha));
				Outline(a_v, a_x, a_y, a_w, a_h, Fade(th.edge, a_alpha), r);
				Line(a_v, a_x + r, a_y + 0.5f, a_x + a_w - r, a_y + 0.5f, Rgba(255, 255, 255, 0.16f * a_alpha));
				break;
			default:
				Gradient(a_v.dl, a_v.P(a_x, a_y), a_v.P(a_x + a_w, a_y + a_h), Fade(th.panelTop, a_alpha), Fade(th.panelTop, a_alpha), Fade(th.panelBottom, a_alpha),
					Fade(th.panelBottom, a_alpha));
				Outline(a_v, a_x, a_y, a_w, a_h, Fade(th.edge, a_alpha), 0.0f, th.style == Style::kRobCo ? 2.0f : 1.0f);
				Scanlines(a_v, a_x, a_y, a_w, a_h, a_alpha);
				break;
			}
			if (th.corners) {
				// the Pip-Boy's brackets, top left and bottom right
				const ImU32 c = Fade(th.accent, a_alpha);
				const float l = 14.0f, o = 4.0f;
				Line(a_v, a_x - o, a_y - o, a_x - o + l, a_y - o, c, 2.0f);
				Line(a_v, a_x - o, a_y - o, a_x - o, a_y - o + l, c, 2.0f);
				Line(a_v, a_x + a_w + o, a_y + a_h + o, a_x + a_w + o - l, a_y + a_h + o, c, 2.0f);
				Line(a_v, a_x + a_w + o, a_y + a_h + o, a_x + a_w + o, a_y + a_h + o - l, c, 2.0f);
			}
		}

		// a key: a keycap, a green outline, a yellow cap or [BRACKETS], by look. Returns its width
		float Key(const View& a_v, std::string_view a_key, float a_x, float a_y, float a_size, float a_alpha = 1.0f)
		{
			const auto& th = *a_v.th;
			if (th.style == Style::kRobCo) {
				const std::string t = "[" + std::string(a_key) + "]";
				Print(a_v, t, a_x, a_y + a_size * 0.25f, a_size, Fade(th.text, a_alpha), Align::kLeft);
				return TextWidth(a_v, t, a_size);
			}
			const float tw = TextWidth(a_v, a_key, a_size, 0.6f);
			const float w = std::max(a_size * 1.5f, tw + a_size * 0.9f);
			const float h = a_size * 1.55f;
			switch (th.style) {
			case Style::kPipBoy:
				Fill(a_v, a_x, a_y, w, h, Rgba(0, 18, 4, 0.6f * a_alpha));
				Outline(a_v, a_x, a_y, w, h, Fade(th.accent, a_alpha));
				Print(a_v, a_key, a_x + w * 0.5f, a_y + (h - a_size) * 0.5f, a_size, Fade(th.text, a_alpha), Align::kCentre, 0.6f);
				break;
			case Style::kVaultTec:
				Fill(a_v, a_x, a_y + 2.0f, w, h, Fade(Rgba(155, 116, 16), a_alpha), 5.0f);
				Fill(a_v, a_x, a_y, w, h, Fade(th.accent, a_alpha), 5.0f);
				Print(a_v, a_key, a_x + w * 0.5f, a_y + (h - a_size) * 0.5f, a_size, Fade(th.ink, a_alpha), Align::kCentre, 0.6f, true, false);
				break;
			default:
				Fill(a_v, a_x, a_y + 2.0f, w, h, Fade(Rgba(0, 0, 0, 0.5f), a_alpha), 4.0f);
				Gradient(a_v.dl, a_v.P(a_x, a_y), a_v.P(a_x + w, a_y + h), Fade(Rgba(250, 244, 232), a_alpha), Fade(Rgba(250, 244, 232), a_alpha),
					Fade(Rgba(206, 198, 184), a_alpha), Fade(Rgba(206, 198, 184), a_alpha));
				Outline(a_v, a_x, a_y, w, h, Fade(Rgba(255, 255, 255, 0.6f), a_alpha), 4.0f);
				Print(a_v, a_key, a_x + w * 0.5f, a_y + (h - a_size) * 0.5f, a_size, Fade(th.ink, a_alpha), Align::kCentre, 0.6f, false, false);
				break;
			}
			return w;
		}

		// ------------------------------------------------------------ the pieces

		// at a ball return: F) START BOWLING beside the crosshair, then how many play and the lane, then who you'd play
		void Prompt(const View& a_v, const Game::HudState& a_st)
		{
			const auto& th = *a_v.th;
			const float x = a_v.w * 0.5f + 42.0f;
			const float y = a_v.h * 0.5f + 18.0f;
			std::string key = "F";
			std::string action = a_st.prompt;
			if (const auto p = a_st.prompt.find(") "); p != std::string::npos) {
				key = a_st.prompt.substr(0, p);
				action = a_st.prompt.substr(p + 2);
			}
			const float lines = std::max(TextWidth(a_v, a_st.promptSub, 13.0f, 0.8f), TextWidth(a_v, a_st.promptVs, 13.0f, 0.8f));
			const float h = a_st.promptVs.empty() ? 76.0f : 96.0f;

			if (th.style == Style::kPipBoy) {
				// Fallout 4's own activation prompt: a bracket line down the left, "F) START BOWLING" in green
				const float w = std::max(TextWidth(a_v, a_st.prompt, 22.0f, 0.6f), lines) + 40.0f;
				Gradient(a_v.dl, a_v.P(x - 14.0f, y - 12.0f), a_v.P(x - 14.0f + w, y - 12.0f + h), Rgba(0, 18, 4, 0.6f), Rgba(0, 18, 4, 0.0f), Rgba(0, 18, 4, 0.0f),
					Rgba(0, 18, 4, 0.6f));
				Line(a_v, x - 14.0f, y - 12.0f, x - 14.0f, y - 12.0f + h, th.accent, 2.0f);
				Line(a_v, x - 14.0f, y - 12.0f, x - 4.0f, y - 12.0f, th.accent, 2.0f);
				Line(a_v, x - 14.0f, y - 12.0f + h, x - 4.0f, y - 12.0f + h, th.accent, 2.0f);
				Print(a_v, a_st.prompt, x, y, 22.0f, th.text, Align::kLeft, 0.6f);
				Print(a_v, a_st.promptSub, x, y + 34.0f, 13.0f, th.soft, Align::kLeft, 0.8f);
				Print(a_v, a_st.promptVs, x, y + 54.0f, 13.0f, th.text, Align::kLeft, 0.8f);
				return;
			}
			if (th.style == Style::kRobCo) {
				const std::string line = "> " + a_st.prompt;
				const float       w = std::max(TextWidth(a_v, line, 22.0f) + 20.0f, lines) + 30.0f;
				Panel(a_v, x - 14.0f, y - 12.0f, w, h);
				Print(a_v, line, x, y, 22.0f, th.text);
				if (std::fmod(a_v.time, 1.0f) < 0.55f) {
					const float cx = x + TextWidth(a_v, line, 22.0f) + 4.0f;
					Fill(a_v, cx, y - 1.0f, 10.0f, 22.0f, th.text);
				}
				Print(a_v, a_st.promptSub, x, y + 34.0f, 13.0f, th.soft);
				Print(a_v, a_st.promptVs, x, y + 54.0f, 13.0f, th.text);
				return;
			}
			const float kw = TextWidth(a_v, key, 15.0f, 0.6f);
			const float cap = std::max(15.0f * 1.5f, kw + 15.0f * 0.9f);
			const float aw = TextWidth(a_v, action, 22.0f, 1.2f, true);
			const float w = std::max(cap + 14.0f + aw, lines) + 36.0f;
			Panel(a_v, x - 14.0f, y - 14.0f, w, h);
			if (th.style == Style::kModern) {
				Fill(a_v, x - 14.0f, y - 14.0f, 3.0f, h, th.accent, 1.5f);
			}
			Key(a_v, key, x + 4.0f, y - 1.0f, 15.0f);
			Print(a_v, action, x + 4.0f + cap + 12.0f, y, 22.0f, th.style == Style::kVaultTec ? th.accent : th.text, Align::kLeft, 1.2f, true);
			Print(a_v, a_st.promptSub, x + 4.0f, y + 34.0f, 13.0f, th.soft, Align::kLeft, 0.8f);
			Print(a_v, a_st.promptVs, x + 4.0f, y + 54.0f, 13.0f, th.accent, Align::kLeft, 0.8f);
		}

		// the score sheet across the top: a row per player, ten frames each, and the totals
		void Sheet(const View& a_v, const Game::HudState& a_st)
		{
			const auto&     th = *a_v.th;
			const bool      robco = th.style == Style::kRobCo;
			constexpr float kName = 150.0f;
			constexpr float kFrame = 48.0f;
			constexpr float kTenth = 66.0f;
			constexpr float kTotal = 76.0f;
			constexpr float kRow = 46.0f;
			constexpr float kMark = 15.0f;
			constexpr float kW = kName + 9.0f * kFrame + kTenth + kTotal;
			const float     kHead = robco ? 46.0f : 20.0f;
			const int       players = static_cast<int>(a_st.cards.size());
			const float     x0 = (a_v.w - kW) * 0.5f;
			const float     y0 = 14.0f;
			const float     ph = kHead + kRow * static_cast<float>(players) + 8.0f;
			Panel(a_v, x0 - 10.0f, y0 - 6.0f, kW + 20.0f, ph + 6.0f);

			// the heading: the lane on the left, frame numbers over their columns, TOTAL
			float hy = y0 + 4.0f;
			if (robco) {
				Print(a_v, std::format("> ROBCO LANE MONITOR // LANE {:02}", a_st.lane), x0, y0 + 2.0f, 15.0f, th.text);
				Print(a_v, a_st.over ? std::string("GAME OVER") : std::string("GAME IN PROGRESS"), x0 + kW, y0 + 2.0f, 15.0f, th.soft, Align::kRight);
				for (float dx = 0.0f; dx < kW; dx += 9.0f) {
					Line(a_v, x0 + dx, y0 + 22.0f, x0 + dx + 5.0f, y0 + 22.0f, th.edge);
				}
				hy = y0 + 28.0f;
			} else if (th.style == Style::kPipBoy || th.style == Style::kVaultTec) {
				// a solid bar across the top, its writing in ink
				const float top = y0 - 6.0f;
				if (th.style == Style::kVaultTec) {
					Fill(a_v, x0 - 10.0f, top, kW + 20.0f, kHead + 4.0f, th.accent, th.radius);
					Fill(a_v, x0 - 10.0f, top + th.radius, kW + 20.0f, kHead + 4.0f - th.radius, th.accent);
				} else {
					Fill(a_v, x0 - 10.0f, top, kW + 20.0f, kHead + 4.0f, th.accent);
				}
				hy = y0;
			}
			const bool  bar = th.style == Style::kPipBoy || th.style == Style::kVaultTec;
			const ImU32 headCol = bar ? th.ink : th.dim;
			const float hs = robco ? 13.0f : 10.0f;
			if (!robco) {
				Print(a_v, std::format("LANE {}", a_st.lane), x0 + 6.0f, hy, hs, bar ? th.ink : th.accent, Align::kLeft, 2.0f, true, false);
			} else {
				Print(a_v, "BOWLER", x0, hy, hs, headCol);
			}
			float hx = x0 + kName;
			for (int f = 0; f < bowl::Card::kFrames; ++f) {
				const float fw = f == 9 ? kTenth : kFrame;
				Print(a_v, robco ? std::format("{:02}", f + 1) : std::to_string(f + 1), hx + fw * 0.5f, hy, hs, headCol, Align::kCentre, 0.0f, true, false);
				hx += fw;
			}
			Print(a_v, "TOTAL", hx + kTotal * 0.5f, hy, hs, headCol, Align::kCentre, robco ? 0.0f : 2.0f, true, false);

			float y = y0 + kHead + (bar ? 2.0f : 0.0f);
			for (int p = 0; p < players; ++p) {
				const auto& card = a_st.cards[static_cast<std::size_t>(p)];
				const bool  up = p == a_st.bowler && !a_st.over;
				const bool  inverse = up && robco;  // (RobCo: whoever's up is a lit bar, written in ink)
				const ImU32 ink = inverse ? th.ink : th.text;
				const ImU32 inkSoft = inverse ? th.ink : th.soft;
				if (inverse) {
					Fill(a_v, x0 - 4.0f, y, kW + 8.0f, kRow - 4.0f, th.accent);
				} else if (up) {
					const ImU32 a0 = Fade(th.accent, th.style == Style::kVaultTec ? 0.16f : 0.22f);
					const ImU32 a1 = Fade(th.accent, 0.03f);
					Gradient(a_v.dl, a_v.P(x0 - 10.0f, y), a_v.P(x0 + kW + 10.0f, y + kRow - 4.0f), a0, a1, a1, a0);
					if (th.style == Style::kModern) {
						Fill(a_v, x0 - 10.0f, y, 3.0f, kRow - 4.0f, th.accent);
					}
				} else if (p % 2 == 1 && th.style == Style::kModern) {
					Fill(a_v, x0 - 10.0f, y, kW + 20.0f, kRow - 4.0f, Rgba(255, 255, 255, 0.07f));
				}
				if (!robco && p > 0) {
					Line(a_v, x0 - 10.0f, y - 2.0f, x0 + kW + 10.0f, y - 2.0f, th.rule, th.style == Style::kVaultTec ? 2.0f : 1.0f);
				}
				std::string name = p < static_cast<int>(a_st.names.size()) ? a_st.names[static_cast<std::size_t>(p)] : std::string("?");
				while (name.size() > 3 && TextWidth(a_v, name, 17.0f, 0.6f, true) > kName - 30.0f) {
					name = name.substr(0, name.size() - 2) + ".";
				}
				float nx = x0 + 6.0f;
				if (up && th.style == Style::kPipBoy) {
					DL::AddTriangleFilled(a_v.dl, a_v.P(nx, y + 14.0f), a_v.P(nx + 8.0f, y + 20.5f), a_v.P(nx, y + 27.0f), th.accent);
					nx += 14.0f;
				}
				const ImU32 nameCol = inverse ? th.ink : (up && th.style == Style::kVaultTec) ? th.accent : up ? th.text : th.soft;
				Print(a_v, name, nx, y + 13.0f, 17.0f, nameCol, Align::kLeft, 0.6f, true, !inverse);

				float     x = x0 + kName;
				const int current = card.Done() ? -1 : card.Frame();
				for (int f = 0; f < bowl::Card::kFrames; ++f) {
					const float fw = f == 9 ? kTenth : kFrame;
					const bool  here = up && f == current;
					const float cx = x + 1.0f;
					const float cw = fw - 2.0f;
					const float ch = kRow - 6.0f;
					const auto  marks = card.Marks(f);
					const int   boxes = f == 9 ? 3 : 2;
					if (robco) {
						// a terminal writes its marks: "X ·", "7 /", "9 -"
						std::string m;
						for (int k = 0; k < boxes; ++k) {
							const auto& mk = marks[static_cast<std::size_t>(k)];
							m += (k ? " " : "") + (mk.empty() ? std::string(".") : mk);
						}
						Print(a_v, m, cx + cw * 0.5f, y + 2.0f, 11.0f, inverse ? th.ink : th.soft, Align::kCentre, 0.0f, false, !inverse);
					} else {
						if (th.cell >> 24) {
							Fill(a_v, cx, y, cw, ch, here ? Fade(th.accent, 0.16f) : th.cell, th.style == Style::kModern ? 3.0f : 0.0f);
						} else if (here) {
							Fill(a_v, cx, y, cw, ch, Fade(th.accent, 0.16f));
						}
						if (f > 0) {
							Line(a_v, x, y, x, y + ch + 2.0f, th.rule, th.style == Style::kVaultTec ? 2.0f : 1.0f);
						}
						for (int k = 0; k < boxes; ++k) {
							const float mx = cx + cw - static_cast<float>(boxes - k) * kMark;
							const float my = y;
							const auto& m = marks[static_cast<std::size_t>(k)];
							if (th.style == Style::kVaultTec) {
								Fill(a_v, mx, my, kMark, kMark, Rgba(0, 0, 0, 0.25f));
							}
							Line(a_v, mx, my, mx, my + kMark, th.rule);
							Line(a_v, mx, my + kMark, mx + kMark, my + kMark, th.rule);
							if (m == "X") {
								Fill(a_v, mx + 1.5f, my + 1.5f, kMark - 3.0f, kMark - 3.0f, th.accent, th.style == Style::kModern ? 2.0f : 0.0f);
								Print(a_v, "X", mx + kMark * 0.5f, my + 3.2f, 9.5f, th.ink, Align::kCentre, 0.0f, true, false);
							} else if (m == "/") {
								DL::AddTriangleFilled(a_v.dl, a_v.P(mx + 1.5f, my + kMark - 1.5f), a_v.P(mx + kMark - 1.5f, my + 1.5f),
									a_v.P(mx + kMark - 1.5f, my + kMark - 1.5f), th.accent);
							} else if (!m.empty()) {
								Print(a_v, m, mx + kMark * 0.5f, my + 3.2f, 9.5f, th.soft, Align::kCentre, 0.0f, false, false);
							}
						}
						if (here) {
							Outline(a_v, cx, y, cw, ch, th.accent, th.style == Style::kModern ? 3.0f : 0.0f, th.style == Style::kVaultTec ? 2.0f : 1.5f);
						}
					}
					const int total = card.Total(f);
					if (total >= 0) {
						Print(a_v, std::to_string(total), cx + cw * 0.5f, y + kMark + (robco ? 4.0f : 6.0f), robco ? 17.0f : 15.0f, ink, Align::kCentre,
							0.4f, th.style == Style::kVaultTec, !inverse);
					}
					x += fw;
				}
				// the total so far, big, and the most it could still be
				if (!robco) {
					Line(a_v, x, y, x, y + kRow - 4.0f, th.style == Style::kPipBoy ? th.edge : th.rule, th.style == Style::kVaultTec ? 2.0f : 1.0f);
				}
				const ImU32 totCol = inverse ? th.ink : (th.style == Style::kVaultTec || (up && th.style == Style::kModern)) ? th.accent : th.text;
				Print(a_v, robco ? std::format("{:03}", card.Total()) : std::to_string(card.Total()), x + kTotal * 0.5f, y + 5.0f, 24.0f, totCol,
					Align::kCentre, 0.5f, true, !inverse);
				if (!card.Done() && !robco) {
					Print(a_v, std::format("MAX {}", card.Best()), x + kTotal * 0.5f, y + 32.0f, 8.0f, th.dim, Align::kCentre, 1.2f, false, false);
				}
				(void)inkSoft;
				y += kRow;
			}
		}

		// the controls along the bottom: "MOUSE: AIM   A/D: MOVE ..." as keys and labels
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
				const std::size_t to = a_keys.find("   ", from);
				const std::string seg = a_keys.substr(from, to == std::string::npos ? std::string::npos : to - from);
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
			const auto&     th = *a_v.th;
			constexpr float kSize = 10.0f;
			const auto      keyWidth = [&](const std::string& a_key) {
                if (a_key.empty()) {
                    return 0.0f;
                }
                if (th.style == Style::kRobCo) {
                    return TextWidth(a_v, "[" + a_key + "]", kSize) + 6.0f;
                }
                return std::max(kSize * 1.5f, TextWidth(a_v, a_key, kSize, 0.6f) + kSize * 0.9f) + 7.0f;
			};
			float total = 0.0f;
			for (const auto& p : parts) {
				total += keyWidth(p.key) + TextWidth(a_v, p.label, kSize, 1.0f) + 22.0f;
			}
			float x = (a_v.w - total + 22.0f) * 0.5f;
			for (const auto& p : parts) {
				if (!p.key.empty()) {
					Key(a_v, p.key, x, a_y - 2.0f, kSize);
					x += keyWidth(p.key);
				}
				Print(a_v, p.label, x, a_y + 2.0f, kSize, th.style == Style::kVaultTec ? th.text : th.soft, Align::kLeft, 1.0f);
				x += TextWidth(a_v, p.label, kSize, 1.0f) + 22.0f;
			}
		}

		// bottom middle while aiming: the hook, the power, the board you're on
		void Meters(const View& a_v, const Game::HudState& a_st)
		{
			const auto&       th = *a_v.th;
			const float       cx = a_v.w * 0.5f;
			const float       k = a_st.maxHook > 0.0f ? std::clamp(a_st.hook / a_st.maxHook, -1.0f, 1.0f) : 0.0f;
			const float       power = std::clamp(a_st.power, 0.0f, 1.0f);
			const bool        charging = a_st.charging && a_st.power > 0.001f;
			const std::string hook = std::abs(a_st.hook) < 0.5f ? std::string("STRAIGHT") :
			                         std::format("{} {:.0f}%", a_st.hook > 0.0f ? "LEFT" : "RIGHT", std::abs(a_st.hook));
			const std::string right = charging ? std::format("{:.0f}%", power * 100.0f) : std::format("BOARD {}", a_st.board);

			if (th.style == Style::kRobCo) {
				// text meters: a terminal's idea of a gauge
				const float pw = 520.0f, y = a_v.h - 132.0f;
				Panel(a_v, cx - pw * 0.5f, y, pw, 92.0f);
				constexpr int kCells = 25;
				const int     at = static_cast<int>(std::round((1.0f - k) * 0.5f * static_cast<float>(kCells - 1)));
				std::string   strip;
				for (int i = 0; i < kCells; ++i) {
					strip += i == at ? '*' : i == kCells / 2 ? '|' : '.';
				}
				std::string bar;
				const int   filled = charging ? static_cast<int>(std::round(power * 24.0f)) : 0;
				for (int i = 0; i < 24; ++i) {
					bar += i < filled ? '#' : '-';
				}
				const float lx = cx - pw * 0.5f + 16.0f;
				Print(a_v, "HOOK  [R " + strip + " L]  " + hook, lx, y + 10.0f, 16.0f, th.text);
				Print(a_v, "POWER [" + bar + "]  " + (charging ? right : std::string("--")), lx, y + 36.0f, 16.0f, th.text);
				const std::string ready = std::format("BOARD {:02} / 39        > {}", a_st.board, charging ? "RELEASE" : "READY");
				Print(a_v, ready, lx, y + 62.0f, 16.0f, th.soft);
				if (std::fmod(a_v.time, 1.0f) < 0.55f) {
					Fill(a_v, lx + TextWidth(a_v, ready, 16.0f) + 4.0f, y + 61.0f, 9.0f, 17.0f, th.text);
				}
				Keys(a_v, a_st.keys, a_v.h - 30.0f);
				return;
			}

			const float pw = 470.0f;
			const float y = a_v.h - 122.0f;
			const float tx0 = cx - 120.0f;
			const float tx1 = cx + 150.0f;
			const float tm = (tx0 + tx1) * 0.5f;
			const float hy = y + 22.0f;
			const float py = y + 46.0f;
			const float nx = tm - k * (tx1 - tx0) * 0.5f;
			const float lx = cx - pw * 0.5f + 18.0f;
			const float rx = cx + pw * 0.5f - 18.0f;
			if (th.style != Style::kPipBoy) {
				Panel(a_v, cx - pw * 0.5f, y, pw, 78.0f);
			} else {
				// (the Pip-Boy's bars sit on a faint wash, no box)
				Gradient(a_v.dl, a_v.P(cx - pw * 0.5f, y - 4.0f), a_v.P(cx + pw * 0.5f, y + 84.0f), Rgba(0, 14, 3, 0.0f), Rgba(0, 14, 3, 0.0f), Rgba(0, 14, 3, 0.55f),
					Rgba(0, 14, 3, 0.55f));
			}
			const float lab = 10.0f;
			Print(a_v, "HOOK", lx, hy - 5.0f, lab, th.style == Style::kPipBoy ? th.text : th.dim, Align::kLeft, 2.0f, true, th.style == Style::kPipBoy);
			Print(a_v, hook, rx, hy - 5.0f, lab, th.style == Style::kVaultTec ? th.accent : th.text, Align::kRight, 1.2f, false, th.style == Style::kPipBoy);
			Print(a_v, "POWER", lx, py + 1.0f, lab, th.style == Style::kPipBoy ? th.text : th.dim, Align::kLeft, 2.0f, true, th.style == Style::kPipBoy);
			Print(a_v, right, rx, py + 1.0f, lab, th.style == Style::kVaultTec ? th.accent : charging ? th.text : th.soft, Align::kRight, 1.2f, false,
				th.style == Style::kPipBoy);

			switch (th.style) {
			case Style::kPipBoy:
				{
					// hook: a compass strip, ticks along its foot, a pointer underneath
					Fill(a_v, tx0, hy - 9.0f, tx1 - tx0, 16.0f, Rgba(0, 15, 3, 0.45f));
					Line(a_v, tx0, hy + 7.0f, tx1, hy + 7.0f, th.accent, 2.0f);
					for (int t = -8; t <= 8; ++t) {
						const float x = tm + static_cast<float>(t) * (tx1 - tx0) / 16.0f;
						Line(a_v, x, hy + 7.0f, x, hy + (t == 0 ? -6.0f : t % 2 == 0 ? 0.0f : 3.0f), t == 0 ? th.accent : th.dim, t == 0 ? 2.0f : 1.0f);
					}
					Print(a_v, "R", tx0 + 4.0f, hy - 7.0f, 7.0f, th.dim, Align::kLeft, 0.0f, false, false);
					Print(a_v, "L", tx1 - 4.0f, hy - 7.0f, 7.0f, th.dim, Align::kRight, 0.0f, false, false);
					DL::AddTriangleFilled(a_v.dl, a_v.P(nx, hy + 1.0f), a_v.P(nx + 7.0f, hy + 13.0f), a_v.P(nx - 7.0f, hy + 13.0f), th.accent);
					// power: the AP bar, in segments
					Outline(a_v, tx0, py - 1.0f, tx1 - tx0, 14.0f, th.accent);
					const int   kSeg = 30;
					const float sw = (tx1 - tx0 - 4.0f) / static_cast<float>(kSeg);
					const int   lit = charging ? static_cast<int>(std::ceil(power * static_cast<float>(kSeg))) : 0;
					for (int i = 0; i < kSeg; ++i) {
						Fill(a_v, tx0 + 2.0f + static_cast<float>(i) * sw, py + 1.0f, sw - 2.0f, 10.0f, i < lit ? th.accent : Rgba(26, 255, 90, 0.07f));
					}
					break;
				}
			case Style::kVaultTec:
				{
					// hook: a dial track with a big brass knob
					Fill(a_v, tx0, hy - 6.0f, tx1 - tx0, 12.0f, Rgba(0, 0, 0, 0.35f), 6.0f);
					Line(a_v, tm, hy - 4.0f, tm, hy + 4.0f, Rgba(255, 255, 255, 0.5f), 2.0f);
					DL::AddCircleFilled(a_v.dl, a_v.P(nx, hy + 2.0f), 11.0f * a_v.s, Rgba(0, 0, 0, 0.4f), 28);
					DL::AddCircleFilled(a_v.dl, a_v.P(nx, hy), 11.0f * a_v.s, th.text, 28);
					DL::AddCircleFilled(a_v.dl, a_v.P(nx, hy), 9.0f * a_v.s, th.accent, 28);
					DL::AddCircleFilled(a_v.dl, a_v.P(nx - 3.0f, hy - 3.0f), 3.0f * a_v.s, Rgba(255, 238, 170), 16);
					// power: a fat pill filling yellow
					Fill(a_v, tx0, py - 1.0f, tx1 - tx0, 14.0f, Rgba(0, 0, 0, 0.35f), 7.0f);
					if (charging) {
						const float fx = tx0 + (tx1 - tx0) * power;
						Fill(a_v, tx0, py - 1.0f, std::max(14.0f, fx - tx0), 14.0f, th.accent, 7.0f);
						Fill(a_v, tx0 + 4.0f, py + 1.0f, std::max(6.0f, fx - tx0 - 8.0f), 4.0f, Rgba(255, 238, 170, 0.8f), 2.0f);
					}
					break;
				}
			default:
				{
					Fill(a_v, tx0, hy - 2.0f, tx1 - tx0, 4.0f, Rgba(255, 255, 255, 0.10f), 2.0f);
					for (int t = -4; t <= 4; ++t) {
						const float x = tm + static_cast<float>(t) * (tx1 - tx0) / 8.0f;
						Line(a_v, x, hy + 5.0f, x, hy + (t == 0 ? 11.0f : 8.0f), t == 0 ? th.soft : th.rule);
					}
					Fill(a_v, std::min(nx, tm), hy - 2.0f, std::abs(nx - tm), 4.0f, th.accent, 2.0f);
					DL::AddCircleFilled(a_v.dl, a_v.P(nx, hy), 9.0f * a_v.s, Fade(th.accent, 0.22f), 24);
					DL::AddCircleFilled(a_v.dl, a_v.P(nx, hy), 5.5f * a_v.s, th.accent, 24);
					DL::AddCircleFilled(a_v.dl, a_v.P(nx, hy), 2.2f * a_v.s, Rgba(255, 240, 210), 16);
					Fill(a_v, tx0, py, tx1 - tx0, 12.0f, Rgba(255, 255, 255, 0.08f), 3.0f);
					if (charging) {
						const float fx = tx0 + (tx1 - tx0) * power;
						const ImU32 c0 = Rgba(120, 220, 140), c1 = th.accent, c2 = Rgba(255, 92, 70);
						const ImU32 tip = power < 0.6f ? Lerp(c0, c1, power / 0.6f) : Lerp(c1, c2, (power - 0.6f) / 0.4f);
						const float mid = tx0 + (tx1 - tx0) * 0.6f;
						Gradient(a_v.dl, a_v.P(tx0, py), a_v.P(std::min(fx, mid), py + 12.0f), c0, power < 0.6f ? tip : c1, power < 0.6f ? tip : c1, c0);
						if (fx > mid) {
							Gradient(a_v.dl, a_v.P(mid, py), a_v.P(fx, py + 12.0f), c1, tip, tip, c1);
						}
						Fill(a_v, fx - 1.5f, py - 3.0f, 3.0f, 18.0f, Rgba(255, 248, 230), 1.5f);
					}
					break;
				}
			}
			Keys(a_v, a_st.keys, a_v.h - 30.0f);
		}

		// the aim line, in the look's colour, fading as it goes
		void Guide(ImGuiViewport* a_viewport, const View& a_v, const Game::HudState& a_st)
		{
			if (a_st.guide.size() < 2) {
				return;
			}
			const auto at = [&](const std::array<float, 2>& a_p) {
				return ImVec2{ a_viewport->Pos.x + a_p[0] * a_viewport->Size.x, a_viewport->Pos.y + a_p[1] * a_viewport->Size.y };
			};
			const float       scale = std::max(0.1f, a_viewport->Size.y / 720.0f);
			const ImU32       c = a_v.th->guide;
			const std::size_t n = a_st.guide.size();
			for (std::size_t i = 1; i < n; ++i) {
				const float  fade = 1.0f - static_cast<float>(i) / static_cast<float>(n);
				const ImVec2 a = at(a_st.guide[i - 1]);
				const ImVec2 b = at(a_st.guide[i]);
				DL::AddLine(a_v.dl, a, b, Fade(c, 0.25f * fade), 7.0f * scale);  // (a soft glow under it)
				DL::AddLine(a_v.dl, a, b, Fade(c, 0.95f * fade + 0.05f), 2.5f * scale);
			}
		}

		// the calls in the middle of the screen: the strike X, GAME OVER
		void Banner(const View& a_v, const Game::HudState& a_st)
		{
			const auto& th = *a_v.th;
			const float a = a_st.bannerAlpha;
			if (a <= 0.0f || a_st.banner.empty()) {
				return;
			}
			const float cx = a_v.w * 0.5f;
			const float t = a_st.bannerAge;

			if (a_st.banner == "X") {
				const float in = Ease(t / 0.28f);
				float       size = 190.0f * (1.0f + 0.55f * (1.0f - in));
				float       alpha = a * std::min(1.0f, in * 1.6f);
				const float cy = a_v.h * 0.34f;
				switch (th.style) {
				case Style::kRobCo:
					size = 190.0f;
					alpha = a * ((std::fmod(t * 17.0f, 1.0f) < 0.15f && t < 0.6f) ? 0.35f : 1.0f);  // (it flickers on)
					Panel(a_v, cx - 120.0f, cy - 112.0f, 240.0f, 250.0f, a);
					break;
				case Style::kVaultTec:
					size = 230.0f * (1.0f + 0.35f * std::sin(std::min(1.0f, t / 0.35f) * 3.1415927f) * (1.0f - std::min(1.0f, t / 0.35f)) + 0.25f * (1.0f - in));
					break;
				case Style::kPipBoy:
					alpha *= 0.88f + 0.12f * std::sin(t * 9.0f);
					break;
				default:
					DL::AddCircleFilled(a_v.dl, a_v.P(cx, cy), size * 0.42f * a_v.s, Rgba(0, 0, 0, 0.35f * a), 48);
					alpha *= 0.85f + 0.15f * std::sin(t * 9.0f);
					break;
				}
				if (auto* tex = Load(th.xFile)) {
					Image(a_v.dl, tex, a_v.P(cx - size * 0.5f, cy - size * 0.5f), a_v.P(cx + size * 0.5f, cy + size * 0.5f), { 0.0f, 0.0f }, { 1.0f, 1.0f },
						Rgba(255, 255, 255, alpha));
				} else {
					Print(a_v, "X", cx, cy - size * 0.3f, size * 0.6f, Fade(th.accent, a), Align::kCentre);
				}
				if (!a_st.bannerSub.empty()) {
					const std::string who = th.style == Style::kRobCo ? "> STRIKE // " + a_st.bannerSub : a_st.bannerSub;
					const float       wy = cy + (th.style == Style::kRobCo ? 100.0f : 82.0f);
					if (th.style == Style::kVaultTec) {
						const float ww = TextWidth(a_v, who, 16.0f, 3.0f, true) + 30.0f;
						Fill(a_v, cx - ww * 0.5f, wy - 6.0f, ww, 30.0f, Fade(th.panelBottom, a), 8.0f);
						Outline(a_v, cx - ww * 0.5f, wy - 6.0f, ww, 30.0f, Fade(th.accent, a), 8.0f, 3.0f);
					}
					Print(a_v, who, cx, wy, 16.0f, Fade(th.style == Style::kModern ? Rgba(244, 238, 226) : th.text, a * in), Align::kCentre,
						th.style == Style::kRobCo ? 0.0f : 3.0f, true);
				}
				return;
			}

			// GAME OVER and the like
			const float in = Ease(t / 0.35f);
			const float y = a_v.h * 0.30f;
			const float size = 58.0f;
			const float track = th.style == Style::kRobCo ? 2.0f : 6.0f * in;
			const float w = std::max(TextWidth(a_v, a_st.banner, size, track, true), TextWidth(a_v, a_st.bannerSub, 16.0f, 2.0f)) + 260.0f;
			const float bh = a_st.bannerSub.empty() ? size + 34.0f : size + 62.0f;
			const float half = w * 0.5f * (0.6f + 0.4f * in);
			switch (th.style) {
			case Style::kVaultTec:
				Panel(a_v, cx - half * 0.8f, y - 18.0f, half * 1.6f, bh + 4.0f, a);
				break;
			case Style::kRobCo:
				Panel(a_v, cx - half * 0.8f, y - 18.0f, half * 1.6f, bh + 4.0f, a);
				break;
			default:
				{
					const ImU32 band = Rgba(8, 7, 10, 0.82f * a), none = Rgba(8, 7, 10, 0.0f);
					Gradient(a_v.dl, a_v.P(cx - half, y - 16.0f), a_v.P(cx, y - 16.0f + bh), none, band, band, none);
					Gradient(a_v.dl, a_v.P(cx, y - 16.0f), a_v.P(cx + half, y - 16.0f + bh), band, none, none, band);
					const ImU32 rule = Fade(th.accent, 0.9f * a), ruleNone = Fade(th.accent, 0.0f);
					for (const float ry : { y - 17.0f, y - 17.5f + bh }) {
						Gradient(a_v.dl, a_v.P(cx - half, ry), a_v.P(cx, ry + 1.5f), ruleNone, rule, rule, ruleNone);
						Gradient(a_v.dl, a_v.P(cx, ry), a_v.P(cx + half, ry + 1.5f), rule, ruleNone, ruleNone, rule);
					}
					break;
				}
			}
			const std::string title = th.style == Style::kRobCo ? "> " + a_st.banner : a_st.banner;
			const ImU32       titleCol = th.style == Style::kModern ? Rgba(244, 238, 226) : th.style == Style::kVaultTec ? th.accent : th.text;
			Print(a_v, title, cx, y, size, Fade(titleCol, a), Align::kCentre, track, true);
			Print(a_v, a_st.bannerSub, cx, y + size + 10.0f, 16.0f, Fade(th.style == Style::kVaultTec ? th.text : th.accent, a), Align::kCentre, 2.0f);
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
			static const auto start = std::chrono::steady_clock::now();
			View              v;
			v.dl = GetBackgroundDrawList();
			v.s = std::max(0.1f, viewport->Size.y / 720.0f * Settings::Get().hudScale);
			v.origin = viewport->Pos;
			v.w = viewport->Size.x / v.s;
			v.h = viewport->Size.y / v.s;
			v.time = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
			v.th = &ThemeNow();
			if (!v.dl) {
				return;
			}
			if (!st.game) {
				Prompt(v, st);
				return;
			}
			Guide(viewport, v, st);
			Sheet(v, st);
			if (st.aiming) {
				Meters(v, st);
			}
			Banner(v, st);
		}

		// the settings page, in the framework's menu as Playable Bowling. Every change is saved to the INI straight away
		// (sliders when you let go); everything else is in the INI itself
		void __stdcall SettingsPage()
		{
			auto&      s = Settings::Get();
			const auto save = [&](bool a_changed) {
				if (a_changed) {
					s.SaveMenu();
				}
			};
			const auto slider = [&](const char* a_label, float& a_value, float a_min, float a_max, const char* a_format) {
				SliderFloat(a_label, &a_value, a_min, a_max, a_format, 0);
				save(IsItemDeactivatedAfterEdit());
			};

			Text("HUD");
			Separator();
			int style = std::clamp(s.hudStyle, 0, static_cast<int>(Settings::kHudStyles.size()) - 1);
			if (Combo("Style", &style, Settings::kHudStyles.data(), static_cast<int>(Settings::kHudStyles.size()), -1)) {
				s.hudStyle = style;
				s.SaveMenu();
			}
			TextWrapped("Modern: smoked glass and amber. Pip-Boy: Fallout 4's own green HUD. Vault-Tec: 1950s blue and yellow. RobCo: a green terminal.");
			slider("Size", s.hudScale, 0.6f, 1.6f, "%.2f");
			save(Checkbox("Aim line", &s.guide));
			save(Checkbox("Calls as notifications too", &s.notifications));

			Spacing();
			Text("Sound");
			Separator();
			save(Checkbox("Sounds", &s.sound));
			slider("Volume", s.soundVolume, 0.0f, 2.0f, "%.2f");

			Spacing();
			Text("Lane");
			Separator();
			save(Checkbox("Pinsetter (from the next game)", &s.pinsetter));
			save(Checkbox("Lights over the pins (from the next game)", &s.pinLights));
			slider("Light brightness", s.pinLightBrightness, 0.1f, 1.5f, "%.2f");
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
		F4SEMenuFramework::SetSection("Playable Bowling");
		F4SEMenuFramework::AddSectionItem("Settings", SettingsPage);
		log::info("HUD element and settings page registered");
	}

	bool Installed()
	{
		return g_element != nullptr;
	}
}
