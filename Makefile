# FoldQuest (CTR-P-FOLD, UniqueId 0x0D3F7). Gate:
#   make test && python3 -m unittest discover -s tests -p test_contract.py -v && make 3ds && make verify-3ds
# Native commands may be wrapped by a build queue; `make 3ds` also works standalone.
APP_TITLE  := FoldQuest
APP_DESC   := Fold the room by closing the lid
APP_AUTHOR := lafiabrew
TARGET     := 3ds_foldquest

HOST_CC     := gcc
JOURNAL_INC := vendor/lafiabrew_journal/include
JOURNAL_SRC := vendor/lafiabrew_journal/src/native_log_store.c
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -g -Iinclude -I$(JOURNAL_INC)
SAN_FLAGS   := -fsanitize=address,undefined -fno-sanitize-recover=undefined

CORE_SRCS := src/fq_core.c src/fq_lifecycle.c src/fq_log.c src/fq_app.c
ARM_SRCS  := $(CORE_SRCS) src/apt_lifecycle.c src/platform_3ds.c src/main.c \
             $(JOURNAL_SRC) vendor/lafiabrew_journal/src/native_fs_3ds.c

OCI_RUNTIME := $(shell command -v podman 2>/dev/null || command -v docker 2>/dev/null)
OCI_IMAGE   := localhost/pieceduel-3ds-toolchain:latest
OCI_USER    := $(if $(findstring podman,$(OCI_RUNTIME)),--userns=keep-id,-u $(shell id -u):$(shell id -g))
ARM_FLAGS   := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -std=gnu11 -Wall -Wextra -Werror \
               -Os -ffunction-sections -fdata-sections -D__3DS__ -Iinclude -I$(JOURNAL_INC)

.PHONY: all journal-check host test 3ds 3ds-native 3ds-container verify-3ds clean
all: test

# Shared-runtime regression signal only; never counted as app acceptance.
journal-check:
	mkdir -p build/host
	gcc -std=c11 -Wall -Wextra -Werror -O2 -g -fsanitize=address,undefined -I$(JOURNAL_INC) $(JOURNAL_SRC) vendor/lafiabrew_journal/src/native_fs_host.c tests/journal_smoke.c -o build/host/journal_smoke
	./build/host/journal_smoke

build/host:
	@mkdir -p build/host

build/host/test_fq: $(CORE_SRCS) src/platform_host.c $(JOURNAL_SRC) vendor/lafiabrew_journal/src/native_fs_host.c tests/test_fq.c $(wildcard include/*.h) | build/host
	$(HOST_CC) $(HOST_CFLAGS) $(SAN_FLAGS) -DNLS_TEST_HOOKS $(CORE_SRCS) src/platform_host.c $(JOURNAL_SRC) vendor/lafiabrew_journal/src/native_fs_host.c tests/test_fq.c -o $@

build/host/test_apt: src/apt_lifecycle.c src/fq_lifecycle.c src/fq_app.c src/fq_core.c src/fq_log.c src/platform_host.c $(JOURNAL_SRC) vendor/lafiabrew_journal/src/native_fs_host.c tests/test_apt.c tests/shim/3ds.h | build/host
	$(HOST_CC) $(HOST_CFLAGS) $(SAN_FLAGS) -Itests/shim src/apt_lifecycle.c src/fq_lifecycle.c src/fq_app.c src/fq_core.c src/fq_log.c src/platform_host.c $(JOURNAL_SRC) vendor/lafiabrew_journal/src/native_fs_host.c tests/test_apt.c -o $@

# Host SIMULATION of the app (scripted input/lifecycle). Not hardware.
build/host/$(TARGET)_host: $(CORE_SRCS) src/platform_host.c src/main.c $(JOURNAL_SRC) vendor/lafiabrew_journal/src/native_fs_host.c $(wildcard include/*.h) | build/host
	$(HOST_CC) $(HOST_CFLAGS) $(SAN_FLAGS) $(CORE_SRCS) src/platform_host.c src/main.c $(JOURNAL_SRC) vendor/lafiabrew_journal/src/native_fs_host.c -o $@

host: build/host/test_fq build/host/test_apt build/host/$(TARGET)_host

test: journal-check host
	./build/host/test_fq
	./build/host/test_apt

# ---- Native (ARM11, libctru/citro2d). Runs in the toolchain container unless DEVKITARM is set. ----
3ds-native:
	@mkdir -p build/3ds/negative build/meta
	python3 meta/generate_assets.py build/meta
	$(DEVKITARM)/bin/arm-none-eabi-gcc $(ARM_FLAGS) -I$(DEVKITPRO)/libctru/include $(ARM_SRCS) \
		-specs=3dsx.specs -L$(DEVKITPRO)/libctru/lib -Wl,--gc-sections -Wl,-Map=build/3ds/$(TARGET).map \
		-lcitro2d -lcitro3d -lctru -lm -o build/3ds/$(TARGET).elf
	$(DEVKITARM)/bin/arm-none-eabi-strip --strip-debug build/3ds/$(TARGET).elf
	$(DEVKITARM)/bin/arm-none-eabi-objdump -d build/3ds/$(TARGET).elf > build/3ds/$(TARGET).dis
	$(DEVKITPRO)/tools/bin/smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" build/meta/icon.png build/3ds/$(TARGET).smdh
	python3 -c 'import sys; sys.path.insert(0, "meta"); from generate_bnr_shared import patch_smdh_for_cia; patch_smdh_for_cia("build/3ds/$(TARGET).smdh")'
	$(DEVKITPRO)/tools/bin/3dsxtool build/3ds/$(TARGET).elf build/3ds/$(TARGET).3dsx --smdh=build/3ds/$(TARGET).smdh
	makerom -f cia -o build/3ds/$(TARGET).cia -DAPP_ENCRYPTED=false -rsf meta/app.rsf -target t -exefslogo -elf build/3ds/$(TARGET).elf -icon build/3ds/$(TARGET).smdh -banner build/meta/banner.bnr
	makerom -f cxi -o build/3ds/$(TARGET).cxi -DAPP_ENCRYPTED=false -rsf meta/app.rsf -target t -exefslogo -elf build/3ds/$(TARGET).elf -icon build/3ds/$(TARGET).smdh -banner build/meta/banner.bnr
	@# Same-architecture NEGATIVE CONTROL: a launcher that compiles/links for ARM but has no game path.
	$(DEVKITARM)/bin/arm-none-eabi-gcc $(ARM_FLAGS) -I$(DEVKITPRO)/libctru/include tests/stub_launcher.c \
		-specs=3dsx.specs -L$(DEVKITPRO)/libctru/lib -Wl,--gc-sections -Wl,-Map=build/3ds/negative/stub.map \
		-lctru -lm -o build/3ds/negative/stub.elf
	$(DEVKITARM)/bin/arm-none-eabi-objdump -d build/3ds/negative/stub.elf > build/3ds/negative/stub.dis

3ds-container:
	$(OCI_RUNTIME) run --rm $(OCI_USER) -v "$(CURDIR):/work:Z" -w /work $(OCI_IMAGE) make 3ds-native

3ds:
	@if [ -n "$(DEVKITARM)" ] && [ -x "$(DEVKITARM)/bin/arm-none-eabi-gcc" ]; then \
		$(MAKE) 3ds-native; \
	else \
		$(MAKE) 3ds-container; \
	fi

verify-3ds:
	python3 scripts/verify_3ds.py --build build/3ds
	python3 scripts/verify_3ds.py --build build/3ds --negative-control

clean:
	rm -rf build/
