/* cinterface.cpp - Cadmium behind Chimera's guest interface.
 *
 * Cadmium is a CHIP-8 emulator with a window, an editor and a debugger. This
 * file uses the parts that are the machine: its emulation library
 * (src/emulation), and the code that decides which kind of CHIP-8 a program
 * is for and puts it in memory (Chip8EmuHostEx, with its table of known
 * programs). The machine here is a "host" in Cadmium's sense - the object an
 * emulator asks for keys and tells about its screen - with no window behind
 * it: the keys are what the frontend set for this frame, and the picture and
 * the sound are taken when a frame ends.
 *
 * This file compiles IDENTICALLY for the guest (miniBox's emulibc) and for the
 * native reference build (native-shim/emulibc.h): the tests compare the two.
 */
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include <chip8emuhostex.hpp>
#include <emulation/chip8options.hpp>
#include <emulation/ichip8.hpp>

namespace {

using Options = emu::Chip8EmulatorOptions;

/* The sixteen keys of a CHIP-8 keypad, 0 to F: the order of
 * waterbox.config's "input.buttons". */
constexpr int KEYS = 16;

/* The biggest picture any of the machines makes is MegaChip's 256x192. Every
 * other one is twice as wide as it is high on a television (64x32, 128x64,
 * and the COSMAC VIP's 64x64 and 64x128 with taller or flatter dots), so the
 * package says 2:1 and MegaChip's picture is given black borders left and
 * right to make it so: 384x192. */
constexpr int MAX_W = 384, MAX_H = 192;

/* Sound is made at 48000 samples a second, one channel: 800 a frame at 60
 * frames a second, 750 at 64 (the HP-48 calculators' rate), 960 at 50 (the
 * DREAM 6800). All whole numbers, which is why not 44100. */
constexpr int OUT_RATE = 48000;
constexpr int MAX_SAMPLES = 2048;

/* A text a person reads is a key of languages/en.json, each of its
 * arguments after a TEXT_ARG (chimera docs/translations.md). TEXT_ARG is a
 * string of its own so the byte after it is never read as more of its hex. */
#define TEXT_ARG "\x1f"

char g_loadError[512];
uint32_t g_videoOut[MAX_W * MAX_H];
int g_vwidth = 64, g_vheight = 32;
int16_t g_soundOut[MAX_SAMPLES];
int g_nsamples;
int g_inputRead;
uint8_t g_setButtons[KEYS];

/* written and read within one call, so in no savestate */
ECL_INVISIBLE uint32_t g_scratch[256 * 192];

class Machine final : public emu::Chip8EmuHostEx
{
public:
	Options &options() { return _options; }
	emu::IChip8Emulator &chip() { return *_chipEmu; }
	bool hasChip() const { return static_cast<bool>(_chipEmu); }
	/* what the loader put in memory: nothing, when a source did not compile */
	size_t programBytes() const { return _romImage.size(); }

	/* the keys held in this frame, and in the one before */
	std::array<bool, KEYS> keys{};
	std::array<bool, KEYS> before{};

	bool isHeadless() const override { return true; }

	bool isKeyDown(uint8_t key) override
	{
		g_inputRead = 1;
		return keys[key & 0xF];
	}

	const std::array<bool, KEYS> &getKeyStates() const override
	{
		g_inputRead = 1;
		return keys;
	}

	/* "Wait for a key" (Fx0A), as Cadmium's window answers it: a key that
	 * goes down is noticed, and it is reported when it comes up again - as
	 * long as the program is still at the same instruction. 0: nothing yet.
	 * -1: a key is down and its release is awaited. Otherwise the key, 1 to
	 * 16. Cadmium keeps this in statics of its window; here it is the
	 * machine's, and so it is in a savestate. */
	int getKeyPressed() override
	{
		g_inputRead = 1;
		const uint32_t pc = _chipEmu->getPC();
		if (_awaited && _awaitedAt == pc)
		{
			if (!keys[_awaited - 1])
			{
				const int key = _awaited;
				_awaited = 0;
				_awaitedAt = 0;
				return key;
			}
			return -1;
		}
		_awaited = 0;
		for (int i = 0; i < KEYS; i++)
		{
			if (keys[i] && !before[i])
			{
				_awaitedAt = pc;
				_awaited = i + 1;
				return 0;
			}
		}
		return 0;
	}

