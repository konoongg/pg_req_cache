#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "postgres.h"

#include "alloc.h"
#include "connection.h"
#include "config.h"
#include "io.h"
#include "resp_creater.h"
#include "storage_data.h"

const char crlf[] = "\r\n";

default_resp_answer def_resp;


/*
*This file contains the functions responsible for generating the response in RESP
*/

// Initialization of standard responses from Redis
void init_def_resp (void) {
    def_resp.ok.answer = "+OK\r\n";
    def_resp.ok.answer_size = 5;

    def_resp.pong.answer = "+PONG\r\n";
    def_resp.pong.answer_size = 7;

    def_resp.aof.answer = "*2\r\n$10\r\nappendonly\r\n$2\r\nno\r\n";
    def_resp.aof.answer_size = 29;

    def_resp.save.answer = "*2\r\n$4\r\nsave\r\n$0\r\n\r\n";
    def_resp.save.answer_size = 20;
}

void create_simple_string_resp(answer* answ, char* src) {
    int answ_size = 1 + strlen(src) + 2;  // +<src>\n\r
    int index = 0;

    answ->answer = wcalloc(answ_size * sizeof(char));
    answ->answer_size = answ_size;
    answ->answer[index] = '+';
    index++;

    memcpy(answ->answer + index, src, strlen(src));
    index += strlen(src);
    memcpy(answ->answer + index, crlf, 2);
}

void create_err_resp(answer* answ, char* src) {
    int answ_size = 1 + strlen(src) + 2;  // -<error message>\r\n - Error
    int index = 0;

    answ->answer = wcalloc(answ_size * sizeof(char));
    answ->answer_size = answ_size;

    answ->answer[index] = '-';
    index++;

    memcpy(answ->answer + index, src, strlen(src));
    index += strlen(src);
    memcpy(answ->answer + index, crlf, 2);
}

void create_num_resp(answer* answ, int num) {
    char str_num[MAX_STR_NUM_SIZE];
    int answ_size;
    int index = 0;

    snprintf(str_num, MAX_STR_NUM_SIZE, "%d", num);

    answ_size = 1 + strlen(str_num) + 2; // :<integer>\r\n

    answ->answer = wcalloc(answ_size * sizeof(char));
    answ->answer_size = answ_size;

    answ->answer[index] = ':';
    index++;
    memcpy(answ->answer + index, str_num, strlen(str_num));
    index += strlen(str_num);
    memcpy(answ->answer + index, crlf, 2);
}

static void create_bulk_string_resp(answer* answ, char* src, int size) {
    int index = 0;
    int answ_size;
    char str_size[MAX_STR_NUM_SIZE];

    snprintf(str_size, MAX_STR_NUM_SIZE, "%d", size);

    answ_size = 1 + strlen(str_size) + 2 + size + 2;  // $<length>\r\n<data>\r\n

    answ->answer = wcalloc(answ_size * sizeof(char));
    answ->answer_size = answ_size;

    answ->answer[index] = '$';
    index++;

    memcpy(answ->answer + index, str_size, strlen(str_size));
    index += strlen(str_size);
    memcpy(answ->answer + index, crlf, 2);
    index += 2;

    memcpy(answ->answer + index, src, size);
    index += size;
    memcpy(answ->answer + index, crlf, 2);
}

/*
* We know that, according to the application logic, we will always return an array of arrays.
* The nested array can contain various simple data types, such as strings or numbers
*/
void create_array_resp(answer* answ, cache_response* res) {
    int index = 0;
    char count_tuple_str[MAX_STR_NUM_SIZE];
    char count_field_str[MAX_STR_NUM_SIZE];
    int answ_size;
    answer* sub_answers;

    snprintf(count_tuple_str, MAX_STR_NUM_SIZE, "%d", res->count_tuples);
    snprintf(count_field_str, MAX_STR_NUM_SIZE, "%d", res->count_fields);

    answ_size = strlen(count_tuple_str) + 1 + 2; //*<size>\r\n
    sub_answers = wcalloc(res->count_tuples * sizeof(answer));

    for (int i = 0; i < res->count_tuples; ++i) {
        int sub_index = 0;
        answer* sub_sub_answer = wcalloc(res->count_fields * sizeof(answer));
        sub_answers[i].answer_size = 1 +  strlen(count_field_str) + 2; // *<count_column>\r\n<answ>
        for (int j = 0; j < res->count_fields; ++j) {
            cache_attr* a = &(res->values[i][j]);
            switch (res->columns[j]->type) {
                case INT:
                    create_num_resp(&(sub_sub_answer[j]), a->data->num);
                    break;
                case STRING:
                    create_bulk_string_resp(&(sub_sub_answer[j]), a->data->str.str, a->data->str.size);
                    break;
            }
            sub_answers[i].answer_size += sub_sub_answer[j].answer_size;
        }

        sub_answers[i].answer = wcalloc(sub_answers[i].answer_size * sizeof(char));
        sub_answers[i].answer[0] = '*';
        sub_index++;
        memcpy(sub_answers[i].answer + sub_index, count_field_str, strlen(count_field_str));
        sub_index += strlen(count_field_str);
        memcpy(sub_answers[i].answer + sub_index, crlf, 2);
        sub_index += 2;
        for (int j = 0; j < res->count_fields; ++j) {
            memcpy(sub_answers[i].answer + sub_index, sub_sub_answer[j].answer, sub_sub_answer[j].answer_size);
            sub_index += sub_sub_answer[j].answer_size;
            free(sub_sub_answer[j].answer);
        }
        free(sub_sub_answer);
        answ_size += sub_answers[i].answer_size;
    }
    answ->answer_size = answ_size;
    answ->answer = wcalloc(answ_size * sizeof(char));

    answ->answer[0] = '*';
    index++;

    memcpy(answ->answer + index, count_tuple_str, strlen(count_tuple_str));
    index += strlen(count_tuple_str);
    memcpy(answ->answer + index, crlf, 2);
    index += 2;

    for (int i = 0; i < res->count_tuples; ++i) {
        memcpy(answ->answer + index, sub_answers[i].answer, sub_answers[i].answer_size);
        index += sub_answers[i].answer_size;
        free(sub_answers[i].answer);
    }

    free(sub_answers);
}
