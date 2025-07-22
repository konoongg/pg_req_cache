#ifndef INVALID_H
#define INVALID_H

#include "postgres.h"

#include "executor/executor.h"

void inv_process_command(QueryDesc* queryDesc);
void inv_process_xact(XactEvent event, void* arg);
void inv_process_record_update(XLogReaderState* xlogreader);
void inv_process_xac_commit(XLogReaderState* xlogreader);

#endif