# Makefile para Windows — usar con mingw32-make (C:\Qt\Tools\mingw1310_64\bin)
# desde cmd o PowerShell:
#
#   mingw32-make              -> configura (si hace falta) y compila
#   mingw32-make run          -> lanza la app con los DLLs en PATH
#   mingw32-make run VIDEO="D:\ruta\partido.mp4"
#   mingw32-make clean        -> borra el directorio de build
#   mingw32-make rebuild      -> clean + build
#
# Las rutas se pueden sobreescribir: mingw32-make QT_DIR=C:/Qt/6.12.0/mingw_64

SHELL = cmd.exe
.SHELLFLAGS = /C

BUILD_DIR       ?= build
BUILD_TYPE      ?= Release
QT_DIR          ?= C:/Qt/6.11.0/mingw_64
MINGW_DIR       ?= C:/Qt/Tools/mingw1310_64
NINJA           ?= C:/Qt/Tools/Ninja/ninja.exe
VCPKG_ROOT      ?= C:/vcpkg
VCPKG_INSTALLED ?= $(CURDIR)/vcpkg_installed
VIDEO           ?=

APP = $(BUILD_DIR)/bin/pepe_track_players.exe

# PATH de runtime/build en formato Windows (backslashes)
MINGW_BIN = $(subst /,\,$(MINGW_DIR))\bin
VCPKG_BIN = $(subst /,\,$(VCPKG_INSTALLED))\x64-mingw-dynamic\bin

CMAKE_FLAGS = \
    -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
    -DCMAKE_PREFIX_PATH="$(QT_DIR)" \
    -DCMAKE_CXX_COMPILER="$(MINGW_DIR)/bin/g++.exe" \
    -DCMAKE_MAKE_PROGRAM="$(NINJA)" \
    -DCMAKE_TOOLCHAIN_FILE="$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic \
    -DVCPKG_HOST_TRIPLET=x64-mingw-dynamic \
    -DVCPKG_INSTALLED_DIR="$(VCPKG_INSTALLED)"

.PHONY: all configure build run clean rebuild

all: build

# Reconfigura solo si no existe el build.ninja
$(BUILD_DIR)/build.ninja:
	cmake -S . -B $(BUILD_DIR) -G Ninja $(CMAKE_FLAGS)

configure:
	cmake -S . -B $(BUILD_DIR) -G Ninja $(CMAKE_FLAGS)

build: $(BUILD_DIR)/build.ninja
	set "PATH=$(MINGW_BIN);%PATH%" && cmake --build $(BUILD_DIR)
	@echo.
	@echo Built in $(BUILD_DIR)\bin

run:
	set "PATH=$(VCPKG_BIN);$(MINGW_BIN);%PATH%" && start "" "$(subst /,\,$(APP))" $(VIDEO)

clean:
	cmake -E rm -rf $(BUILD_DIR)

rebuild: clean build
