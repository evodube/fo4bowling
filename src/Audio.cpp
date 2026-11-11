// Bowling sounds, played with our own XAudio2 engine (Windows' xaudio2_9) so they need no records in a plugin.
//
// Every sound is a mono 16-bit WAV cut from real bowling alley recordings (CC0, see Data\Sound\fo4bowling\
// CREDITS.txt). Each one is panned and made quieter with distance from the camera by hand. Several takes per bank,
// picked at random with a small pitch change, keep repeated hits from sounding the same. The rolling ball is its
// own looping voice that follows the ball.

#include "Audio.h"

#include "Settings.h"

#include <xaudio2.h>

namespace fb::Audio
{
	namespace
	{
		constexpr std::size_t   kVoices = 32;
		constexpr int           kMaxPinVoices = 12;  // (pins clattering: past this many at once, more just adds mush)
		constexpr std::uint32_t kRate = 44100;

		struct Clip
		{
			std::vector<std::uint8_t> data;
		};

		struct Voice
		{
			IXAudio2SourceVoice*                  voice{ nullptr };
			std::chrono::steady_clock::time_point started{};
			Bank                                  bank{ Bank::kCount };
		};

		std::mutex                                                            g_lock;
		bool                                                                  g_tried{ false };
		IXAudio2*                                                             g_engine{ nullptr };
		IXAudio2MasteringVoice*                                               g_master{ nullptr };
		std::uint32_t                                                         g_channels{ 2 };
		std::array<std::vector<Clip>, static_cast<std::size_t>(Bank::kCount)> g_banks;
		std::array<Voice, kVoices>                                            g_voices;
		IXAudio2SourceVoice*                                                  g_roll{ nullptr };
		bool                                                                  g_rolling{ false };
		std::optional<std::chrono::steady_clock::time_point>                  g_rollStopAt;  // (faded out, stopped then)
		RE::NiPoint3                                                          g_ear{};
		float                                                                 g_yaw{ 0.0f };
		bool                                                                  g_haveEar{ false };
		std::uint32_t                                                         g_rng{ 0x9E3779B9u };

		float Random01()
		{
			g_rng ^= g_rng << 13;
			g_rng ^= g_rng >> 17;
			g_rng ^= g_rng << 5;
			return static_cast<float>(g_rng & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
		}

		std::filesystem::path SoundFolder()
		{
			wchar_t exe[MAX_PATH]{};
			::GetModuleFileNameW(nullptr, exe, MAX_PATH);
			return std::filesystem::path(exe).parent_path() / L"Data" / L"Sound" / L"fo4bowling";
		}

		// mono 16-bit PCM at 44.1 kHz only (what the mod ships)
		bool LoadWav(const std::filesystem::path& a_path, Clip& a_out)
		{
			std::ifstream file(a_path, std::ios::binary);
			if (!file) {
				return false;
			}
			std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			auto u32 = [&](std::size_t o) { return o + 4 <= bytes.size() ? *reinterpret_cast<const std::uint32_t*>(&bytes[o]) : 0u; };
			auto u16 = [&](std::size_t o) { return o + 2 <= bytes.size() ? *reinterpret_cast<const std::uint16_t*>(&bytes[o]) : std::uint16_t{ 0 }; };
			if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
				return false;
			}
			bool        formatOK = false;
			std::size_t o = 12;
			while (o + 8 <= bytes.size()) {
				const std::uint32_t size = u32(o + 4);
				const std::size_t   body = o + 8;
				if (std::memcmp(&bytes[o], "fmt ", 4) == 0) {
					formatOK = u16(body) == 1 && u16(body + 2) == 1 && u32(body + 4) == kRate && u16(body + 14) == 16;
				} else if (std::memcmp(&bytes[o], "data", 4) == 0 && formatOK) {
					const std::size_t n = std::min<std::size_t>(size, bytes.size() - body);
					a_out.data.assign(bytes.begin() + body, bytes.begin() + body + n);
					return !a_out.data.empty();
				}
				o = body + size + (size & 1);
			}
			return false;
		}

		const wchar_t* BankName(Bank a_bank)
		{
			switch (a_bank) {
			case Bank::kRelease: return L"release";
			case Bank::kRoll: return L"roll";
			case Bank::kCrash: return L"crash";
			case Bank::kHit: return L"hit";
			case Bank::kPin: return L"pin";
			case Bank::kSweep: return L"sweep";
			case Bank::kSet: return L"set";
			default: return L"";
			}
		}

