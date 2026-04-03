#ifndef __CMD_H__
#define __CMD_H__


#include "config.h"
#include "util.h"


enum {
        CMD_TYPE_EMPTY,
        CMD_TYPE_QUIT,
        CMD_TYPE_SLEEP,
        CMD_TYPE_UNKNOWN,
};


typedef struct cmd {
        int            type;
        int            args_len;
        SpaceTokenizer args[CFG_CMD_ARGS_SIZE];
} Cmd;

void cmd_parse_query(Cmd *c, const char query[]);


#endif
