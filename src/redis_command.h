#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <inttypes.h>
/* Number of static key specs */
#define STATIC_KEY_SPECS_NUM 4

#define MAX_KEYS_BUFFER 256
typedef struct getKeysResult {
    int keysbuf[MAX_KEYS_BUFFER];       /* Pre-allocated buffer, to save heap allocations */
    int *keys;                          /* Key indices array, points to keysbuf or heap */
    int numkeys;                        /* Number of key indices return */
    int size; 
} getKeysResult;

#define GETKEYS_RESULT_INIT { {0}, NULL, 0, MAX_KEYS_BUFFER }

/* Key specs definitions.
 *
 * Brief: This is a scheme that tries to describe the location
 * of key arguments better than the old [first,last,step] scheme
 * which is limited and doesn't fit many commands.
 *
 * There are two steps:
 * 1. begin_search (BS): in which index should we start seacrhing for keys?
 * 2. find_keys (FK): relative to the output of BS, how can we will which args are keys?
 *
 * There are two types of BS:
 * 1. index: key args start at a constant index
 * 2. keyword: key args start just after a specific keyword
 *
 * There are two kinds of FK:
 * 1. range: keys end at a specific index (or relative to the last argument)
 * 2. keynum: there's an arg that contains the number of key args somewhere before the keys themselves
 */

typedef enum {
    KSPEC_BS_INVALID = 0, /* Must be 0 */
    KSPEC_BS_UNKNOWN,
    KSPEC_BS_INDEX,
    KSPEC_BS_KEYWORD
} kspec_bs_type;

typedef enum {
    KSPEC_FK_INVALID = 0, /* Must be 0 */
    KSPEC_FK_UNKNOWN,
    KSPEC_FK_RANGE,
    KSPEC_FK_KEYNUM
} kspec_fk_type;

typedef struct {
    /* Declarative data */
    const char *sflags;
    kspec_bs_type begin_search_type;
    union {
        struct {
            /* The index from which we start the search for keys */
            int pos;
        } index;
        struct {
            /* The keyword that indicates the beginning of key args */
            const char *keyword;
            /* An index in argv from which to start searching.
             * Can be negative, which means start search from the end, in reverse
             * (Example: -2 means to start in reverse from the panultimate arg) */
            int startfrom;
        } keyword;
    } bs;
    kspec_fk_type find_keys_type;
    union {
        /* NOTE: Indices in this struct are relative to the result of the begin_search step!
         * These are: range.lastkey, keynum.keynumidx, keynum.firstkey */
        struct {
            /* Index of the last key.
             * Can be negative, in which case it's not relative. -1 indicating till the last argument,
             * -2 one before the last and so on. */
            int lastkey;
            /* How many args should we skip after finding a key, in order to find the next one. */
            int keystep;
            /* If lastkey is -1, we use limit to stop the search by a factor. 0 and 1 mean no limit.
             * 2 means 1/2 of the remaining args, 3 means 1/3, and so on. */
            int limit;
        } range;
        struct {
            /* Index of the argument containing the number of keys to come */
            int keynumidx;
            /* Index of the fist key (Usually it's just after keynumidx, in
             * which case it should be set to keynumidx+1). */
            int firstkey;
            /* How many args should we skip after finding a key, in order to find the next one. */
            int keystep;
        } keynum;
    } fk;

    /* Runtime data */
    uint64_t flags;
} keySpec;

typedef void redisCommandProc(client *c);
typedef int redisGetKeysProc(struct redisCommand *cmd, robj **argv, int argc, getKeysResult *result);
struct redisCommand {
    /* Declarative data */
    char *name;
    redisCommandProc *proc;
    int arity;
    char *sflags;   /* Flags as string representation, one char per flag. */
    keySpec key_specs_static[STATIC_KEY_SPECS_NUM];
    /* Use a function to determine keys arguments in a command line.
     * Used for Redis Cluster redirect. */
    redisGetKeysProc *getkeys_proc;

    /* Runtime data */
    uint64_t flags; /* The actual flags, obtained from the 'sflags' field. */
    /* What keys should be loaded in background when calling this command? */
    long long microseconds, calls, rejected_calls, failed_calls;
    int id;     /* Command ID. This is a progressive ID starting from 0 that
                   is assigned at runtime, and is used in order to check
                   ACLs. A connection is able to execute a given command if
                   the user associated to the connection has this command
                   bit set in the bitmap of allowed commands. */
    keySpec *key_specs;
    keySpec legacy_range_key_spec; /* The legacy (first,last,step) key spec is
                                     * still maintained (if applicable) so that
                                     * we can still support the reply format of
                                     * COMMAND INFO and COMMAND GETKEYS */
    int key_specs_num;
    int key_specs_max;
    int movablekeys; /* See populateCommandMovableKeys */
} redisCommand;