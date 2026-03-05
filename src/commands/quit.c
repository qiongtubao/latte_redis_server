#include "command_manager.h"
#include "../shared/shared.h"

void quit_command(redis_client_t* c) {
    add_reply(c, shared.ok);
    c->client.flags |= CLIENT_CLOSE_AFTER_REPLY;
}
