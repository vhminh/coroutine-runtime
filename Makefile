.PHONY: all lib examples run clean fmt dev dev-x86

SRC_DIR     := src
HDR_DIR     := src
EXAMPLE_DIR := examples
OUT_DIR     := out

LIB := $(OUT_DIR)/libcoroutine-runtime.a

SRCS         := arch/aarch64.cpp \
                arch/x86_64.cpp \
                context.cpp \
                fiber.cpp \
                uring-version.cpp \

EXAMPLE_SRCS := echo_server.cpp \
                probe-uring-version.cpp \
                sleep_sort.cpp \
                yield.cpp

CXX ?= g++
AR  ?= ar
CXXFLAGS += -O2 -std=c++20 -Wall -Wextra -Werror=attributes -MMD -MP -I $(HDR_DIR)
LIBS := liburing
CXXFLAGS += $(shell pkg-config --cflags $(LIBS))
LDFLAGS += $(shell pkg-config --libs $(LIBS))

OBJ_DIR      := $(OUT_DIR)/obj
LIB_OBJS     := $(SRCS:%.cpp=$(OBJ_DIR)/%.o)
EXAMPLE_BINS := $(EXAMPLE_SRCS:%.cpp=$(OUT_DIR)/$(EXAMPLE_DIR)/%)

DEPS := $(LIB_OBJS:.o=.d) $(EXAMPLE_BINS:%=%.d)

all: $(LIB) $(EXAMPLE_BINS)

$(LIB): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(LIB_OBJS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT_DIR)/$(EXAMPLE_DIR)/%: $(EXAMPLE_DIR)/%.cpp $(LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MF $@.d $< -o $@ $(LIB) $(LDFLAGS)

fmt:
	find $(SRC_DIR) $(EXAMPLE_DIR) -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -o -name "*.cc" \) \
	  | xargs clang-format -i

clean:
	rm -rf $(OUT_DIR)

dev:
	docker build -t coroutine-runtime-dev - < Dockerfile
	docker run --rm -it -p 8080:8080 --security-opt seccomp=unconfined --cap-add SYS_PTRACE -v "$$PWD":/work coroutine-runtime-dev bash
dev-x86:
	docker build --platform linux/amd64 -t coroutine-runtime-dev-x86 - < Dockerfile
	docker run --rm -it -p 8080:8080 --platform linux/amd64 --security-opt seccomp=unconfined --cap-add SYS_PTRACE -v "$$PWD":/work coroutine-runtime-dev-x86 bash

-include $(DEPS)
