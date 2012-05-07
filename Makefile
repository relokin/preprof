CC = gcc
INSTALL = install

PREFIX = /usr/local

WARN=-Wall -W -Wextra -Wno-long-long -Winline -Wvla -Wno-overlength-strings \
     -Wunsafe-loop-optimizations -Wundef -Wformat=2 -Wlogical-op            \
     -Wsign-compare -Wformat-security -Wmissing-include-dirs                \
     -Wformat-nonliteral -Wold-style-definition -Wpointer-arith             \
     -Winit-self -Wdeclaration-after-statement -Wfloat-equal                \
     -Wmissing-prototypes -Wstrict-prototypes -Wredundant-decls             \
     -Wmissing-declarations -Wmissing-noreturn -Wshadow -Wendif-labels      \
     -Wcast-align -Wwrite-strings -Wno-unused-parameter -Wno-strict-aliasing

CFLAGS=$(WARN) -pthread -g -std=gnu99 $(INCLUDES) -O0 -ffast-math \
     -Wp,-D_FORTIFY_SOURCE=2 -fno-common -fdiagnostics-show-option \
     -fno-omit-frame-pointer -MD -MP -fPIC -D_GNU_SOURCE

LDFLAGS += -shared -ldl -lm -lpthread -lbz2

SRC = preprof.c process.c utils.c log.c
OBJ = preprof.o process.o utils.o log.o

LIBS = libpreprof.so

all: ${LIBS}

%.o: %.c
	${CC} -o $@ -c ${CFLAGS} ${EXTRA_FLAGS} $<

lib%.so: ${OBJ}
	${CC} -o $@ -Wl,-soname,$@ $< ${LDFLAGS}

clean:
	@rm -f *.o *.d

distclean: clean
	@rm -f ${LIBS}

install: ${LIBS}
	@echo
	@echo "Copying the preprof libraries to ${DESTDIR}${PREFIX}/lib/ and the preprof wrapper script to ${DESTDIR}${PREFIX}/bin ..."
	$(INSTALL) -dm0755 "${DESTDIR}${PREFIX}/lib/"
	$(INSTALL) -m0644 ${LIBS} "${DESTDIR}${PREFIX}/lib/"
	$(INSTALL) -Dm0755 preprof.in "${DESTDIR}${PREFIX}/bin/preprof"

uninstall:
	for f in ${LIBS}; do rm -f "${DESTDIR}${PREFIX}/lib/$$f"; done
	rm -f "${DESTDIR}${PREFIX}/bin/preprof"

.PHONY: all clean distclean install uninstall