#contrib/pg_redis_proxy/Makefile

MODULE_big = pg_redis_proxy

OBJS = \
	$(WIN32RES) \
	alloc.o \
	cache_gc.o \
	cache_serializer.o \
	cache.o \
	command_processor.o \
	config.o \
	connection.o \
	data_parser.o \
	db.o \
	event.o \
	hash.o \
	ht_response_type.o \
	ht_table_type.o \
	ht.o \
	invalidation/invalid.o \
	invalidation/wal_reader.o \
	io.o \
	pg_req_creater.o \
	query_cache_controller.o \
	redis_proxy.o \
	resp_creater.o \
	socket_wrapper.o \
	stats.o \
	worker.o


EXTENSION = pg_redis_proxy
DATA = pg_redis_proxy--1.1.sql

SHLIB_LINK += -lev -I/home/konoongg/home/postgres/install/include -lpq
PG_CPPFLAGS += -std=c11 -lev -I/home/konoongg/home/postgres/install/include -lpq



ifdef USE_PGXS
	PG_CONFIG = pg_config
	PGXS := $(shell $(PG_CONFIG) --pgxs)
	include $(PGXS)
else
	subdir = contrib/pg_redis_proxy
	top_builddir = ../..
	include $(top_builddir)/src/Makefile.global
    include $(top_srcdir)/contrib/contrib-global.mk
endif