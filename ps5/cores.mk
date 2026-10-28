# Stella PS5: the cores of the other Atari systems, built into the same eboot as Stella (included by ps5/Makefile).
#
#   Atari 5200  a5200 (github.com/libretro/a5200, third_party/cores/a5200): atari800's 5200 emulation as a libretro core
#               of its own, with the analog joystick, the keypad (an on-screen one too) and Altirra's free 5200 BIOS
#   Atari 7800  ProSystem (github.com/libretro/prosystem-libretro, third_party/cores/prosystem)
#   Atari Lynx  Gearlynx (github.com/drhelius/Gearlynx, third_party/cores/gearlynx): its libretro port
# (third_party/cores/README.md: why these, their versions and licences.)
#
# Each core is compiled from the source list of its own libretro build (its Makefile.common, included below) with
# that build's flags, and with frontend/fe_retro_prefix.h forced in: its libretro entry points get its prefix
# (a52_retro_run, a78_retro_run, lnx_retro_run; Stella's are st_retro_run). Then its objects are linked into one
# (ld -r) and every other strong global symbol of it is made local (tools/localize_core.sh): two cores can have a
# function or a variable of the same name (each has its own copy of libretro-common, for one) without meeting. C++'s
# weak symbols (inline functions, templates) can't be hidden that way: Gearlynx's classes are renamed instead
# (frontend/fe_gearlynx_names.h), and tools/check_core_symbols.py stops the build if two cores define one name.
#
# SPDX-License-Identifier: MIT

RETRO_PREFIX = -include frontend/fe_retro_prefix.h -DFE_RETRO_PREFIX=$(1)

# ---- a5200 (C)
A52_DIR := third_party/cores/a5200
CORE_DIR := $(A52_DIR)
include $(A52_DIR)/Makefile.common
A52_SRC := $(SOURCES_C)
A52_FLAGS := -D__LIBRETRO__ -fsigned-char -DNDEBUG $(INCFLAGS) $(call RETRO_PREFIX,a52_) -w

# ---- ProSystem (C)
A78_DIR := third_party/cores/prosystem
CORE_DIR := $(A78_DIR)/core
include $(A78_DIR)/Makefile.common
A78_SRC := $(SOURCES_C)
A78_FLAGS := -D__LIBRETRO__ -DNDEBUG $(INCFLAGS) $(call RETRO_PREFIX,a78_) -w

# ---- Gearlynx (C++11, and miniz in C)
LNX_DIR := third_party/cores/gearlynx
CORE_DIR := $(LNX_DIR)/platforms/libretro
SOURCE_DIR := $(LNX_DIR)/src
DEPS_DIR := $(LNX_DIR)/platforms/shared/dependencies
include $(LNX_DIR)/platforms/libretro/Makefile.common
LNX_SRC := $(SOURCES_C) $(SOURCES_CXX)
LNX_DEFS := -D__LIBRETRO__ -DNDEBUG -DEMULATOR_BUILD=\"77901f3\" -DGLYNX_DISABLE_DISASSEMBLER -DGLYNX_DISABLE_VGMRECORDER -DMINIZ_NO_TIME \
	-I$(CORE_DIR) -I$(SOURCE_DIR) -I$(DEPS_DIR)/miniz $(call RETRO_PREFIX,lnx_) -include frontend/fe_gearlynx_names.h
LNX_CXXFLAGS := -std=c++11 -fno-rtti -fno-exceptions
CORE_DIR := $(ROOT)/src

CORE_HEADERS := frontend/fe_retro_prefix.h frontend/fe_gearlynx_names.h

# ---- PS5
$(PS5_OUT)/cores/a52/%.o: %.c $(CORE_HEADERS)
	@mkdir -p $(dir $@)
	$(PS5_CC) $(PS5_OPT) $(A52_FLAGS) -c -o $@ $<
$(PS5_OUT)/cores/a78/%.o: %.c $(CORE_HEADERS)
	@mkdir -p $(dir $@)
	$(PS5_CC) $(PS5_OPT) $(A78_FLAGS) -c -o $@ $<
$(PS5_OUT)/cores/lnx/%.o: %.c $(CORE_HEADERS)
	@mkdir -p $(dir $@)
	$(PS5_CC) $(PS5_OPT) $(LNX_DEFS) -w -c -o $@ $<
