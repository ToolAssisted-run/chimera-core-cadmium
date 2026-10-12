/* What both runners of the tests do with a core once they have its exports:
 * the native one (run-native.c) calls them directly, the sandboxed one
 * (run-wbx.c) through miniBox. One file, so that the two runs are asked the
 * same things in the same order.
 *
 *   <runner> ... [--frames N] [--every K] [--press F:N:MASK]... [--exercise]
 *                [--variant V] [--speed N] [--seed N]
 *                [--rerecord] [--save-state FILE --at F] [--state FILE]
 *                [--ppm FILE [--ppm-at F]] [--dump-ram FILE]
 *
 * --press F:N:MASK  from frame F (the first is 1) for N frames, these keys
 *                   (hex; bit 0 is key 0, bit 15 is key F)
 * --exercise        keys pressed by a fixed pattern, the same in both runners
 * --every K         a line of digest every K frames, and always one at the end
 * --variant V, --speed N, --seed N   the package's settings "variant",
 *                   "instructions_per_frame" and "random_seed"
 * --rerecord        a state saved and loaded before every frame (sandbox)
 * --save-state FILE --at F   after frame F, save a state to FILE and stop
 * --state FILE      carry on from a state another process saved
 * --ppm FILE [--ppm-at F]    the picture after frame F (the last one without
 *                   --ppm-at), as a file a person can look at
 * --dump-ram FILE   the machine's memory after the run
 */
#ifndef CHIMERA_CADMIUM_GATE_HARNESS_H_
#define CHIMERA_CADMIUM_GATE_HARNESS_H_

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct harness_core {
	int (*Init)(void);
	const char *(*GetLoadError)(void);
	void (*FrameAdvance)(uint64_t);
	int (*InputWasRead)(void);
	int16_t *(*GetAudio)(void);
	int (*GetAudioSampleCount)(void);
	uint32_t *(*GetVideoBgra)(void);
	int (*GetVideoWidth)(void);
	int (*GetVideoHeight)(void);
	int (*GetVsyncNumerator)(void);
	uint8_t *(*GetMemoryDomainPtr)(int);
	int64_t (*GetMemoryDomainSize)(int);
	const char *(*DebugVariant)(void);
	int64_t (*GetCycleCount)(void);
	int32_t (*GetInstructionsPerFrame)(void);
	/* the sandbox's; null natively */
	int (*seal)(void);
	int (*save)(uint8_t **buf, size_t *len);
	int (*load)(const uint8_t *buf, size_t len);
};

#define HARNESS_MAX_EVENTS 64
struct harness_event { uint64_t from, count, mask; };
struct harness_opts {
	uint64_t frames, every, save_at, ppm_at;
	int rerecord, exercise;
	long speed, seed;
	const char *variant, *save_state, *state, *ppm, *dump_ram;
	struct harness_event press[HARNESS_MAX_EVENTS];
	int presses;
};

/* The options from argv[first...]. 0 with a message when one is not one. */
static int harness_parse(int argc, char **argv, int first, struct harness_opts *o)
{
	memset(o, 0, sizeof *o);
	o->frames = 300;
	o->variant = "auto";
	for (int i = first; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--rerecord")) { o->rerecord = 1; continue; }
		if (!strcmp(a, "--exercise")) { o->exercise = 1; continue; }
		if (i + 1 >= argc) {
			fprintf(stderr, "%s wants a value, or is not an option\n", a);
			return 0;
		}
		const char *v = argv[++i];
		if (!strcmp(a, "--frames")) o->frames = strtoull(v, NULL, 10);
		else if (!strcmp(a, "--every")) o->every = strtoull(v, NULL, 10);
		else if (!strcmp(a, "--at")) o->save_at = strtoull(v, NULL, 10);
		else if (!strcmp(a, "--variant")) o->variant = v;
		else if (!strcmp(a, "--speed")) o->speed = atol(v);
		else if (!strcmp(a, "--seed")) o->seed = atol(v);
		else if (!strcmp(a, "--save-state")) o->save_state = v;
		else if (!strcmp(a, "--state")) o->state = v;
		else if (!strcmp(a, "--ppm")) o->ppm = v;
		else if (!strcmp(a, "--ppm-at")) o->ppm_at = strtoull(v, NULL, 10);
		else if (!strcmp(a, "--dump-ram")) o->dump_ram = v;
		else if (!strcmp(a, "--press")) {
			unsigned long long from, count, mask;
			if (o->presses >= HARNESS_MAX_EVENTS || sscanf(v, "%llu:%llu:%llx", &from, &count, &mask) != 3) {
				fprintf(stderr, "--press wants <frame>:<frames>:<hex mask>, not %s\n", v);
				return 0;
			}
			o->press[o->presses++] = (struct harness_event){ from, count, mask };
		} else {
			fprintf(stderr, "no such option: %s\n", a);
			return 0;
		}
	}
	if (o->save_state && !o->save_at) {
		fprintf(stderr, "--save-state wants --at <a frame of the run>\n");
		return 0;
	}
	return 1;
}

