// Stella PS5 frontend: the libretro cores in the eboot, one per system (fe_cores.cpp).
//
//   Atari 2600  Stella (its own libretro port, compiled in fe_stella.cxx)   st_retro_*
//   Atari 5200  a5200                                                     a52_retro_*
//   Atari 7800  ProSystem                                                 a78_retro_*
//   Atari Lynx  Gearlynx                                                  lnx_retro_*
// Each core's libretro entry points carry its prefix (fe_retro_prefix.h, ps5/cores.mk); the frontend calls the
// running game's core through its table.
//
// SPDX-License-Identifier: MIT
#pragma once

#include "fe_games.h"
#include "libretro.h"

#include <cstddef>

namespace fe
{
struct CoreApi
{
	const char* name; // "Stella", "a5200", "ProSystem", "Gearlynx"
	void (*set_environment)(retro_environment_t);
	void (*set_video_refresh)(retro_video_refresh_t);
	void (*set_audio_sample)(retro_audio_sample_t);
	void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*set_input_poll)(retro_input_poll_t);
	void (*set_input_state)(retro_input_state_t);
	void (*init)(void);
	void (*deinit)(void);
	unsigned (*api_version)(void);
	void (*get_system_info)(retro_system_info*);
	void (*get_system_av_info)(retro_system_av_info*);
	void (*set_controller_port_device)(unsigned, unsigned);
	void (*reset)(void);
	void (*run)(void);
	size_t (*serialize_size)(void);
	bool (*serialize)(void*, size_t);
	bool (*unserialize)(const void*, size_t);
	bool (*load_game)(const retro_game_info*);
	void (*unload_game)(void);
	void* (*get_memory_data)(unsigned);
	size_t (*get_memory_size)(unsigned);
};

const CoreApi& CoreFor(System s);
} // namespace fe
