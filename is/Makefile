LUA_INCLUDE = /usr/include/lua5.1
MYSQL_INCLUDE = /usr/include/mysql
SQLITE3_INCLUDE = /usr/include/sqlite3

INST = /usr/local/lib/lua/5.1

all: is.so mysql.so sqlite3.so tds.so

is.o: is.h is.c
	gcc -c -Wall -fPIC is.c -I${LUA_INCLUDE}

is.so: is.o
	gcc -shared -o is.so is.o

mysql.o: is.h mysql.h mysql.c
	gcc -c -Wall -fPIC -D_REENTRANT -I${LUA_INCLUDE} -I${MYSQL_INCLUDE} mysql.c

mysql.so: mysql.o
	gcc -shared -o mysql.so -lmysqlclient -lpthread mysql.o

sqlite3.o: is.h sqlite3.h sqlite3.c
	gcc -c -Wall -fPIC -I${LUA_INCLUDE} -I${SQLITE3_INCLUDE} sqlite3.c

sqlite3.so: sqlite3.o
	gcc -shared -o sqlite3.so -lsqlite3 sqlite3.o

tds.o: is.h tds.h tds.c
	gcc -c -Wall -fPIC -D_REENTRANT -I${LUA_INCLUDE} tds.c

tds.so: tds.o
	gcc -shared -o tds.so -lsybdb -lpthread tds.o

clean:
	-rm is.o is.so
	-rm mysql.o mysql.so
	-rm sqlite3.o sqlite3.so
	-rm tds.o tds.so

install: all
	mkdir -p ${INST}/is
	cp is.so ${INST}
	cp mysql.so ${INST}/is
	cp sqlite3.so ${INST}/is
	cp tds.so ${INST}/is