/* The settings a runner hands the core, as the frontend would. */
static void harness_settings(const struct harness_opts *o, char *out, size_t size)
{
	snprintf(out, size, "{\"variant\":\"%s\",\"instructions_per_frame\":%ld,\"random_seed\":%ld}",
	         o->variant, o->speed, o->seed);
}

static uint64_t harness_hash(uint64_t h, const void *data, size_t bytes)
{
	const uint8_t *p = (const uint8_t *)data;
	for (size_t i = 0; i < bytes; i++)
		h = (h ^ p[i]) * 0x100000001b3ull;
	return h;
}
#define HARNESS_HASH_START 0xcbf29ce484222325ull

/* The keys of a frame when --exercise is given: a fixed pattern that changes
 * every few frames and holds one or two keys at a time, so that programs that
 * wait for a key and programs that poll one both see something. */
static uint64_t harness_exercise(uint64_t frame)
{
	uint64_t x = (frame / 6) * 6364136223846793005ull + 1442695040888963407ull;
	x ^= x >> 33;
	if ((x & 3) == 0)
		return 0;
	return (1ull << ((x >> 8) & 15)) | ((x & 3) == 3 ? 1ull << ((x >> 16) & 15) : 0);
}

/* What a run has added up so far; a state file carries it, so that a run
 * carried on in another process ends with the sums of one that never stopped. */
struct harness_sums { uint64_t frame, input_frames, audio_samples, audio_hash, video_hash; };
#define HARNESS_STATE_MAGIC "cadmium-gate-state-1"

static void harness_line(const struct harness_core *c, const struct harness_sums *s, const char *what)
{
	const int w = c->GetVideoWidth(), h = c->GetVideoHeight();
	const int64_t ram = c->GetMemoryDomainSize(0);
	printf("%s frame=%" PRIu64 " cycles=%" PRId64 " inputFrames=%" PRIu64 " audioSamples=%" PRIu64
	       " audioHash=%016" PRIx64 " ramBytes=%" PRId64 " ramHash=%016" PRIx64
	       " video=%dx%d videoHash=%016" PRIx64 " videosHash=%016" PRIx64 "\n",
	       what, s->frame, c->GetCycleCount(), s->input_frames, s->audio_samples, s->audio_hash, ram,
	       harness_hash(HARNESS_HASH_START, c->GetMemoryDomainPtr(0), (size_t)ram), w, h,
	       harness_hash(HARNESS_HASH_START, c->GetVideoBgra(), (size_t)w * (size_t)h * 4), s->video_hash);
	fflush(stdout);
}

/* The picture as a PPM: the simplest file a viewer opens. */
static void harness_ppm(const struct harness_core *c, const char *path)
{
	const int w = c->GetVideoWidth(), h = c->GetVideoHeight();
	const uint32_t *p = c->GetVideoBgra();
	FILE *f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "cannot write %s\n", path);
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++) {
		const uint8_t rgb[3] = { (uint8_t)(p[i] >> 16), (uint8_t)(p[i] >> 8), (uint8_t)p[i] };
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
}

