
#include "../sds/sds.h"
#include "../list/list.h"
#include "../rax/rax.h"
#include "../zmalloc/zmalloc.h"
#include "../util/sha256.h"
#include "../dict/dict.h"

#define OK  1
#define ERR 1
/* This structure represents a Redis user. This is useful for ACLs, the
 * user is associated to the connection after the connection is authenticated.
 * If there is no associated user, the connection uses the default user. */
#define USER_COMMAND_BITS_COUNT 1024    /* The total number of command bits
                                           in the user structure. The last valid
                                           command ID we can set in the user
                                           is USER_COMMAND_BITS_COUNT-1. */
#define USER_FLAG_ENABLED (1<<0)        /* The user is active. */
#define USER_FLAG_DISABLED (1<<1)       /* The user is disabled. */
#define USER_FLAG_ALLKEYS (1<<2)        /* The user can mention any key. */
#define USER_FLAG_ALLCOMMANDS (1<<3)    /* The user can run all commands. */
#define USER_FLAG_NOPASS      (1<<4)    /* The user requires no password, any
                                           provided password will work. For the
                                           default user, this also means that
                                           no AUTH is needed, and every
                                           connection is immediately
                                           authenticated. */
#define USER_FLAG_ALLCHANNELS (1<<5)    /* The user can mention any Pub/Sub
                                           channel. */
#define USER_FLAG_SANITIZE_PAYLOAD (1<<6)       /* The user require a deep RESTORE
                                                 * payload sanitization. */
#define USER_FLAG_SANITIZE_PAYLOAD_SKIP (1<<7)  /* The user should skip the
                                                 * deep sanitization of RESTORE
                                                 * payload. */

/* =============================================================================
 * Global state for ACLs
 * ==========================================================================*/

extern rax *Users; /* Table mapping usernames to user structures. */

extern list *UsersToLoad; /* This is a list of users found in the configuration file
                       that we'll need to load in the final stage of Redis
                       initialization, after all the modules are already
                       loaded. Every list element is a NULL terminated
                       array of SDS pointers: the first is the user name,
                       all the remaining pointers are ACL rules in the same
                       format as ACLSetUser(). */

typedef struct {
    sds name;       /* The username as an SDS string. */
    uint64_t flags; /* See USER_FLAG_* */
    
    /* The bit in allowed_commands is set if this user has the right to
     * execute this command. In commands having subcommands, if this bit is
     * set, then all the subcommands are also available.
     *
     * If the bit for a given command is NOT set and the command has
     * subcommands, Redis will also check allowed_subcommands in order to
     * understand if the command can be executed. */
    uint64_t allowed_commands[USER_COMMAND_BITS_COUNT/64];

    /* This array points, for each command ID (corresponding to the command
     * bit set in allowed_commands), to an array of SDS strings, terminated by
     * a NULL pointer, with all the sub commands that can be executed for
     * this command. When no subcommands matching is used, the field is just
     * set to NULL to avoid allocating USER_COMMAND_BITS_COUNT pointers. */
    sds **allowed_subcommands;
    list *passwords; /* A list of SDS valid passwords for this user. */
    list *patterns;  /* A list of allowed key patterns. If this field is NULL
                        the user cannot mention any key in a command, unless
                        the flag ALLKEYS is set in the user. */
    list *channels;  /* A list of allowed Pub/Sub channel patterns. If this
                        field is NULL the user cannot mention any channel in a
                        `PUBLISH` or [P][UNSUBSCRIBE] command, unless the flag
                        ALLCHANNELS is set in the user. */
} user;

extern user *DefaultUser; /* Global reference to the default user.
                       Every new connection is associated to it, if no
                       AUTH or HELLO is used to authenticate with a
                       different user. */

extern list *ACLLog;


void ACLInit(void);