$(PS5_OUT)/cores/lnx/%.o: %.cpp $(CORE_HEADERS)
	@mkdir -p $(dir $@)
	$(PS5_CXX) $(LNX_CXXFLAGS) $(PS5_OPT) $(LNX_DEFS) -w -c -o $@ $<

PS5_TOOLS = NM=$(PS5_PAYLOAD_SDK)/bin/llvm-nm OBJCOPY=$(PS5_PAYLOAD_SDK)/bin/llvm-objcopy LD=$(PS5_PAYLOAD_SDK)/bin/ld.lld
$(PS5_OUT)/cores/%.o: tools/localize_core.sh
	$(PS5_TOOLS) sh tools/localize_core.sh $@ $(*F)_ $(filter %.o,$^)
$(PS5_OUT)/cores/a52.o: $(patsubst %.c,$(PS5_OUT)/cores/a52/%.o,$(A52_SRC))
$(PS5_OUT)/cores/a78.o: $(patsubst %.c,$(PS5_OUT)/cores/a78/%.o,$(A78_SRC))
$(PS5_OUT)/cores/lnx.o: $(patsubst %.cpp,$(PS5_OUT)/cores/lnx/%.o,$(patsubst %.c,$(PS5_OUT)/cores/lnx/%.o,$(LNX_SRC)))
PS5_CORES_OBJ := $(PS5_OUT)/cores/a52.o $(PS5_OUT)/cores/a78.o $(PS5_OUT)/cores/lnx.o

# ---- host (the cores under AddressSanitizer, as Stella's)
$(HOST_OUT)/cores/a52/%.o: %.c $(CORE_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -fPIC $(HOST_CORE_OPT) $(HOST_CORE_SAN) $(A52_FLAGS) -c -o $@ $<
$(HOST_OUT)/cores/a78/%.o: %.c $(CORE_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -fPIC $(HOST_CORE_OPT) $(HOST_CORE_SAN) $(A78_FLAGS) -c -o $@ $<
$(HOST_OUT)/cores/lnx/%.o: %.c $(CORE_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -fPIC $(HOST_CORE_OPT) $(HOST_CORE_SAN) $(LNX_DEFS) -w -c -o $@ $<
$(HOST_OUT)/cores/lnx/%.o: %.cpp $(CORE_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(LNX_CXXFLAGS) -fPIC $(HOST_CORE_OPT) $(HOST_CORE_SAN) $(LNX_DEFS) -w -c -o $@ $<

HOST_TOOLS = NM=nm OBJCOPY=objcopy LD=ld
$(HOST_OUT)/cores/%.o: tools/localize_core.sh
	$(HOST_TOOLS) sh tools/localize_core.sh $@ $(*F)_ $(filter %.o,$^)
$(HOST_OUT)/cores/a52.o: $(patsubst %.c,$(HOST_OUT)/cores/a52/%.o,$(A52_SRC))
$(HOST_OUT)/cores/a78.o: $(patsubst %.c,$(HOST_OUT)/cores/a78/%.o,$(A78_SRC))
$(HOST_OUT)/cores/lnx.o: $(patsubst %.cpp,$(HOST_OUT)/cores/lnx/%.o,$(patsubst %.c,$(HOST_OUT)/cores/lnx/%.o,$(LNX_SRC)))
HOST_CORES_OBJ := $(HOST_OUT)/cores/a52.o $(HOST_OUT)/cores/a78.o $(HOST_OUT)/cores/lnx.o

ps5-cores: $(PS5_CORES_OBJ)
host-cores: $(HOST_CORES_OBJ)
.PHONY: ps5-cores host-cores

# no two parts of the eboot define one name (tools/check_core_symbols.py): Stella (its core and the bridge), each other
# core, the frontend
comma := ,
CHECK_GROUPS = stella=$(subst $(space),$(comma),$(strip $(1))) a52=$(2)/cores/a52.o a78=$(2)/cores/a78.o lnx=$(2)/cores/lnx.o \
	frontend=$(subst $(space),$(comma),$(strip $(3)))
empty :=
space := $(empty) $(empty)
