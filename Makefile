AXIS_USABLE_LIBS = UCLIBC GLIBC
include $(AXIS_TOP_DIR)/tools/build/rules/common.mak

PROGS     = panoramatv

CFLAGS   += -Wall -g -O2

PKGS = glib-2.0 gio-2.0 fixmath axptz axparameter
CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LIBDIR) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LIBDIR) pkg-config --libs $(PKGS))
LDLIBS   += -Wl,-Bstatic,-llicensekey_stat,-Bdynamic,-llicensekey -ldl

SRCS      = axauto.c
OBJS      = $(SRCS:.c=.o)

all: $(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) $(LDLIBS) -o $@

clean:
	rm -f $(PROGS) *.o

