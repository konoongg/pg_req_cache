#ifndef WAL_READER_H
#define WAL_READER_H

#include "access/xlog.h"
#include "access/xlogreader.h"

#define CACHE_SLOT_NAME "cache_req_slot"
#define SLOT_NAME_SIZE 15
#define MAXPG_LSN_LENGTH 17

typedef struct wal_info wal_info;

bool is_valid_LSN(XLogRecPtr read_lsn);
void destroy_wal_info(wal_info* wal);
void update_slot_position(const char* slot_name, XLogRecPtr lsn);
wal_info* init_wal_info (void);
XLogRecord* read_next_XLog_record(wal_info* wal);

struct wal_info {
    XLogRecPtr start_lsn;
    XLogReaderState* xlogreader;
    char slot_name[SLOT_NAME_SIZE];
};

#endif