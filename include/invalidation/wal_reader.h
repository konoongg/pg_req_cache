#ifndef WAL_READER_H
#define WAL_READER_H

#include "access/xlog.h"
#include "access/xlogreader.h"

typedef struct wal_info wal_info;

void destroy_wal_info(wal_info* wal);
wal_info* init_wal_info (void);
XLogRecord* read_next_XLog_record(wal_info* wal);

struct wal_info {
    XLogRecPtr start_lsn;
    XLogReaderState* xlogreader;
};

#endif