#include <assert.h>
#include <string.h>

#include "cmd.h"
#include "util.h"


static void _handle_sleep(Cmd *c, SpaceTokenizer *st, const char *arg);
static void _handle_quit(Cmd *c);


void
cmd_parse_query(Cmd *c, char buffer[], int buffer_size, const char query[])
{
        SpaceTokenizer st;
        const char *const next = space_tokenizer_next(&st, query);
        if (next == NULL) {
                c->type = CMD_TYPE_EMPTY;
                c->args_len = 0;
                return;
        }

        if (strncmp(st.value, "sleep", 5) == 0) {
                _handle_sleep(c, &st, st.value);
                return;
        }

        if (strcmp(st.value, "q") == 0) {
                _handle_quit(c);
                return;
        }

        c->type = CMD_TYPE_UNKNOWN;
        c->args_len = 0;
}


/*
 * Private
 */
static void
_handle_sleep(Cmd *c, SpaceTokenizer *st, const char *arg)
{
        log_info("%s", st->value);

        c->type = CMD_TYPE_SLEEP;
        //log_info("|%s|", arg);

        //c->type = CMD_TYPE_SLEEP;
        c->args_len = 1;
}


static void
_handle_quit(Cmd *c)
{
        c->type = CMD_TYPE_QUIT;
        c->args_len = 0;
}
