#include "command_manager.h"
#include "../redis/module.h"

void module_command(redis_client_t* c) {
    char* subcmd = c->argv[1]->ptr;
    if (c->argc == 2 && !strcasecmp(subcmd, "help")) {
        module_help_command(c);
    } else if (!strcasecmp(subcmd,"load") && c->argc >= 3) {
        module_load_command(c);
    } else if (!strcasecmp(subcmd,"unload") && c->argc >= 3) {
        module_unload_command(c);
    } else if (!strcasecmp(subcmd,"list") && c->argc >= 3) {
        module_list_command(c);
    } else {
        // add_reply_subcommand_syntax_error(c); 
    }
}
