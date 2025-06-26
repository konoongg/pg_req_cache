#include <errno.h>
#include <stdbool.h>

#include "postgres.h"
#include "utils/elog.h"

#include "alloc.h"
#include "connection.h"
#include "data_parser.h"
#include "io.h"

// saves data to a buffer if more than one packet has been written off
static void replace_part_of_buffer(io_read* data, int cur_buffer_index) {
    memmove(data->read_buffer, data->read_buffer + cur_buffer_index, data->cur_buffer_size - cur_buffer_index);
    data->cur_buffer_size -=  cur_buffer_index;
}

/*
 * the function works on the basis of a finite state machine,
 * it takes a character from the buffer and, depending on the state of the machine,
 * performs the necessary actions
 * The parsing state is preserved if the data arrives incomplete;
 * the received data will be processed, and the parsing state is saved. When the remaining data arrives,
 * parsing will continue correctly.
 */
exit_status  pars_data(io_read* data) {
    int cur_buffer_index = 0;
    for (; cur_buffer_index < data->cur_buffer_size; ++cur_buffer_index) {
        char c = data->read_buffer[cur_buffer_index];

        read_status cur_status = data->pars.cur_read_status;
        char* new_str;

        if (c == '*' && cur_status == ARRAY_WAIT) {
            data->pars.cur_read_status = ARGC_WAIT;
        } else if ((c >= '0' && c <= '9')  && (cur_status == NUM_WAIT || cur_status == ARGC_WAIT) ) {
            data->pars.parsing_num = (data->pars.parsing_num * 10) + (c - '0');
        } else if (c == '\r' && cur_status == ARGC_WAIT) {
            if (data->reqs->first == NULL) {
                data->reqs->first = (client_req*)wcalloc(sizeof(client_req));
                data->reqs->last = data->reqs->first;
            } else {
                data->reqs->last->next = (client_req*)wcalloc(sizeof(client_req));
                data->reqs->last = data->reqs->last->next;
            }
            data->reqs->last->next = NULL;
            data->reqs->last->is_ready = false;;
            data->reqs->last->argc = data->pars.parsing_num;
            data->reqs->last->argv = wcalloc(data->reqs->last->argc * sizeof(char*));
            data->reqs->last->argv_size = wcalloc(data->reqs->last->argc * sizeof(int));

            data->pars.parsing_num = 0;
            data->pars.next_read_status = START_STRING_WAIT;
            data->pars.cur_read_status = LF;
        } else if (c == '\r' && cur_status == CR) {
            data->pars.cur_read_status = LF;
        } else if (c == '\n' && cur_status == LF) {
            data->pars.cur_read_status = data->pars.next_read_status;
        } else if (c == '$' && cur_status == START_STRING_WAIT) {
            data->pars.cur_read_status = NUM_WAIT;
        } else if (c == '\r' && cur_status == NUM_WAIT) {
            data->pars.size_str = data->pars.parsing_num;
            data->pars.cur_size_str = 0;
            data->pars.parsing_str = (char*)wcalloc((data->pars.size_str + 1)  * sizeof(char));
            data->pars.next_read_status = STRING_WAIT;
            data->pars.cur_read_status = LF;
            data->pars.parsing_num = 0;
        } else if (cur_status == STRING_WAIT) {
            data->pars.parsing_str[data->pars.cur_size_str] = c;
            data->pars.cur_size_str += 1;

            if (data->pars.cur_size_str == data->pars.size_str) {
                data->pars.parsing_str[data->pars.size_str] = '\0';
                new_str = (char*)wcalloc((data->pars.size_str + 1) * sizeof(char));
                memcpy(new_str, data->pars.parsing_str, data->pars.size_str + 1);
                data->reqs->last->argv[data->pars.cur_count_argv] = new_str;

                data->reqs->last->argv_size[data->pars.cur_count_argv] = data->pars.cur_size_str;

                free(data->pars.parsing_str);
                data->pars.parsing_str = NULL;
                data->pars.cur_count_argv++;
                data->pars.size_str = data->pars.cur_size_str = 0;

                if (data->pars.cur_count_argv == data->reqs->last->argc) {
                    data->pars.cur_count_argv = 0;
                    data->pars.cur_read_status = CR;
                    data->pars.next_read_status = END;
                } else {
                    data->pars.cur_read_status = CR;
                    data->pars.next_read_status = START_STRING_WAIT;
                }
            }
        } else if(cur_status == END) {
            replace_part_of_buffer(data, cur_buffer_index);
            data->pars.cur_read_status = ARRAY_WAIT;
            data->reqs->last->is_ready = true;
            return ALL;
        } else {
            return ERR;
        }
    }

    if(data->pars.cur_read_status == END) {
        data->pars.cur_read_status = ARRAY_WAIT;
        data->reqs->last->is_ready = true;
        replace_part_of_buffer(data, cur_buffer_index);
        return ALL;
    }
    return NOT_ALL;
}
