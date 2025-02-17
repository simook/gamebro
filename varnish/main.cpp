#include "varnish.h"
#include <cstdio>
#include <libgbc/machine.hpp>
#include <spng.h>

EMBED_BINARY(index_html, "../index.html");
EMBED_BINARY(rom, "../rom.gbc");
static std::vector<uint8_t> romdata { rom, rom + rom_size };

using PaletteArray = std::array<uint32_t, 64>;
using PixelArray = std::array<uint16_t, 160 * 144>;
struct PixelState {
	PixelArray pixels;
	PaletteArray palette;
};
struct InputState {
	bool a = false;
	bool b = false;
	bool e = false;
	bool s = false;
	int  direction = 0;
};
static gbc::Machine* machine = nullptr;
static PixelState storage_state;

static std::pair<void*, size_t>
generate_png(const PixelArray& pixels, const PaletteArray& palette)
{
    const int size_x = 160;
    const int size_y = 144;

	//  Convert to RGBA pixel array
	alignas(32) std::array<uint32_t, 160 * 144> rgba_pixels;
	size_t pidx = 0;
	for (const auto idx : pixels) {
		rgba_pixels[pidx++] = palette.at(idx);
	}

	// Render to PNG
	spng_ctx *enc = spng_ctx_new(SPNG_CTX_ENCODER);
	spng_set_option(enc, SPNG_ENCODE_TO_BUFFER, 1);

	spng_ihdr ihdr;
	spng_get_ihdr(enc, &ihdr);
	ihdr.width = size_x;
	ihdr.height = size_y;
	ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
	ihdr.bit_depth = 8;

	spng_set_ihdr(enc, &ihdr);

	int ret =
		spng_encode_image(enc,
			rgba_pixels.data(), rgba_pixels.size() * 4,
			SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);

	size_t png_size = 0;
    void  *png_buf = spng_get_png_buffer(enc, &png_size, &ret);

	return {png_buf, png_size};
}

struct FrameState {
	size_t   frame_number;
	timespec ts;
	InputState inputs;
};
static FrameState current_state;

static timespec time_now() {
	timespec t;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	return t;
}
static double time_diff(timespec start_time, timespec end_time) {
	const double secs = end_time.tv_sec - start_time.tv_sec;
	return secs + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
}

static void get_state(size_t n, struct virtbuffer vb[n], size_t res)
{
	assert(machine);

	auto& inputs = *(InputState*)vb[0].data;
	current_state.inputs.a |= inputs.a;
	current_state.inputs.b |= inputs.b;
	current_state.inputs.e |= inputs.e;
	current_state.inputs.s |= inputs.s;
	current_state.inputs.direction |= inputs.direction;

	auto t1 = time_now();

	if (time_diff(current_state.ts, t1) > 0.016)
	{
		uint8_t keys = 0;
		if (current_state.inputs.e)
			gbc::setflag(true, keys, gbc::BUTTON_START);
		if (current_state.inputs.s)
			gbc::setflag(true, keys, gbc::BUTTON_SELECT);
		if (current_state.inputs.a)
			gbc::setflag(true, keys, gbc::BUTTON_A);
		if (current_state.inputs.b)
			gbc::setflag(true, keys, gbc::BUTTON_B);
		gbc::setflag(current_state.inputs.direction & 1, keys, gbc::DPAD_UP);
		gbc::setflag(current_state.inputs.direction & 2, keys, gbc::DPAD_DOWN);
		gbc::setflag(current_state.inputs.direction & 4, keys, gbc::DPAD_RIGHT);
		gbc::setflag(current_state.inputs.direction & 8, keys, gbc::DPAD_LEFT);
		machine->set_inputs(keys);

		machine->simulate_one_frame();
		current_state.frame_number = machine->gpu.frame_count();
		current_state.ts = t1;
		std::copy(machine->gpu.pixels().begin(), machine->gpu.pixels().end(), storage_state.pixels.begin());
		current_state.inputs = {};
	}

	storage_return(&storage_state, sizeof(storage_state));
}

static void on_get(const char* c_url, int, int)
{
	std::string url = c_url;

	if (url == "/x") {
		const char* ctype = "text/html";
		backend_response(200, ctype, strlen(ctype),
			index_html, index_html_size);
	}

	InputState inputs {};
	inputs.a = (url.find('a') != std::string::npos);
	inputs.b = (url.find('b') != std::string::npos);
	inputs.e = (url.find('e') != std::string::npos);
	inputs.s = (url.find('s') != std::string::npos);
	if (url.find('u') != std::string::npos)
		inputs.direction |= 1;
	else if (url.find('d') != std::string::npos)
		inputs.direction |= 2;
	else if (url.find('r') != std::string::npos)
		inputs.direction |= 4;
	else if (url.find('l') != std::string::npos)
		inputs.direction |= 8;

	// Read the current state from the shared storage VM
	// The storage VM also increments the frame number
	PixelState state;
	storage_call(get_state, &inputs, sizeof(inputs), &state, sizeof(state));

	auto png = generate_png(state.pixels, state.palette);
	const char* ctype = "image/png";
	backend_response(200, ctype, strlen(ctype),
		png.first, png.second);
}

static void do_serialize_state() {
	std::vector<uint8_t> state;
	machine->serialize_state(state);
	state.insert(state.end(), (uint8_t*) &storage_state, (uint8_t*) &storage_state + sizeof(storage_state));
	state.insert(state.end(), (uint8_t*) &current_state, (uint8_t*) &current_state + sizeof(FrameState));
	storage_return(state.data(), state.size());
}
static void do_restore_state(size_t len) {
	printf("State: %zu bytes\n", len);
	fflush(stdout);
	// Restoration happens in two stages.
	// 1st stage: No data, but the length is provided.
	std::vector<uint8_t> state;
	state.resize(len);
	storage_return(state.data(), state.size());
	// 2nd stage: Do the actual restoration:
	size_t off = machine->restore_state(state);
	if (state.size() >= off + sizeof(PixelState)) {
		storage_state = *(PixelState*) &state.at(off);
		off += sizeof(PixelState);
	}
	if (state.size() >= off + sizeof(FrameState)) {
		current_state = *(FrameState*) &state.at(off);
	}
	printf("State restored!\n");
	fflush(stdout);
}

int main(int argc, char** argv)
{
	// Storage has a gbc emulator
	if (std::string(argv[2]) == "1")
	{
		machine = new gbc::Machine(romdata);
		machine->gpu.on_palchange([](const uint8_t idx, const uint16_t color) {
	        storage_state.palette.at(idx) = gbc::GPU::color15_to_rgba32(color);
	    });

		current_state.frame_number = 0;
		current_state.ts = time_now();

		printf("Done loading\n");
		fflush(stdout);
	}

	set_backend_get(on_get);
	set_on_live_update(do_serialize_state);
	set_on_live_restore(do_restore_state);
	wait_for_requests();
}
