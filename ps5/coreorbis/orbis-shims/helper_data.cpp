// Stella PS5: StellaPS5-helper.elf built into the app (ProsperoJailbreak.h). Only the native eboot links
// this file; the Makefile passes HELPER_ELF, the helper payload's path.
//
// SPDX-License-Identifier: MIT

#include "ProsperoJailbreak.h"

__asm__(".section .rodata\n"
		".balign 16\n"
		".global stellaps5_helper_elf_begin\n"
		"stellaps5_helper_elf_begin:\n"
		".incbin \"" HELPER_ELF "\"\n"
		".global stellaps5_helper_elf_end\n"
		"stellaps5_helper_elf_end:\n"
		".previous\n");
extern "C" const unsigned char stellaps5_helper_elf_begin[];
extern "C" const unsigned char stellaps5_helper_elf_end[];

namespace jailbreak
{
Blob EmbeddedHelper()
{
	return {stellaps5_helper_elf_begin, size_t(stellaps5_helper_elf_end - stellaps5_helper_elf_begin)};
}
} // namespace jailbreak
