#contrib/pg_redis_proxy/Makefile

MODULE_big = pg_redis_proxy

INCLUDE_SUBDIRS := $(shell find . -type d -not -path '*/\.*' -not -path './src*')
PG_CPPFLAGS += $(addprefix -I,$(INCLUDE_SUBDIRS)) -I.

OBJS = \
	$(WIN32RES) \
	src/backend/db.o \
	src/backend/meta_db.o \
	src/backend/parse_pg_command.o \
	src/backend/pg_req_creater.o \
	src/cache/cache_gc.o \
	src/cache/cache_serializer.o \
	src/cache/cache.o \
	src/cache/query_cache_controller.o \
	src/command_processor.o \
	src/config.o \
	src/connection/connection.o \
	src/connection/event.o \
	src/connection/io.o \
	src/connection/socket_wrapper.o \
	src/data_parser.o \
	src/hash_table/hash.o \
	src/hash_table/ht_response_type.o \
	src/hash_table/ht_table_type.o \
	src/hash_table/ht.o \
	src/invalidation/invalid.o \
	src/invalidation/invalid_trans.o \
	src/redis_proxy.o \
	src/resp_creater.o \
	src/stats.o \
	src/utils/alloc.o \
	src/utils/logger.o \
	src/worker.o


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