		int Takes(Bank a_bank)
		{
			switch (a_bank) {
			case Bank::kRelease: return 2;
			case Bank::kRoll: return 1;
			case Bank::kCrash: return 6;
			case Bank::kHit: return 6;
			case Bank::kPin: return 4;
			case Bank::kSweep: return 2;
			case Bank::kSet: return 3;
			default: return 0;
			}
		}

		WAVEFORMATEX MonoFormat()
		{
			WAVEFORMATEX format{};
			format.wFormatTag = WAVE_FORMAT_PCM;
			format.nChannels = 1;
			format.nSamplesPerSec = kRate;
			format.wBitsPerSample = 16;
			format.nBlockAlign = 2;
			format.nAvgBytesPerSec = kRate * 2;
			return format;
		}

		bool Start()
		{
			const HRESULT co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			(void)co;  // (already initialised in another mode is fine for XAudio2 2.9)
			if (FAILED(::XAudio2Create(&g_engine, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
				log::warn("sound: XAudio2 could not start; no bowling sounds");
				g_engine = nullptr;
				return false;
			}
			if (FAILED(g_engine->CreateMasteringVoice(&g_master))) {
				log::warn("sound: no audio output device; no bowling sounds");
				g_engine->Release();
				g_engine = nullptr;
				return false;
			}
			XAUDIO2_VOICE_DETAILS details{};
			g_master->GetVoiceDetails(&details);
			g_channels = std::max<std::uint32_t>(1, details.InputChannels);

			const WAVEFORMATEX format = MonoFormat();
			for (auto& v : g_voices) {
				if (FAILED(g_engine->CreateSourceVoice(&v.voice, &format, 0, 2.0f))) {
					v.voice = nullptr;
				}
			}
			if (FAILED(g_engine->CreateSourceVoice(&g_roll, &format, 0, 2.0f))) {
				g_roll = nullptr;
			}
			return true;
		}

		// a voice that's done playing. None free (or too many pins already): the new sound is dropped. Nothing is ever
		// cut off mid-sound, which clicks
		Voice* FreeVoice(Bank a_bank)
		{
			Voice* free = nullptr;
			int    pins = 0;
			for (auto& v : g_voices) {
				if (!v.voice) {
					continue;
				}
				XAUDIO2_VOICE_STATE state{};
				v.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
				if (state.BuffersQueued == 0) {
					free = free ? free : &v;
				} else if (v.bank == Bank::kPin) {
					++pins;
				}
			}
			return a_bank == Bank::kPin && pins >= kMaxPinVoices ? nullptr : free;
		}

		// the rolling loop is faded out (its levels to 0, which XAudio2 ramps) and stopped a moment later
		void FinishRollStop()
		{
			if (g_rollStopAt && std::chrono::steady_clock::now() >= *g_rollStopAt) {
				if (g_roll) {
					g_roll->Stop();
					g_roll->FlushSourceBuffers();
				}
				g_rollStopAt.reset();
			}
		}

		// the gains for each speaker for a sound at a_pos: quieter with distance, panned by direction. False: too far
		bool Mix(const RE::NiPoint3& a_pos, float a_gain, std::vector<float>& a_matrix)
		{
			const auto& s = Settings::Get();
			RE::NiPoint3 ear = g_haveEar ? g_ear : a_pos;
			float        yaw = g_yaw;
			if (!g_haveEar) {
				if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && camera->cameraRoot) {
					ear = camera->cameraRoot->world.translate;
				}
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					yaw = player->data.angle.z;
				}
			}
			const RE::NiPoint3 to = a_pos - ear;
			const float        distance = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
			if (distance >= s.soundMaxDistance) {
				return false;
			}
			float gain = a_gain * s.soundVolume;
			if (distance > s.soundFullDistance) {
				gain *= s.soundFullDistance / distance;
				gain *= 1.0f - (distance - s.soundFullDistance) / (s.soundMaxDistance - s.soundFullDistance);  // (0 at the max)
			}
			// -1 left .. 1 right, kept off the extremes; equal-power pan
			float pan = 0.0f;
			if (distance > 1.0f) {
				const float right = (to.x * std::cos(yaw) - to.y * std::sin(yaw)) / distance;
				pan = std::clamp(right, -1.0f, 1.0f) * 0.7f;
			}
			const float angle = (pan + 1.0f) * 0.25f * 3.14159265f;
			a_matrix.assign(g_channels, 0.0f);
			if (g_channels == 1) {
				a_matrix[0] = gain;
			} else {
				a_matrix[0] = gain * std::cos(angle);  // front left
				a_matrix[1] = gain * std::sin(angle);  // front right
			}
			return gain > 0.0005f;
		}
	}