	/* The emulator says its screen is to be shown. MegaChip draws into a
	 * work buffer and shows it only when the program says so, so the picture
	 * is taken now and not when the frame ends. */
	void updateScreen() override
	{
		if (!_chipEmu) return;
		int w = _chipEmu->getCurrentScreenWidth(), h = _chipEmu->getCurrentScreenHeight();
		if (w <= 0 || h <= 0 || w > 256 || h > 192) return;
		if (const auto *screen = _chipEmu->getScreen())
		{
			screen->convert(g_scratch, 256, 255, nullptr);
		}
		else if (const auto *rgba = _chipEmu->getScreenRGBA())
		{
			rgba->convert(g_scratch, 256, _chipEmu->getScreenAlpha(), _chipEmu->getWorkRGBA());
		}
		else
		{
			return;
		}
		/* twice as wide as high, whatever the dots: a picture that is not
		 * (MegaChip's 256x192) gets black borders left and right */
		const bool bordered = w == 256 && h == 192;
		const int outW = bordered ? MAX_W : w;
		const int left = (outW - w) / 2;
		for (int y = 0; y < h; y++)
		{
			uint32_t *out = g_videoOut + y * outW;
			const uint32_t *in = g_scratch + y * 256;
			for (int x = 0; x < left; x++) out[x] = 0xFF000000u;
			for (int x = 0; x < w; x++)
			{
				/* Cadmium's pixel is red, green, blue, alpha in memory;
				 * the frontend's has blue lowest */
				const uint32_t p = in[x];
				out[left + x] = 0xFF000000u | ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);
			}
			for (int x = left + w; x < outW; x++) out[x] = 0xFF000000u;
		}
		g_vwidth = outW;
		g_vheight = h;
	}

	void vblank() override {}

	/* A program that sets its own colours (CHIP-8X, XO-CHIP with a palette):
	 * what Cadmium's window does with them. */
	void updatePalette(const std::array<uint8_t, 16> &palette) override
	{
		if (_customPalette) return;
		static const uint8_t b3[] = { 0, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xff };
		static const uint8_t b2[] = { 0, 0x60, 0xA0, 0xff };
		for (size_t i = 0; i < palette.size(); i++)
		{
			const uint8_t c = palette[i];
			const uint32_t rgb = (uint32_t(b3[(c & 0xe0) >> 5]) << 16) | (uint32_t(b3[(c & 0x1c) >> 2]) << 8) | b2[c & 3];
			_colorPalette[i] = (rgb << 8) | 0xff;
		}
		if (_chipEmu) _chipEmu->setPalette(_colorPalette);
	}

	void updatePalette(const std::vector<uint32_t> &palette, size_t offset) override { setPalette(palette, offset); }

private:
	int _awaited = 0;
	uint32_t _awaitedAt = 0;
};

Machine *g_machine;
int g_vsyncNum = 60;

/* The names the "variant" setting takes, and which of Cadmium's presets each
 * is. "auto" is not in the table: it lets Cadmium decide from the program. */
struct Variant { const char *name; Options::SupportedPreset preset; };
const Variant kVariants[] = {
	{ "chip-8", Options::eCHIP8 },
	{ "chip-8-strict", Options::eCHIP8TE },
	{ "chip-10", Options::eCHIP10 },
	{ "chip-8x", Options::eCHIP8X },
	{ "chip-48", Options::eCHIP48 },
	{ "schip-1.0", Options::eSCHIP10 },
	{ "schip-1.1", Options::eSCHIP11 },
	{ "schip-compatibility", Options::eSCHPC },
	{ "schip-modern", Options::eSCHIP_MODERN },
	{ "megachip", Options::eMEGACHIP },
	{ "xo-chip", Options::eXOCHIP },
	{ "vip-chip-8", Options::eCHIP8VIP },
	{ "vip-chip-8-64x64", Options::eCHIP8VIP_TPD },
	{ "vip-chip-8-64x128", Options::eCHIP8VIP_FPD },
	{ "vip-chip-8e", Options::eCHIP8EVIP },
	{ "vip-chip-8x", Options::eCHIP8XVIP },
	{ "vip-chip-8x-64x64", Options::eCHIP8XVIP_TPD },
	{ "vip-chip-8x-64x128", Options::eCHIP8XVIP_FPD },
	{ "cosmac-vip", Options::eRAWVIP },
	{ "dream-6800", Options::eCHIP8DREAM },
	{ "dream-6800-chiposlo", Options::eC8D68CHIPOSLO },
};

/* The machines that are a whole computer (a COSMAC VIP, a DREAM 6800) or
 * Cadmium's cycle-exact CHIP-8: they keep their own time and make their own
 * random numbers, and the speed and seed settings are not theirs. */
bool keepsItsOwnTime(Options::SupportedPreset p)
{
	switch (p)
	{
		case Options::eCHIP8TE:
		case Options::eCHIP8VIP: case Options::eCHIP8VIP_TPD: case Options::eCHIP8VIP_FPD:
		case Options::eCHIP8EVIP:
		case Options::eCHIP8XVIP: case Options::eCHIP8XVIP_TPD: case Options::eCHIP8XVIP_FPD:
		case Options::eRAWVIP:
		case Options::eCHIP8DREAM: case Options::eC8D68CHIPOSLO:
			return true;
		default:
			return false;
	}
}

