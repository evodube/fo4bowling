#pragma once

#include "MenuWatch.h"

namespace fb::util
{
	inline HWND GameWindow()
	{
		auto* data = RE::BSGraphics::GetRendererData();
		return data ? reinterpret_cast<HWND>(data->renderWindow[0].hwnd) : nullptr;
	}

	inline bool GameHasFocus()
	{
		const HWND window = GameWindow();
		return !window || ::GetForegroundWindow() == window;
	}

	// menus, the console, loading: the game isn't being played right now
	inline bool GamePaused()
	{
		auto* ui = RE::UI::GetSingleton();
		if (ui && (ui->menuMode != 0 || ui->freezeFramePause != 0)) {
			return true;
		}
		return menus::AnyBlocking();
	}

	inline bool KeyDown(int a_vk)
	{
		return a_vk > 0 && (::GetAsyncKeyState(a_vk) & 0x8000) != 0;
	}

	// a node rotation (rows = the object's axes in the world) from a matrix that takes the object's own vectors to
	// world ones (world = a_m * own): its transpose
	inline RE::NiMatrix3 NodeRotation(const double a_m[3][3])
	{
		return RE::NiMatrix3(
			static_cast<float>(a_m[0][0]), static_cast<float>(a_m[1][0]), static_cast<float>(a_m[2][0]), 0.0f,
			static_cast<float>(a_m[0][1]), static_cast<float>(a_m[1][1]), static_cast<float>(a_m[2][1]), 0.0f,
			static_cast<float>(a_m[0][2]), static_cast<float>(a_m[1][2]), static_cast<float>(a_m[2][2]), 0.0f);
	}

	// rotation by a_angle about the unit axis a_k (world = R * own)
	inline void AxisAngle(double a_kx, double a_ky, double a_kz, double a_angle, double a_out[3][3])
	{
		const double c = std::cos(a_angle), s = std::sin(a_angle), t = 1.0 - c;
		const double m[3][3]{ { t * a_kx * a_kx + c, t * a_kx * a_ky - s * a_kz, t * a_kx * a_kz + s * a_ky },
			{ t * a_kx * a_ky + s * a_kz, t * a_ky * a_ky + c, t * a_ky * a_kz - s * a_kx },
			{ t * a_kx * a_kz - s * a_ky, t * a_ky * a_kz + s * a_kx, t * a_kz * a_kz + c } };
		std::copy(&m[0][0], &m[0][0] + 9, &a_out[0][0]);
	}

	inline void Multiply(const double a_a[3][3], const double a_b[3][3], double a_out[3][3])
	{
		double m[3][3]{};
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				m[i][j] = a_a[i][0] * a_b[0][j] + a_a[i][1] * a_b[1][j] + a_a[i][2] * a_b[2][j];
			}
		}
		std::copy(&m[0][0], &m[0][0] + 9, &a_out[0][0]);
	}

	// smallest difference between two angles, in [-pi, pi]
	inline float AngleDiff(float a_a, float a_b)
	{
		constexpr float kPi = 3.14159265358979f;
		float           d = std::fmod(a_a - a_b, 2.0f * kPi);
		if (d > kPi) {
			d -= 2.0f * kPi;
		} else if (d < -kPi) {
			d += 2.0f * kPi;
		}
		return d;
	}

	inline std::string Upper(std::string a_s)
	{
		for (auto& c : a_s) {
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		}
		return a_s;
	}
}
