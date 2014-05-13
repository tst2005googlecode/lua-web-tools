APACHE2_BIN = /usr/bin
APXS = apxs
LUA_INCLUDE = /usr/include/lua5.1
LUA_LIB = lua5.1
LUA_INSTALL = /usr/local/share/lua/5.1

all: mod_lwt.la

mod_lwt.la: mod_lwt.c util.h util.c template.h template.c apache.h apache.c
	${APACHE2_BIN}/${APXS} -c -Wc,-Wall -I${LUA_INCLUDE} -l${LUA_LIB} -lrt mod_lwt.c util.c template.c apache.c

install:
	${APACHE2_BIN}/${APXS} -i -a mod_lwt.la
	mkdir -p ${LUA_INSTALL}
	cp httpd.lua ${LUA_INSTALL}
	mkdir -p ${LUA_INSTALL}/httpd
	cp wsapi.lua ${LUA_INSTALL}/httpd

clean:
	-rm *.la
	-rm *.lo
	-rm *.slo
	-rm *.o