std::string lowered(std::string s)
{
	for (auto &c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
	return s;
}

std::string extensionOf(const std::string &name)
{
	const auto slash = name.find_last_of("/\\");
	const auto dot = name.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
	return lowered(name.substr(dot));
}

/* the extensions Cadmium's loader tells a program's kind by */
bool knownExtension(const std::string &ext)
{
	static const char *const known[] = { ".ch8", ".ch10", ".hc8", ".c8tp", ".c8e", ".c8x", ".sc8", ".mc8", ".xo8",
		".c8b", ".8o", ".gif", ".bin", ".ram", ".vip" };
	for (const char *k : known) if (ext == k) return true;
	return false;
}

bool readWhole(const char *name, std::vector<uint8_t> &out, size_t most)
{
	FILE *f = fopen(name, "rb");
	if (!f) return false;
	uint8_t buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
	{
		out.insert(out.end(), buf, buf + n);
		if (out.size() > most) break;
	}
	fclose(f);
	return true;
}

void drainAudio()
{
	int n = OUT_RATE / (g_vsyncNum > 0 ? g_vsyncNum : 60);
	if (n > MAX_SAMPLES) n = MAX_SAMPLES;
	g_machine->chip().renderAudio(g_soundOut, static_cast<size_t>(n), OUT_RATE);
	g_nsamples = n;
}

} // namespace

/* Chimera's guest interface is a C one: the frontend looks these up by name */
extern "C" {

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }

ECL_EXPORT int Init(void)
{
	g_loadError[0] = '\0';

	/* The program: the project slot's file, else the file the frontend
	 * opened on its own, whose name it mounts beside it. The name matters:
	 * its extension is one of the things that say which CHIP-8 this is. */
	char name[512] = "";
	std::string file;
	if (wbx_slot_count("program") > 0 && wbx_slot_name("program", 0, name, sizeof(name)) != nullptr) file = name;
	if (file.empty())
	{
		std::vector<uint8_t> said;
		if (readWhole("rom.name", said, 4096)) file.assign(said.begin(), said.end());
		while (!file.empty() && (file.back() == '\n' || file.back() == '\r')) file.pop_back();
	}
	std::vector<uint8_t> program;
	bool opened = !file.empty() && readWhole(file.c_str(), program, 16u << 20);
	if (!opened && !file.empty() && file[0] != '/') opened = readWhole(("/" + file).c_str(), program, 16u << 20);
	if (!opened) opened = readWhole("rom", program, 16u << 20);
	if (!opened)
	{
		snprintf(g_loadError, sizeof(g_loadError), "error.no_file");
		return 0;
	}
	if (program.empty() || program.size() > (16u << 20))
	{
		snprintf(g_loadError, sizeof(g_loadError), "error.not_program" TEXT_ARG "%s" TEXT_ARG "%zu", file.c_str(), program.size());
		return 0;
	}

	char variant[64] = "auto";
	wbx_setting_str("variant", variant, sizeof(variant));
	const long speed = wbx_setting_long("instructions_per_frame", 0);
	const long seed = wbx_setting_long("random_seed", 0);
	if (speed < 0 || speed > 500000)
	{
		snprintf(g_loadError, sizeof(g_loadError), "error.bad_speed" TEXT_ARG "%ld", speed);
		return 0;
	}
	if (seed < 0 || seed > 65535)
	{
		snprintf(g_loadError, sizeof(g_loadError), "error.bad_seed" TEXT_ARG "%ld", seed);
		return 0;
	}
	const bool decide = !strcmp(variant, "auto");
	Options::SupportedPreset chosen = Options::eCHIP8;
	if (!decide)
	{
		bool found = false;
		for (const Variant &v : kVariants)
		{
			if (!strcmp(variant, v.name)) { chosen = v.preset; found = true; break; }
		}
		if (!found)
		{
			snprintf(g_loadError, sizeof(g_loadError), "error.bad_variant" TEXT_ARG "%s", variant);
			return 0;
		}
	}

	/* The name Cadmium's loader is given: the file's own, without folders.
	 * With the variant chosen by hand the loader is told not to decide, and
	 * a file whose extension it does not know is loaded as plain CHIP-8
	 * bytes. */
	const auto slash = file.find_last_of("/\\");
	std::string shown = slash == std::string::npos ? file : file.substr(slash + 1);
	std::string ext = extensionOf(shown);
	if (!knownExtension(ext))
	{
		if (decide)
		{
			snprintf(g_loadError, sizeof(g_loadError),
				"error.which_variant" TEXT_ARG "%s",
				shown.c_str());
			return 0;
		}
		shown = "program.ch8";
	}

	g_machine = new Machine();
	Machine &m = *g_machine;
	m.updateEmulatorOptions(Options::optionsOfPreset(chosen));
	const auto asAsked = decide ? Machine::LoadOptions::None : Machine::LoadOptions::DontChangeOptions;
	if (!m.loadBinary(shown, program.data(), program.size(), asAsked))
	{
		snprintf(g_loadError, sizeof(g_loadError),
			"error.not_loaded" TEXT_ARG "%s" TEXT_ARG "%s", shown.c_str(), Options::nameOfPreset(m.options().behaviorBase).c_str());
		return 0;
	}

	if (m.programBytes() == 0)
	{
		snprintf(g_loadError, sizeof(g_loadError), "error.not_compiled" TEXT_ARG "%s", shown.c_str());
		return 0;
	}

	/* The machine is decided. Now what is the frontend's to say, on top of
	 * what the variant (or Cadmium's table, for a program it knows) says:
	 * the speed, and the seed of the random numbers. Cadmium's ordinary
	 * generator for the later variants is the C library's rand(), which is
	 * another generator in another C library; its "rand-lcg" is its own, and
	 * the same everywhere. */
	Options final = m.options().clone();
	if (!keepsItsOwnTime(final.behaviorBase))
	{
		if (speed > 0) final.instructionsPerFrame = static_cast<int>(speed);
		if (final.instructionsPerFrame <= 0)
		{
			/* "as fast as the host can": a core has no such speed */
			final.instructionsPerFrame = Options::optionsOfPreset(final.behaviorBase).instructionsPerFrame;
			if (final.instructionsPerFrame <= 0) final.instructionsPerFrame = 15;
		}
		final.advanced["random"] = final.behaviorBase >= Options::eSCHIP10 ? "rand-lcg" : "seeded";
		final.advanced["seed"] = static_cast<int>(seed);
		final.updatedAdvanced();
	}
	m.updateEmulatorOptions(final);
	if (!m.loadBinary(shown, program.data(), program.size(), Machine::LoadOptions::DontChangeOptions))
	{
		snprintf(g_loadError, sizeof(g_loadError), "error.second_load" TEXT_ARG "%s", shown.c_str());
		return 0;
	}

	g_vsyncNum = m.chip().frameRate();
	if (g_vsyncNum <= 0) g_vsyncNum = 60;
	for (uint32_t &p : g_videoOut) p = 0xFF000000u;
	m.updateScreen();
	return 1;
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < KEYS) g_setButtons[index] = state ? 1 : 0;
}

