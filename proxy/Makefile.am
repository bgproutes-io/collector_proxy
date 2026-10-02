# Automake input Makefile.am

bin_PROGRAMS = proxy
check_PROGRAMS = test_circular_buffer test_config test_message_queue test_timers
TESTS = $(check_PROGRAMS)

AM_CPPFLAGS = -I$(srcdir)/include $(OPENSSL_CFLAGS) $(LIBBMPROUTES_CFLAGS)
AM_CFLAGS = -Wall -Wextra

proxy_SOURCES = proxy.c \
                src/config.c \
                src/blacklist.c \
                src/proxy_server.c \
                src/my_tls.c \
                src/timers.c \
                src/commands_def.c \
                src/commands.c \
                src/circular_buffer.c \
                src/debug.c \
                src/utils.c \
                include/commands_def.h \
                include/commands.h \
                include/circular_buffer.h \
                include/common.h \
                include/config.h \
                include/blacklist.h \
                include/llist.h \
                include/my_tls.h \
                include/proxy_server.h \
                include/timers.h \
                include/debug.h \
                include/utils.h

proxy_LDADD = $(OPENSSL_LIBS) $(DL_LIBS) @LIBBMPROUTES_LIBS@

test_circular_buffer_SOURCES = tests/test_circular_buffer.c \
                               src/circular_buffer.c \
                               src/utils.c \
                               include/circular_buffer.h \
                               include/common.h \
                               include/utils.h
test_circular_buffer_LDADD = $(OPENSSL_LIBS)

test_config_SOURCES = tests/test_config.c \
                      src/config.c \
                      src/blacklist.c \
                      src/utils.c \
                      include/config.h \
                      include/blacklist.h \
                      include/common.h \
                      include/utils.h
test_config_LDADD = $(OPENSSL_LIBS)

test_message_queue_SOURCES = tests/test_message_queue.c \
                             src/proxy_server.c \
                             src/blacklist.c \
                             src/my_tls.c \
                             src/timers.c \
                             src/debug.c \
                             src/utils.c \
                             include/proxy_server.h \
                             include/blacklist.h \
                             include/my_tls.h \
                             include/timers.h \
                             include/debug.h \
                             include/utils.h \
                             include/llist.h \
                             include/common.h
test_message_queue_LDADD = $(OPENSSL_LIBS)

test_timers_SOURCES = tests/test_timers.c \
                      src/timers.c \
                      include/timers.h \
                      include/common.h
