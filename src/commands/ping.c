#include "command_manager.h"
#include "../shared/shared.h"

void ping_command(redis_client_t* c) {
    if (c->argc > 2) {
        add_reply_error_format(c, "wrong number of arguments for '%s' command",
            c->cmd->name);
        return;
    }
    if (c->argc == 1) {
        add_reply(c, shared.pong);
    } else {
        add_reply_bulk(c, c->argv[1]);
    }
}
