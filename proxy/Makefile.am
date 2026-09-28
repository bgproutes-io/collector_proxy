# Automake input Makefile.am

bin_PROGRAMS = proxy

AM_CPPFLAGS = -I$(srcdir)/include $(OPENSSL_CFLAGS)
AM_CFLAGS = -Wall -Wextra

proxy_SOURCES = proxy.c \
                src/commands_def.c \
                src/commands.c \
                src/debug.c \
                src/utils.c \
                include/commands_def.h \
                include/commands.h \
                include/common.h \
                include/debug.h \
                include/utils.h

proxy_LDADD = $(OPENSSL_LIBS) $(DL_LIBS)
