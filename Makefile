PROJECT := AmiGmail
VERSION := 1.2

ifeq ($(origin CC),default)
CC := m68k-amigaos-gcc
endif
HOST_CC ?= gcc
ifeq ($(origin AR),default)
AR := m68k-amigaos-ar
endif

# Der Installationsprefix des Crosscompilers wird standardmaessig aus dem
# tatsaechlich gefundenen m68k-amigaos-gcc abgeleitet. Unter MSYS/UCRT64
# ergibt /c/amiga-gcc/bin/m68k-amigaos-gcc damit automatisch /c/amiga-gcc.
# Jeder Wert kann weiterhin explizit auf der make-Kommandozeile gesetzt werden.
CC_PATH := $(shell command -v $(CC) 2>/dev/null)
CC_PREFIX := $(shell if [ -n "$(CC_PATH)" ]; then dirname "$$(dirname "$(CC_PATH)")"; fi)

ifeq ($(origin AMIGA_PREFIX),undefined)
ifneq ($(strip $(CC_PREFIX)),)
AMIGA_PREFIX := $(CC_PREFIX)
else
AMIGA_PREFIX := /opt/amiga
endif
endif

NDK_INC ?= $(AMIGA_PREFIX)/m68k-amigaos/ndk-include
REACTION_SDK ?= $(NDK_INC)

# AMISSL_SDK must point at the AmiSSL Developer directory containing
# both include/ and lib/.  Respect an explicitly supplied value first.
# For the common AmiSSL 5 SDK archive layout, auto-detect the copy kept
# below the user's development tree before falling back to AMIGA_PREFIX.
AMISSL_SDK_HOME := $(HOME)/dev/amiga/sdk/AmiSSL-5.27-SDK/AmiSSL/Developer
AMISSL_SDK_PREFIX := $(AMIGA_PREFIX)/m68k-amigaos/amissl
ifeq ($(origin AMISSL_SDK),undefined)
ifneq ($(wildcard $(AMISSL_SDK_HOME)/include/proto/amissl.h),)
AMISSL_SDK := $(AMISSL_SDK_HOME)
else
AMISSL_SDK := $(AMISSL_SDK_PREFIX)
endif
endif
AMISSL_OS3_LIB ?= $(AMISSL_SDK)/lib/AmigaOS3

CPPFLAGS := -Iinclude -I"$(NDK_INC)" -I"$(REACTION_SDK)" -I"$(AMISSL_SDK)/include" -D__USE_INLINE__ -D__USE_BASETYPE__
COMMON_WARN := -Wall -Wextra -Wshadow -Wpointer-arith -Wstrict-prototypes -Wmissing-prototypes -Wformat=2
AMIGA_CFLAGS := -m68020 -msoft-float -fomit-frame-pointer -fno-common $(COMMON_WARN)
AMIGA_LDFLAGS := -m68020 -msoft-float -L"$(AMISSL_OS3_LIB)"
# AmiSSL and bsdsocket are opened explicitly in src/tls.c.  Therefore the
# AmiSSL auto-open library must not be linked as well.  AMISSL_EXTRA_LIBS can
# be set to -lamisslstubs if a future source file passes AmiSSL entry points
# as function pointers.
AMISSL_EXTRA_LIBS ?=
AMIGA_LIBS := $(AMISSL_EXTRA_LIBS) -Wl,--start-group -lc -lstubs -lamiga -Wl,--end-group

SOURCES := src/main.c src/app.c src/common.c src/buffer.c src/account.c src/codec.c \
           src/crypto.c src/imap_parser.c src/mime.c src/oauth.c src/tls.c \
           src/imap.c src/smtp.c src/storage.c src/network_task.c src/gui.c \
           src/charset.c src/i18n.c src/banner_data.c
OBJECTS := $(SOURCES:src/%.c=build/%.o)

HOST_SOURCES := src/common.c src/buffer.c src/account.c src/codec.c src/crypto.c \
                src/imap_parser.c src/mime.c src/oauth.c src/tls.c src/smtp.c \
                src/storage.c src/i18n.c
HOST_TEST := build/host-tests

.PHONY: all release debug clean dist source-dist host-test host-check check-env

all: release

release: CFLAGS := $(AMIGA_CFLAGS) -Os -DNDEBUG
release: check-env bin/$(PROJECT)

debug: CFLAGS := $(AMIGA_CFLAGS) -O0 -g3
debug: clean bin/$(PROJECT)

bin/$(PROJECT): $(OBJECTS) | bin

	$(CC) $(AMIGA_LDFLAGS) -o $@ $(OBJECTS) $(AMIGA_LIBS)

build/%.o: src/%.c | build

	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build bin:

	mkdir -p $@

host-test: $(HOST_TEST)

	./$(HOST_TEST)

host-check: | build

	$(HOST_CC) -std=c99 -O2 $(COMMON_WARN) -Iinclude src/*.c -o build/amigmail-host-check

$(HOST_TEST): tests/test_main.c $(HOST_SOURCES) | build

	$(HOST_CC) -std=c99 -O2 $(COMMON_WARN) -Iinclude $^ -o $@

check-env:

	@command -v $(CC) >/dev/null || { echo "FEHLT: $(CC)"; exit 1; }

	@test -d "$(NDK_INC)" || { echo "FEHLT: NDK_INC=$(NDK_INC)"; echo "Compiler gefunden unter: $(CC_PATH)"; echo "Abgeleiteter AMIGA_PREFIX: $(AMIGA_PREFIX)"; echo "Falls noetig: make NDK_INC=/c/amiga-gcc/m68k-amigaos/ndk-include"; exit 1; }

	@test -d "$(AMISSL_SDK)" || { echo "FEHLT: AMISSL_SDK=$(AMISSL_SDK)"; exit 1; }

	@test -d "$(AMISSL_OS3_LIB)" || { echo "FEHLT: AMISSL_OS3_LIB=$(AMISSL_OS3_LIB)"; exit 1; }

dist: release

	rm -rf dist/$(PROJECT)-$(VERSION)

	mkdir -p dist/$(PROJECT)-$(VERSION)

	cp bin/$(PROJECT) README.md CHANGELOG.md dist/$(PROJECT)-$(VERSION)/

	cp -R config dist/$(PROJECT)-$(VERSION)/

	cd dist && tar -czf $(PROJECT)-$(VERSION)-AmigaOS3.tar.gz $(PROJECT)-$(VERSION)

source-dist:

	rm -f dist/$(PROJECT)-$(VERSION)-source.zip

	cd .. && zip -qr $(PROJECT)/dist/$(PROJECT)-$(VERSION)-source.zip $(PROJECT) \
		-x '$(PROJECT)/build/*' '$(PROJECT)/bin/*' '$(PROJECT)/dist/*'

clean:

	rm -rf build bin dist/$(PROJECT)-$(VERSION) dist/$(PROJECT)-$(VERSION)-AmigaOS3.tar.gz dist/$(PROJECT)-$(VERSION)-source.zip

-include $(OBJECTS:.o=.d)
