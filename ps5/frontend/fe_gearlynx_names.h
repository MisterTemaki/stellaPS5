/* Stella PS5: Gearlynx's class names, renamed in its own sources (forced in with -include, ps5/cores.mk).
 *
 * Gearlynx's classes live in the global namespace with names Stella's own classes have too (M6502, Audio, Random...).
 * Two different classes of one name in one program break C++'s one-definition rule: their inline member functions,
 * which the linker keeps one copy of, would be mixed up. So every top-level class name of Gearlynx gets the Glx
 * prefix in Gearlynx's translation units (ps5/tools/check_core_symbols.py checks that no core shares a symbol with
 * another one).
 * SPDX-License-Identifier: MIT */
#ifndef FE_GEARLYNX_NAMES_H
#define FE_GEARLYNX_NAMES_H

#define Audio GlxAudio
#define Bus GlxBus
#define EEPROM GlxEEPROM
#define ElCheapoSD GlxElCheapoSD
#define GameDrive GlxGameDrive
#define GearlynxCore GlxGearlynxCore
#define Input GlxInput
#define LcdScreen GlxLcdScreen
#define LcdScreen_State GlxLcdScreen_State
#define LibretroInstance GlxLibretroInstance
#define LibretroLink GlxLibretroLink
#define LibretroLinkPlacement GlxLibretroLinkPlacement
#define M6502 GlxM6502
#define M6502_State GlxM6502_State
#define Media GlxMedia
#define Memory GlxMemory
#define Memory_State GlxMemory_State
#define Mikey GlxMikey
#define Mikey_State GlxMikey_State
#define Profiler GlxProfiler
#define Random GlxRandom
#define RedEyeStream GlxRedEyeStream
#define SdCardFileSystem GlxSdCardFileSystem
#define SdCardFileSystemEntry GlxSdCardFileSystemEntry
#define SdCardFileSystemLibretro GlxSdCardFileSystemLibretro
#define SdCardFileSystemNative GlxSdCardFileSystemNative
#define SerialState GlxSerialState
#define SixteenBitRegister GlxSixteenBitRegister
#define EightBitRegister GlxEightBitRegister
#define StateSerializer GlxStateSerializer
#define Suzy GlxSuzy
#define Suzy_State GlxSuzy_State
#define SuzyPackState GlxSuzyPackState
#define SuzyPhase GlxSuzyPhase
#define TraceLogger GlxTraceLogger
#define VgmMetadata GlxVgmMetadata
#define VgmRecorder GlxVgmRecorder
#define EepromState GlxEepromState
#define memory_buffer glx_memory_buffer
#define memory_input_buffer glx_memory_input_buffer
#define memory_input_stream glx_memory_input_stream
#define memory_stream glx_memory_stream
#define u16_union glx_u16_union

#endif