ECL_EXPORT void SetAxis(int32_t, int32_t) {}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	Machine &m = *g_machine;
	m.before = m.keys;
	for (int i = 0; i < KEYS; i++) m.keys[i] = g_setButtons[i] || ((packed >> i) & 1);
	g_inputRead = 0;

	m.chip().tick(m.options().instructionsPerFrame);
	if (m.chip().needsScreenUpdate()) m.updateScreen();
	drainAudio();
}

ECL_EXPORT uint32_t *GetVideoBgra(void) { return g_videoOut; }
ECL_EXPORT int GetVideoWidth(void) { return g_vwidth; }
ECL_EXPORT int GetVideoHeight(void) { return g_vheight; }

ECL_EXPORT int16_t *GetAudio(void) { return g_soundOut; }
ECL_EXPORT int GetAudioSampleCount(void) { return g_nsamples; }

ECL_EXPORT int GetVsyncNumerator(void) { return g_vsyncNum; }
ECL_EXPORT int GetVsyncDenominator(void) { return 1; }

ECL_EXPORT int InputWasRead(void) { return g_inputRead; }

/* The machine's memory, for RAM tools: the 4 KiB of a CHIP-8 (64 KiB for
 * XO-CHIP, 16 MiB for MegaChip), or the whole memory of the COSMAC VIP or the
 * DREAM 6800 when the variant is one of those. */
ECL_EXPORT int GetMemoryDomainCount(void) { return g_machine && g_machine->hasChip() ? 1 : 0; }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return i == 0 ? "RAM" : ""; }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return i == 0 && g_machine ? g_machine->chip().memory() : nullptr; }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return i == 0 && g_machine ? g_machine->chip().memSize() : 0; }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return i == 0 ? 1 : 0; }

/* For the tests: what the machine turned out to be, and how far it has run. */
ECL_EXPORT const char *DebugVariant(void)
{
	/* an answer to a tool is in no savestate */
	ECL_INVISIBLE static char text[64];
	snprintf(text, sizeof(text), "%s", g_machine ? Options::nameOfPreset(g_machine->options().behaviorBase).c_str() : "");
	return text;
}
ECL_EXPORT int64_t GetCycleCount(void) { return g_machine ? static_cast<int64_t>(g_machine->chip().getCycles()) : 0; }
ECL_EXPORT int32_t GetInstructionsPerFrame(void) { return g_machine ? g_machine->options().instructionsPerFrame : 0; }

} // extern "C"
