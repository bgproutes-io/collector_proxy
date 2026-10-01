# Automake input Makefile.am

bin_PROGRAMS = proxy
check_PROGRAMS = test_config
TESTS = $(check_PROGRAMS)

AM_CPPFLAGS = -I$(srcdir)/include $(OPENSSL_CFLAGS) $(LIBBMPROUTES_CFLAGS)
AM_CFLAGS = -Wall -Wextra

proxy_SOURCES = proxy.c \
                src/config.c \
                src/blacklist.c \
                src/commands_def.c \
                src/commands.c \
                src/debug.c \
                src/utils.c \
                include/commands_def.h \
                include/commands.h \
                include/common.h \
                include/config.h \
                include/blacklist.h \
                include/debug.h \
                include/utils.h

proxy_LDADD = $(OPENSSL_LIBS) $(DL_LIBS) @LIBBMPROUTES_LIBS@

test_config_SOURCES = tests/test_config.c \
                      src/config.c \
                      src/blacklist.c \
                      src/utils.c \
                      include/config.h \
                      include/blacklist.h \
                      include/common.h \
                      include/utils.h
test_config_LDADD = $(OPENSSL_LIBS)
