LUA_INCLUDE = /usr/include/lua5.1
MYSQL_INCLUDE = /usr/include/mysql
SQLITE3_INCLUDE = /usr/include/sqlite3
LIB_INSTALL = /usr/local/lib/lua/5.1
LUA_INSTALL = /usr/local/share/lua/5.1

all: core.so mysql.so sqlite3.so tds.so

core.o: core.h core.c
	gcc -c -Wall -fPIC core.c -I${LUA_INCLUDE}

core.so: core.o
	gcc -shared -o core.so core.o

mysql.o: core.h mysql.h mysql.c
	gcc -c -Wall -fPIC -pthread -I${LUA_INCLUDE} -I${MYSQL_INCLUDE} mysql.c

mysql.so: mysql.o
	gcc -shared -o mysql.so mysql.o -lmysqlclient_r -lpthread

sqlite3.o: core.h sqlite3.h sqlite3.c
	gcc -c -Wall -fPIC -I${LUA_INCLUDE} -I${SQLITE3_INCLUDE} sqlite3.c

sqlite3.so: sqlite3.o
	gcc -shared -o sqlite3.so sqlite3.o -lsqlite3

tds.o: core.h tds.h tds.c
	gcc -c -Wall -fPIC -pthread -I${LUA_INCLUDE} tds.c

tds.so: tds.o
	gcc -shared -o tds.so tds.o -lsybdb -lpthread

clean:
	-rm core.o core.so
	-rm mysql.o mysql.so
	-rm sqlite3.o sqlite3.so
	-rm tds.o tds.so

install: all
	mkdir -p ${LIB_INSTALL}/is
	cp core.so ${LIB_INSTALL}/is
	cp mysql.so ${LIB_INSTALL}/is
	cp sqlite3.so ${LIB_INSTALL}/is
	cp tds.so ${LIB_INSTALL}/is
	mkdir -p ${LUA_INSTALL}
	cp is.lua ${LUA_INSTALL}
