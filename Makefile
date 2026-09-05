# ---------- Configuration ----------
BUILD_MODE ?= debug
SRC_DIR   := src
BUILD_DIR := build
ODIN_BIN  := odin
SCRIPT    := scripts/build_flags.sh

# ---------- Auto-generated flags ----------
# build_flags.mk 共享在 build/ 根，所有 mode 共用
$(BUILD_DIR)/build_flags.mk: $(SCRIPT) $(SRC_DIR)/main.cpp $(SRC_DIR)/libtommath.cpp | $(BUILD_DIR)
	@./$(SCRIPT) > $@

include $(BUILD_DIR)/build_flags.mk

# ---------- Tool flags ----------
WARNINGS := $(DISABLED_WARNINGS)

BUILDFLAGS_debug          := -g
BUILDFLAGS_release        := -O3
# release-native 在 arm 上需要 -mcpu=native，x86 上 -march=native
BUILDFLAGS_release-native := $(if $(filter $(shell uname -m),arm64 aarch64),-O3 -mcpu=native,-O3 -march=native)
BUILDFLAGS_nightly        := -DNIGHTLY -O3
# 用 = 延迟求值，让 cmdline BUILD_MODE 生效后再展开
OPT = $(BUILDFLAGS_$(BUILD_MODE))

# mold 接入
LD := $(CXX)
ifeq ($(USE_MOLD),1)
LD := $(CXX) -fuse-ld=mold
endif

# 用 = 延迟求值
COMMON_FLAGS = $(CPPFLAGS) $(CXXFLAGS_BASE) $(WARNINGS) $(OPT)

# ---------- Sources ----------
UNITY_MAIN_SRC := $(SRC_DIR)/main.cpp
LIBTOMMATH_SRC := $(SRC_DIR)/libtommath.cpp

# ---------- Targets ----------
.PHONY: all debug release release-native nightly clean clean-all demo report build
.DEFAULT_GOAL := all

# 入口 target 都用 recursive make 强制 BUILD_MODE 走 cmdline override，
# 这样 rule 的 prereq 在 parse 时就能拿到正确的 mode（target-specific var
# 只在执行时生效，prereq 在 parse 时就锁定了）。
#
# 子 make 调用 `_build` target；后者用 $(BUILD_MODE) 真正干活。
debug:          ; @$(MAKE) --no-print-directory _build BUILD_MODE=debug
release:        ; @$(MAKE) --no-print-directory _build BUILD_MODE=release
release-native: ; @$(MAKE) --no-print-directory _build BUILD_MODE=release-native
nightly:        ; @$(MAKE) --no-print-directory _build BUILD_MODE=nightly

all: debug

# demo / report 依赖 _build（确保 odin 已构建）
demo report: _build

demo:
	./$(ODIN_BIN) run examples/demo/demo.odin -file

report:
	./$(ODIN_BIN) report

# 真正干活的 target。BUILD_MODE 是 cmdline override，所有路径都正确解析。
.PHONY: _build
_build: $(ODIN_BIN)

# ---------- Build ----------
$(BUILD_DIR):
	@mkdir -p $@

$(BUILD_DIR)/debug $(BUILD_DIR)/release $(BUILD_DIR)/release-native $(BUILD_DIR)/nightly:
	@mkdir -p $@

# unity TU
$(BUILD_DIR)/$(BUILD_MODE)/odin_main.o: $(UNITY_MAIN_SRC) $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/*.hpp) $(SRC_DIR)/gb/gb.h | $(BUILD_DIR)/$(BUILD_MODE) $(BUILD_DIR)/build_flags.mk
	@echo "  CC       $(BUILD_MODE)/odin_main.o"
	$(CXX) $(COMMON_FLAGS) -c $(UNITY_MAIN_SRC) -o $@

# libtommath.cpp
$(BUILD_DIR)/$(BUILD_MODE)/libtommath.o: $(LIBTOMMATH_SRC) | $(BUILD_DIR)/$(BUILD_MODE) $(BUILD_DIR)/build_flags.mk
	@echo "  CC       $(BUILD_MODE)/libtommath.o"
	$(CXX) $(COMMON_FLAGS) -c $< -o $@

# 每个 mode 链接成独立二进制
$(BUILD_DIR)/$(BUILD_MODE)/odin: $(BUILD_DIR)/$(BUILD_MODE)/odin_main.o $(BUILD_DIR)/$(BUILD_MODE)/libtommath.o | $(BUILD_DIR)/$(BUILD_MODE)
	@echo "  LD       $(BUILD_MODE)/odin"
	$(LD) $(BUILD_DIR)/$(BUILD_MODE)/odin_main.o $(BUILD_DIR)/$(BUILD_MODE)/libtommath.o $(LDFLAGS_BASE) -o $@

# 根 ./odin 是 hardlink，指向当前 mode 的二进制
$(ODIN_BIN): $(BUILD_DIR)/$(BUILD_MODE)/odin
	@echo "  LN       ./odin -> $(BUILD_MODE)/odin"
	@ln -f $< $@

# ---------- 工具 ----------
# 只清理当前 mode 的子目录和根 hardlink
clean:
	rm -rf $(BUILD_DIR)/$(BUILD_MODE) $(ODIN_BIN)

# 全清（删整个 build/）
clean-all:
	rm -rf $(BUILD_DIR) $(ODIN_BIN)