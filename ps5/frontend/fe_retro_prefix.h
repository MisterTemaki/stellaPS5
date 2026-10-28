/* Stella PS5: one libretro core among several in one eboot (ps5/cores.mk).
 *
 * Every core defines the same libretro entry points (retro_init, retro_run...). Each core's sources are compiled with
 * this header forced in (-include) and FE_RETRO_PREFIX set to the core's prefix (st_ Stella, a52_ a5200, a78_
 * ProSystem, lnx_ Gearlynx), so its entry points become st_retro_init, a52_retro_run... in its declarations (libretro.h)
 * and its definitions alike; the frontend calls them through a table (fe_cores.h). The core's other global names are
 * kept apart by cores.mk (made local to the core once its objects are linked together).
 * SPDX-License-Identifier: MIT */
#ifndef FE_RETRO_PREFIX_H
#define FE_RETRO_PREFIX_H

#ifndef FE_RETRO_PREFIX
#error "FE_RETRO_PREFIX (st_, a52_, a78_, lnx_) must be defined"
#endif

#define FE_RP_CAT2(a, b) a##b
#define FE_RP_CAT(a, b) FE_RP_CAT2(a, b)

#define retro_set_environment FE_RP_CAT(FE_RETRO_PREFIX, retro_set_environment)
#define retro_set_video_refresh FE_RP_CAT(FE_RETRO_PREFIX, retro_set_video_refresh)
#define retro_set_audio_sample FE_RP_CAT(FE_RETRO_PREFIX, retro_set_audio_sample)
#define retro_set_audio_sample_batch FE_RP_CAT(FE_RETRO_PREFIX, retro_set_audio_sample_batch)
#define retro_set_input_poll FE_RP_CAT(FE_RETRO_PREFIX, retro_set_input_poll)
#define retro_set_input_state FE_RP_CAT(FE_RETRO_PREFIX, retro_set_input_state)
#define retro_init FE_RP_CAT(FE_RETRO_PREFIX, retro_init)
#define retro_deinit FE_RP_CAT(FE_RETRO_PREFIX, retro_deinit)
#define retro_api_version FE_RP_CAT(FE_RETRO_PREFIX, retro_api_version)
#define retro_get_system_info FE_RP_CAT(FE_RETRO_PREFIX, retro_get_system_info)
#define retro_get_system_av_info FE_RP_CAT(FE_RETRO_PREFIX, retro_get_system_av_info)
#define retro_set_controller_port_device FE_RP_CAT(FE_RETRO_PREFIX, retro_set_controller_port_device)
#define retro_reset FE_RP_CAT(FE_RETRO_PREFIX, retro_reset)
#define retro_run FE_RP_CAT(FE_RETRO_PREFIX, retro_run)
#define retro_serialize_size FE_RP_CAT(FE_RETRO_PREFIX, retro_serialize_size)
#define retro_serialize FE_RP_CAT(FE_RETRO_PREFIX, retro_serialize)
#define retro_unserialize FE_RP_CAT(FE_RETRO_PREFIX, retro_unserialize)
#define retro_cheat_reset FE_RP_CAT(FE_RETRO_PREFIX, retro_cheat_reset)
#define retro_cheat_set FE_RP_CAT(FE_RETRO_PREFIX, retro_cheat_set)
#define retro_load_game FE_RP_CAT(FE_RETRO_PREFIX, retro_load_game)
#define retro_load_game_special FE_RP_CAT(FE_RETRO_PREFIX, retro_load_game_special)
#define retro_unload_game FE_RP_CAT(FE_RETRO_PREFIX, retro_unload_game)
#define retro_get_region FE_RP_CAT(FE_RETRO_PREFIX, retro_get_region)
#define retro_get_memory_data FE_RP_CAT(FE_RETRO_PREFIX, retro_get_memory_data)
#define retro_get_memory_size FE_RP_CAT(FE_RETRO_PREFIX, retro_get_memory_size)

#endif
