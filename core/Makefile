HOST_CC         ?= gcc
HOST_CXX        ?= g++
HOST_LD         ?= gcc
HOST_AR         ?= ar
HOST_AS         ?= as
HOST_OBJCOPY    ?= objcopy
HOST_OBJDUMP    ?= objdump
HOST_GDB        ?= gdb

HOST_CFLAGS 	:= -I ./ -O0 -g -ggdb
CFLAGS 			:= -I ./ -Os

SRCS = shell/shell.c \
	   shell/print.c \
	   gcode/gcodes.c \
	   control/line.c \
	   control/moves.c \
	   control/control.c \
	   control/planner.c \
	   math/math.c

UNIT_TESTS = unit_tests/test_gcode

OBJECTS_TARGET = $(addprefix build/target/, $(addsuffix .o, $(basename $(SRCS))))
OBJECTS_HOST = $(addprefix build/host/, $(addsuffix .o, $(basename $(SRCS))))

OBJECTS_UNIT = $(addsuffix .o, $(basename $(UNIT_TESTS)))

TARGET_DIR = build
HOST_DIR   = host

all: clean libcore_host.a libcore_target.a unit_tests


build/target/%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

build/host/%.o : %.c
	$(HOST_CC) -c $(HOST_CFLAGS) $< -o $@

libcore_host.a: $(OBJECTS_HOST)
	$(HOST_AR) cr $@ $^

libcore_target.a: $(OBJECTS_TARGET)
	$(AR) cr $@ $^

unit_tests: $(UNIT_TESTS)

unit_tests/% : unit_tests/%.c libcore_host.a
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@
	$@

clean:
	rm -f $(UNIT_TESTS)
	rm -f $(OBJECTS_TARGET)
	rm -f $(OBJECTS_HOST)