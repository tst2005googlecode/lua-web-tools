LUA_INCLUDE = /usr/include/lua5.1
MEMCACHED_INCLUDE = /usr/include/memcached
LIB_INSTALL = /usr/local/lib/lua/5.1
LUA_INSTALL = /usr/local/share/lua/5.1

all: core.so memcached.so

core.o: core.h core.c
	gcc -c -Wall -fPIC -I${LUA_INCLUDE} core.c

core.so: core.o
	gcc -shared -o core.so core.o

memcached.o: core.h memcached.h memcached.c
	gcc -c -Wall -fPIC -D_REENTRANT -I${LUA_INCLUDE} -I${MEMCACHED_INCLUDE} memcached.c

memcached.so: memcached.o core.o
	gcc -shared -o memcached.so memcached.o core.o

clean:
	-rm core.o core.so
	-rm memcached.o memcached.so

install: all
	mkdir -p ${LIB_INSTALL}/cache
	cp core.so ${LIB_INSTALL}/cache
	cp memcached.so ${LIB_INSTALL}/cache
	mkdir -p ${LUA_INSTALL}
	cp cache.lua ${LUA_INSTALL}
