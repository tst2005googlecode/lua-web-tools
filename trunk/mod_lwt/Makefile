APACHE2_BIN = /usr/bin
APXS = apxs2
LUA_INCLUDE = /usr/include/lua5.1

all: mod_lwt.la

mod_lwt.la: mod_lwt.c util.h util.c template.h template.c apache.h apache.c
	${APACHE2_BIN}/${APXS} -c -Wc,-Wall -I${LUA_INCLUDE} -llua5.1 mod_lwt.c util.c template.c apache.c

install:
	${APACHE2_BIN}/${APXS} -i -a mod_lwt.la

clean:
	-rm *.o
	-rm *.la
	-rm *.lo
	-rm *.slo
