LUA_INCLUDE = /usr/include/lua5.1
MEMCACHED_INCLUDE = /usr/include/memcached

INST = /usr/local/lib/lua/5.1

all: cache.so memcached.so

cache.o: cache.h cache.c
	gcc -c -Wall -fPIC -I${LUA_INCLUDE} cache.c

cache.so: cache.o
	gcc -shared -o cache.so cache.o

memcached.o: cache.h memcached.h memcached.c
	gcc -c -Wall -fPIC -D_REENTRANT -I${LUA_INCLUDE} -I${MEMCACHED_INCLUDE} memcached.c

memcached.so: memcached.o cache.o
	gcc -shared -o memcached.so memcached.o cache.o

clean:
	-rm cache.o cache.so
	-rm memcached.o memcached.so

install: all
	mkdir -p ${INST}/cache
	cp cache.so ${INST}
	cp memcached.so ${INST}/cache
