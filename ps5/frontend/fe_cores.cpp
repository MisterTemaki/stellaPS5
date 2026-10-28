// Stella PS5 frontend: the libretro cores in the eboot (fe_cores.h).
// SPDX-License-Identifier: MIT

#include "fe_cores.h"

// each core's entry points, under its prefix
#define FE_CORE_DECLARE(p)                                                                                             \
	extern "C" {                                                                                                       \
	void p##retro_set_environment(retro_environment_t);                                                                \
	void p##retro_set_video_refresh(retro_video_refresh_t);                                                            \
	void p##retro_set_audio_sample(retro_audio_sample_t);                                                              \
	void p##retro_set_audio_sample_batch(retro_audio_sample_batch_t);                                                  \
	void p##retro_set_input_poll(retro_input_poll_t);                                                                  \
	void p##retro_set_input_state(retro_input_state_t);                                                                \
	void p##retro_init(void);                                                                                          \
	void p##retro_deinit(void);                                                                                        \
	unsigned p##retro_api_version(void);                                                                               \
	void p##retro_get_system_info(retro_system_info*);                                                                 \
	void p##retro_get_system_av_info(retro_system_av_info*);                                                           \
	void p##retro_set_controller_port_device(unsigned, unsigned);                                                      \
	void p##retro_reset(void);                                                                                         \
	void p##retro_run(void);                                                                                           \
	size_t p##retro_serialize_size(void);                                                                              \
	bool p##retro_serialize(void*, size_t);                                                                            \
	bool p##retro_unserialize(const void*, size_t);                                                                    \
	bool p##retro_load_game(const retro_game_info*);                                                                   \
	void p##retro_unload_game(void);                                                                                   \
	void* p##retro_get_memory_data(unsigned);                                                                          \
	size_t p##retro_get_memory_size(unsigned);                                                                         \
	}

#define FE_CORE_TABLE(p, name)                                                                                         \
	{                                                                                                                  \
		name, p##retro_set_environment, p##retro_set_video_refresh, p##retro_set_audio_sample,                        \
			p##retro_set_audio_sample_batch, p##retro_set_input_poll, p##retro_set_input_state, p##retro_init,         \
			p##retro_deinit, p##retro_api_version, p##retro_get_system_info, p##retro_get_system_av_info,              \
			p##retro_set_controller_port_device, p##retro_reset, p##retro_run, p##retro_serialize_size,                \
			p##retro_serialize, p##retro_unserialize, p##retro_load_game, p##retro_unload_game,                        \
			p##retro_get_memory_data, p##retro_get_memory_size                                                         \
	}

FE_CORE_DECLARE(st_)
FE_CORE_DECLARE(a52_)
FE_CORE_DECLARE(a78_)
FE_CORE_DECLARE(lnx_)

namespace fe
{
namespace
{
const CoreApi kCores[size_t(System::Count)] = {
	FE_CORE_TABLE(st_, "Stella"),
	FE_CORE_TABLE(a52_, "a5200"),
	FE_CORE_TABLE(a78_, "ProSystem"),
	FE_CORE_TABLE(lnx_, "Gearlynx"),
};
} // namespace

const CoreApi& CoreFor(System s)
{
	return kCores[size_t(s) < size_t(System::Count) ? size_t(s) : 0];
}
} // namespace fe
