#include <assert.h>
#include <string.h>

#include "cmd.h"
#include "util.h"


static void _handle_quit(Cmd *c);
static void _handle_sleep(Cmd *c, const char *arg);
static void _handle_repeat(Cmd *c, const char *arg);


void
cmd_parse_query(Cmd *c, const char query[])
{
        SpaceTokenizer st;
        const char *const next = space_tokenizer_next(&st, query);
        if (next == NULL) {
                c->type = CMD_TYPE_EMPTY;
                c->args_len = 0;
                return;
        }

        if (strcmp(st.value, "q") == 0) {
                _handle_quit(c);
                return;
        }

        if (strncmp(st.value, "sleep", 5) == 0) {
                _handle_sleep(c, next);
                return;
        }

        if (strncmp(st.value, "repeat", 6) == 0) {
                _handle_repeat(c, next);
                return;
        }

        c->type = CMD_TYPE_UNKNOWN;
        c->args_len = 0;
}


/*
 * Private
 */
static void
_handle_quit(Cmd *c)
{
        c->type = CMD_TYPE_QUIT;
        c->args_len = 0;
}


static void
_handle_sleep(Cmd *c, const char *arg)
{
        c->type = CMD_TYPE_SLEEP;
        if (space_tokenizer_next(&c->args[0], arg) == NULL) {
                c->args_len = 0;
                return;
        }

        c->args_len = 1;
}


static void
_handle_repeat(Cmd *c, const char *arg)
{
        c->type = CMD_TYPE_REPEAT;
        if (space_tokenizer_next(&c->args[0], arg) == NULL) {
                c->args_len = 0;
                return;
        }

        c->args_len = 1;
}