/* Init has not been called. The process's exit code. */
static int harness_run(const struct harness_core *c, const struct harness_opts *o)
{
	if ((o->rerecord || o->save_state || o->state) && !c->save) {
		fprintf(stderr, "states are the sandbox's: this runner has none\n");
		return 2;
	}
	if (!c->Init()) {
		fprintf(stderr, "Init: %s\n", c->GetLoadError());
		return 1;
	}
	/* the machine as it stands after Init is what every state is against */
	if (c->seal && !c->seal())
		return 1;
	printf("machine variant=\"%s\" framesPerSecond=%d instructionsPerFrame=%d\n", c->DebugVariant(),
	       c->GetVsyncNumerator(), c->GetInstructionsPerFrame());

	struct harness_sums s = { 0, 0, 0, HARNESS_HASH_START, HARNESS_HASH_START };
	if (o->state) {
		FILE *f = fopen(o->state, "rb");
		char magic[sizeof HARNESS_STATE_MAGIC];
		uint64_t len = 0;
		if (!f || fread(magic, 1, sizeof magic, f) != sizeof magic || memcmp(magic, HARNESS_STATE_MAGIC, sizeof magic) ||
		    fread(&s, sizeof s, 1, f) != 1 || fread(&len, sizeof len, 1, f) != 1) {
			fprintf(stderr, "%s is not a state of these tests\n", o->state);
			return 2;
		}
		uint8_t *buf = (uint8_t *)malloc(len ? len : 1);
		if (!buf || fread(buf, 1, len, f) != len) {
			fprintf(stderr, "%s is cut short\n", o->state);
			return 2;
		}
		fclose(f);
		if (!c->load(buf, len))
			return 1;
		free(buf);
	}

	while (s.frame < o->frames) {
		const uint64_t f = s.frame + 1;
		if (o->rerecord) {
			uint8_t *buf = NULL;
			size_t len = 0;
			if (!c->save(&buf, &len))
				return 1;
			/* the tests' control: once, half way, the state loaded is the
			 * one from the frame before - a state that is not this moment's
			 * must show in what the run ends with */
			static uint8_t *before;
			static size_t before_len;
			const char *stale = getenv("HARNESS_BREAK_STALE_STATE");
			if (stale && *stale && before && f == o->frames / 2) {
				if (!c->load(before, before_len))
					return 1;
			} else if (!c->load(buf, len)) {
				return 1;
			}
			free(before);
			before = buf;
			before_len = len;
		}
		uint64_t keys = o->exercise ? harness_exercise(f) : 0;
		for (int i = 0; i < o->presses; i++)
			if (f >= o->press[i].from && f < o->press[i].from + o->press[i].count)
				keys |= o->press[i].mask;
		c->FrameAdvance(keys);
		s.frame = f;
		s.input_frames += c->InputWasRead() ? 1 : 0;
		const int n = c->GetAudioSampleCount();
		if (n > 0) {
			s.audio_samples += (uint64_t)n;
			s.audio_hash = harness_hash(s.audio_hash, c->GetAudio(), (size_t)n * 2);
		}
		/* every frame's picture, added up: a picture that was wrong for one
		 * frame in the middle shows at the end */
		s.video_hash = harness_hash(s.video_hash, c->GetVideoBgra(),
		                            (size_t)c->GetVideoWidth() * (size_t)c->GetVideoHeight() * 4);
		if (o->every && f % o->every == 0 && f != o->frames)
			harness_line(c, &s, "at");
		if (o->ppm && o->ppm_at == f)
			harness_ppm(c, o->ppm);
		if (o->save_state && f == o->save_at) {
			uint8_t *buf = NULL;
			size_t len = 0;
			if (!c->save(&buf, &len))
				return 1;
			FILE *out = fopen(o->save_state, "wb");
			const uint64_t len64 = len;
			if (!out || fwrite(HARNESS_STATE_MAGIC, 1, sizeof HARNESS_STATE_MAGIC, out) != sizeof HARNESS_STATE_MAGIC ||
			    fwrite(&s, sizeof s, 1, out) != 1 || fwrite(&len64, sizeof len64, 1, out) != 1 ||
			    fwrite(buf, 1, len, out) != len || fclose(out) != 0) {
				fprintf(stderr, "cannot write %s\n", o->save_state);
				return 2;
			}
			printf("saved frame=%" PRIu64 " bytes=%zu\n", f, len);
			return 0;
		}
	}
	harness_line(c, &s, "end");
	if (o->ppm && !o->ppm_at)
		harness_ppm(c, o->ppm);
	if (o->dump_ram) {
		FILE *out = fopen(o->dump_ram, "wb");
		if (!out || fwrite(c->GetMemoryDomainPtr(0), 1, (size_t)c->GetMemoryDomainSize(0), out) !=
		                    (size_t)c->GetMemoryDomainSize(0) || fclose(out) != 0) {
			fprintf(stderr, "cannot write %s\n", o->dump_ram);
			return 2;
		}
	}
	return 0;
}

#endif