	void Init()
	{
		std::scoped_lock lock(g_lock);
		if (g_tried) {
			return;
		}
		g_tried = true;
		if (!Settings::Get().sound) {
			log::info("sound: off in the INI");
			return;
		}
		const auto folder = SoundFolder();
		int        loaded = 0;
		for (std::size_t b = 0; b < g_banks.size(); ++b) {
			// (just the takes this version ships: an older install's extra files, left in the folder, stay unused)
			for (int i = 1; i <= Takes(static_cast<Bank>(b)); ++i) {
				Clip clip;
				if (!LoadWav(folder / std::format(L"{}_{}.wav", BankName(static_cast<Bank>(b)), i), clip)) {
					break;
				}
				g_banks[b].push_back(std::move(clip));
				++loaded;
			}
		}
		if (loaded == 0) {
			log::warn("sound: no sounds found in {}", folder.string());
			return;
		}
		if (Start()) {
			log::info("sound: {} sounds loaded, {} output channels", loaded, g_channels);
		}
	}

	void SetListener(const RE::NiPoint3& a_pos, float a_yaw, bool a_valid)
	{
		std::scoped_lock lock(g_lock);
		g_ear = a_pos;
		g_yaw = a_yaw;
		g_haveEar = a_valid;
		FinishRollStop();
	}

	void Play(Bank a_bank, const RE::NiPoint3& a_pos, float a_gain, float a_pitch)
	{
		const auto& s = Settings::Get();
		if (!s.sound || a_gain <= 0.001f) {
			return;
		}
		std::scoped_lock lock(g_lock);
		FinishRollStop();
		const auto& clips = g_banks[static_cast<std::size_t>(a_bank)];
		if (!g_engine || clips.empty()) {
			return;
		}
		std::vector<float> matrix;
		if (!Mix(a_pos, std::min(1.0f, a_gain), matrix)) {
			return;
		}
		Voice* v = FreeVoice(a_bank);
		if (!v) {
			return;
		}
		v->bank = a_bank;
		const auto&    clip = clips[static_cast<std::size_t>(Random01() * static_cast<float>(clips.size())) % clips.size()];
		XAUDIO2_BUFFER buffer{};
		buffer.Flags = XAUDIO2_END_OF_STREAM;
		buffer.AudioBytes = static_cast<UINT32>(clip.data.size());
		buffer.pAudioData = clip.data.data();
		v->voice->SetOutputMatrix(g_master, 1, g_channels, matrix.data());
		v->voice->SetVolume(1.0f);
		v->voice->SetFrequencyRatio(std::clamp(a_pitch * (0.95f + 0.1f * Random01()), 0.5f, 2.0f));
		if (SUCCEEDED(v->voice->SubmitSourceBuffer(&buffer))) {
			v->voice->Start();
			v->started = std::chrono::steady_clock::now();
		}
	}

	void Roll(const RE::NiPoint3& a_pos, float a_gain, float a_pitch)
	{
		const auto& s = Settings::Get();
		std::scoped_lock lock(g_lock);
		const auto& clips = g_banks[static_cast<std::size_t>(Bank::kRoll)];
		if (!s.sound || !g_engine || !g_roll || clips.empty()) {
			return;
		}
		if (g_rollStopAt) {
			// still fading from the last ball (silent by now or nearly): stop it before starting again
			g_roll->Stop();
			g_roll->FlushSourceBuffers();
			g_rollStopAt.reset();
			g_rolling = false;
		}
		std::vector<float> matrix;
		if (!Mix(a_pos, std::clamp(a_gain, 0.0f, 1.0f), matrix)) {
			matrix.assign(g_channels, 0.0f);
		}
		g_roll->SetOutputMatrix(g_master, 1, g_channels, matrix.data());
		g_roll->SetFrequencyRatio(std::clamp(a_pitch, 0.5f, 2.0f));
		if (!g_rolling) {
			// the loop, round and round until stopped (its levels are set above, before it starts)
			const auto&    clip = clips[static_cast<std::size_t>(Random01() * static_cast<float>(clips.size())) % clips.size()];
			XAUDIO2_BUFFER buffer{};
			buffer.AudioBytes = static_cast<UINT32>(clip.data.size());
			buffer.pAudioData = clip.data.data();
			buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
			if (FAILED(g_roll->SubmitSourceBuffer(&buffer))) {
				return;
			}
			g_roll->Start();
			g_rolling = true;
		}
	}

	void StopRoll()
	{
		std::scoped_lock lock(g_lock);
		if (g_roll && g_rolling) {
			const std::vector<float> silent(g_channels, 0.0f);
			g_roll->SetOutputMatrix(g_master, 1, g_channels, silent.data());
			g_rollStopAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
		}
		g_rolling = false;
	}
}
