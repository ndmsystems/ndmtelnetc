.PHONY: all clean

EXECUTABLE := ndmtelnetc

SRC_DIR    := .

OBJS       := $(foreach d,$(SRC_DIR),$(patsubst %.c,%.o,$(wildcard $d/*.c)))

LIB_PATH   ?= ../libndmtelnet
LIBRARY    := $(LIB_PATH)/libndmtelnet.a

CPPFLAGS   ?= -D_LARGEFILE_SOURCE \
              -D_LARGEFILE64_SOURCE \
              -D_FILE_OFFSET_BITS=64 \
              -D_POSIX_C_SOURCE=200112L \
              -D_BSD_SOURCE \
              -D_XOPEN_SOURCE=600 \
              -D_DEFAULT_SOURCE \
              -I$(LIB_PATH)/include \
              -I$(LIB_PATH)/contrib \
              -MMD

COPTS      := -O2 -flto

CFLAGS     ?= -pipe -fPIC -std=c99 \
              -Wall \
              -Wconversion \
              -Winit-self \
              -Wmissing-field-initializers \
              -Wpointer-arith \
              -Wredundant-decls \
              -Wshadow \
              -Wstack-protector \
              -Wswitch-enum \
              -Wundef \
              -fdata-sections \
              -ffunction-sections \
              -fstack-protector-all \
              -ftabstop=4 \
              $(COPTS)

#CFLAGS    += -fsanitize=address \
              -fsanitize=undefined

LDFLAGS    ?= $(COPTS) -L$(LIB_PATH) -lc -lndmtelnet

#LDFLAGS   += -fsanitize=address \
              -fsanitize=undefined

all: $(EXECUTABLE)

$(EXECUTABLE): $(LIBRARY) $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(OBJS): %.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -fv *~ $(EXECUTABLE) $(OBJS) $(OBJS:.o=.d)

-include $(OBJS:.o=.d)
