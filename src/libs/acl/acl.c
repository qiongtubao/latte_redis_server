#include "acl.h"
#include <string.h>
#include <errno.h>
#include <ctype.h>

void ACLResetSubcommandsForCommand(user *u, unsigned long id);

/* Method for searching for a user within a list of user definitions. The
 * list contains an array of user arguments, and we are only
 * searching the first argument, the username, for a match. */
int ACLListMatchLoadedUser(void *definition, void *user) {
    sds *user_definition = definition;
    return sdscmp(user_definition[0], user) == 0;
}


/* =============================================================================
 * Low level ACL API
 * ==========================================================================*/

/* Return 1 if the specified string contains spaces or null characters.
 * We do this for usernames and key patterns for simpler rewriting of
 * ACL rules, presentation on ACL list, and to avoid subtle security bugs
 * that may arise from parsing the rules in presence of escapes.
 * The function returns 0 if the string has no spaces. */
int ACLStringHasSpaces(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (isspace(s[i]) || s[i] == 0) return 1;
    }
    return 0;
}

/* Flush the entire table of subcommands. This is useful on +@all, -@all
 * or similar to return back to the minimal memory usage (and checks to do)
 * for the user. */
void ACLResetSubcommands(user *u) {
    if (u->allowed_subcommands == NULL) return;
    for (int j = 0; j < USER_COMMAND_BITS_COUNT; j++) {
        if (u->allowed_subcommands[j]) {
            for (int i = 0; u->allowed_subcommands[j][i]; i++)
                sdsfree(u->allowed_subcommands[j][i]);
            zfree(u->allowed_subcommands[j]);
        }
    }
    zfree(u->allowed_subcommands);
    u->allowed_subcommands = NULL;
}

/* The length of the string representation of a hashed password. */
#define HASH_PASSWORD_LEN SHA256_BLOCK_SIZE*2

/* Given an SDS string, returns the SHA256 hex representation as a
 * new SDS string. */
sds ACLHashPassword(unsigned char *cleartext, size_t len) {
    SHA256_CTX ctx;
    unsigned char hash[SHA256_BLOCK_SIZE];
    char hex[HASH_PASSWORD_LEN];
    char *cset = "0123456789abcdef";

    sha256_init(&ctx);
    sha256_update(&ctx,(unsigned char*)cleartext,len);
    sha256_final(&ctx,hash);

    for (int j = 0; j < SHA256_BLOCK_SIZE; j++) {
        hex[j*2] = cset[((hash[j]&0xF0)>>4)];
        hex[j*2+1] = cset[(hash[j]&0xF)];
    }
    return sdsnewlen(hex,HASH_PASSWORD_LEN);
}

/* Given a hash and the hash length, returns OK if it is a valid password
 * hash, or ERR otherwise. */
int ACLCheckPasswordHash(unsigned char *hash, int hashlen) {
    if (hashlen != HASH_PASSWORD_LEN) {
        return ERR;
    }

    /* Password hashes can only be characters that represent
     * hexadecimal values, which are numbers and lowercase
     * characters 'a' through 'f'. */
    for(int i = 0; i < HASH_PASSWORD_LEN; i++) {
        char c = hash[i];
        if ((c < 'a' || c > 'f') && (c < '0' || c > '9')) {
            return ERR;
        }
    }
    return OK;
}

/**
 *  on + off
 *  
 *   skip-sanitize-payload + sanitize-payload
 * 
 *   allkeys + resetkeys
 * 
 *   allchannels + resetchannels
 * 
 *   allcommands + nocommands
 * 
 *   nopass + resetpass
 * 
 *   op[0] == ">" or "!"
 * 
 *   
 */

int ACLSetUser(user *u, const char *op, ssize_t oplen) {
    if (oplen == -1) oplen = strlen(op);
    if (oplen == 0) return OK;
    if (!strcasecmp(op, "on")) {
        u->flags |= USER_FLAG_ENABLED;
        u->flags &= ~USER_FLAG_DISABLED;
    } else if (!strcasecmp(op, "off")) {
        u->flags |= USER_FLAG_DISABLED;
        u->flags &= ~USER_FLAG_ENABLED;
    } else if (!strcasecmp(op,"skip-sanitize-payload")) {
        u->flags |= USER_FLAG_SANITIZE_PAYLOAD_SKIP;
        u->flags &= ~USER_FLAG_SANITIZE_PAYLOAD;
    } else if (!strcasecmp(op,"sanitize-payload")) {
        u->flags &= ~USER_FLAG_SANITIZE_PAYLOAD_SKIP;
        u->flags |= USER_FLAG_SANITIZE_PAYLOAD;
    } else if (!strcasecmp(op,"allkeys") ||
               !strcasecmp(op,"~*"))
    {
        u->flags |= USER_FLAG_ALLKEYS;
        listEmpty(u->patterns);
    } else if (!strcasecmp(op,"resetkeys")) {
        u->flags &= ~USER_FLAG_ALLKEYS;
        listEmpty(u->patterns);
    } else if (!strcasecmp(op,"allchannels") ||
               !strcasecmp(op,"&*"))
    {
        u->flags |= USER_FLAG_ALLCHANNELS;
        listEmpty(u->channels);
    } else if (!strcasecmp(op,"resetchannels")) {
        u->flags &= ~USER_FLAG_ALLCHANNELS;
        listEmpty(u->channels);
    } else if (!strcasecmp(op,"allcommands") ||
               !strcasecmp(op,"+@all"))
    {
        memset(u->allowed_commands,255,sizeof(u->allowed_commands));
        u->flags |= USER_FLAG_ALLCOMMANDS;
        ACLResetSubcommands(u);
    } else if (!strcasecmp(op,"nocommands") ||
               !strcasecmp(op,"-@all"))
    {
        memset(u->allowed_commands,0,sizeof(u->allowed_commands));
        u->flags &= ~USER_FLAG_ALLCOMMANDS;
        ACLResetSubcommands(u);
    } else if (!strcasecmp(op,"nopass")) {
        u->flags |= USER_FLAG_NOPASS;
        listEmpty(u->passwords);
    } else if (!strcasecmp(op,"resetpass")) {
        u->flags &= ~USER_FLAG_NOPASS;
        listEmpty(u->passwords);
    } else if (op[0] == '>' || op[0] == '#') {
        sds newpass;
        if (op[0] == '>') {
            newpass = ACLHashPassword((unsigned char*)op+1,oplen-1);
        } else {
            if (ACLCheckPasswordHash((unsigned char*)op+1,oplen-1) == ERR) {
                errno = EBADMSG;
                return ERR;
            }
            newpass = sdsnewlen(op+1,oplen-1);
        }

        listNode *ln = listSearchKey(u->passwords,newpass);
        /* Avoid re-adding the same password multiple times. */
        if (ln == NULL)
            listAddNodeTail(u->passwords,newpass);
        else
            sdsfree(newpass);
        u->flags &= ~USER_FLAG_NOPASS;
    } else if (op[0] == '<' || op[0] == '!') {
        sds delpass;
        if (op[0] == '<') {
            delpass = ACLHashPassword((unsigned char*)op+1,oplen-1);
        } else {
            if (ACLCheckPasswordHash((unsigned char*)op+1,oplen-1) == ERR) {
                errno = EBADMSG;
                return ERR;
            }
            delpass = sdsnewlen(op+1,oplen-1);
        }
        listNode *ln = listSearchKey(u->passwords,delpass);
        sdsfree(delpass);
        if (ln) {
            listDelNode(u->passwords,ln);
        } else {
            errno = ENODEV;
            return ERR;
        }
    } else if (op[0] == '~') {
        if (u->flags & USER_FLAG_ALLKEYS) {
            errno = EEXIST;
            return ERR;
        }
        if (ACLStringHasSpaces(op+1,oplen-1)) {
            errno = EINVAL;
            return ERR;
        }
        sds newpat = sdsnewlen(op+1,oplen-1);
        listNode *ln = listSearchKey(u->patterns,newpat);
        /* Avoid re-adding the same key pattern multiple times. */
        if (ln == NULL)
            listAddNodeTail(u->patterns,newpat);
        else
            sdsfree(newpat);
        u->flags &= ~USER_FLAG_ALLKEYS;
    } else if (op[0] == '&') {
        if (u->flags & USER_FLAG_ALLCHANNELS) {
            errno = EISDIR;
            return ERR;
        }
        if (ACLStringHasSpaces(op+1,oplen-1)) {
            errno = EINVAL;
            return ERR;
        }
        sds newpat = sdsnewlen(op+1,oplen-1);
        listNode *ln = listSearchKey(u->channels,newpat);
        /* Avoid re-adding the same channel pattern multiple times. */
        if (ln == NULL)
            listAddNodeTail(u->channels,newpat);
        else
            sdsfree(newpat);
        u->flags &= ~USER_FLAG_ALLCHANNELS;
    } else if (op[0] == '+' && op[1] != '@') {
        if (strchr(op,'|') == NULL) {
            //if (ACLLookupCommand(op+1) == NULL) {
            //    errno = ENOENT;
            //    return ERR;
            //}
            // unsigned long id = ACLGetCommandID(op+1);
            // ACLSetUserCommandBit(u,id,1);
            // ACLResetSubcommandsForCommand(u,id);
        } else {
            /* Split the command and subcommand parts. */
            // char *copy = zstrdup(op+1);
            // char *sub = strchr(copy,'|');
            // sub[0] = '\0';
            // sub++;

            // /* Check if the command exists. We can't check the
            //  * subcommand to see if it is valid. */
            // if (ACLLookupCommand(copy) == NULL) {
            //     zfree(copy);
            //     errno = ENOENT;
            //     return ERR;
            // }

            // /* The subcommand cannot be empty, so things like DEBUG|
            //  * are syntax errors of course. */
            // if (strlen(sub) == 0) {
            //     zfree(copy);
            //     errno = EINVAL;
            //     return ERR;
            // }

            // unsigned long id = ACLGetCommandID(copy);
            // /* Add the subcommand to the list of valid ones, if the command is not set. */
            // if (ACLGetUserCommandBit(u,id) == 0) {
            //     ACLAddAllowedSubcommand(u,id,sub);
            // }

            // zfree(copy);
        }
    } else if (op[0] == '-' && op[1] != '@') {
        // if (ACLLookupCommand(op+1) == NULL) {
        //     errno = ENOENT;
        //     return ERR;
        // }
        // unsigned long id = ACLGetCommandID(op+1);
        // ACLSetUserCommandBit(u,id,0);
        // ACLResetSubcommandsForCommand(u,id);
    } else if ((op[0] == '+' || op[0] == '-') && op[1] == '@') {
        // int bitval = op[0] == '+' ? 1 : 0;
        // if (ACLSetUserCommandBitsForCategory(u,op+2,bitval) == ERR) {
        //     errno = ENOENT;
        //     return ERR;
        // }
    } else if (!strcasecmp(op,"reset")) {
        // serverAssert(ACLSetUser(u,"resetpass",-1) == OK);
        // serverAssert(ACLSetUser(u,"resetkeys",-1) == OK);
        // serverAssert(ACLSetUser(u,"resetchannels",-1) == OK);
        // if (server.acl_pubsub_default & USER_FLAG_ALLCHANNELS)
        //     serverAssert(ACLSetUser(u,"allchannels",-1) == OK);
        // serverAssert(ACLSetUser(u,"off",-1) == OK);
        // serverAssert(ACLSetUser(u,"sanitize-payload",-1) == OK);
        // serverAssert(ACLSetUser(u,"-@all",-1) == OK);
    } else {
        errno = EINVAL;
        return ERR;
    }
    return OK;
}

/* Method for passwords/pattern comparison used for the user->passwords list
 * so that we can search for items with listSearchKey(). */
int ACLListMatchSds(void *a, void *b) {
    return sdscmp(a,b) == 0;
}

/* Method to free list elements from ACL users password/patterns lists. */
void ACLListFreeSds(void *item) {
    sdsfree(item);
}

/* Method to duplicate list elements from ACL users password/patterns lists. */
void *ACLListDupSds(void *item) {
    return sdsdup(item);
}

/* Create a new user with the specified name, store it in the list
 * of users (the Users global radix tree), and returns a reference to
 * the structure representing the user.
 *
 * If the user with such name already exists NULL is returned. */
user *ACLCreateUser(const char *name, size_t namelen) {
    if (raxFind(Users,(unsigned char*)name,namelen) != raxNotFound) return NULL;
    user *u = zmalloc(sizeof(*u));
    u->name = sdsnewlen(name,namelen);
    // u->flags = USER_FLAG_DISABLED | server.acl_pubsub_default;
    u->flags = USER_FLAG_DISABLED;
    u->allowed_subcommands = NULL;
    u->passwords = listCreate();
    u->patterns = listCreate();
    u->channels = listCreate();
    listSetMatchMethod(u->passwords,ACLListMatchSds);
    listSetFreeMethod(u->passwords,ACLListFreeSds);
    listSetDupMethod(u->passwords,ACLListDupSds);
    listSetMatchMethod(u->patterns,ACLListMatchSds);
    listSetFreeMethod(u->patterns,ACLListFreeSds);
    listSetDupMethod(u->patterns,ACLListDupSds);
    listSetMatchMethod(u->channels,ACLListMatchSds);
    listSetFreeMethod(u->channels,ACLListFreeSds);
    listSetDupMethod(u->channels,ACLListDupSds);
    memset(u->allowed_commands,0,sizeof(u->allowed_commands));
    raxInsert(Users,(unsigned char*)name,namelen,u,NULL);
    return u;
}

user *ACLCreateDefaultUser(void) {
    user *new = ACLCreateUser("default", 7);
    ACLSetUser(new, "+@all", -1);
    ACLSetUser(new, "~*", -1);
    ACLSetUser(new, "&*", -1);
    ACLSetUser(new, "on", -1);
    ACLSetUser(new, "nopass", -1);
    return new;
}


/* Initialization of the ACL subsystem. */
void ACLInit(void) {
    Users = raxNew();
    UsersToLoad = listCreate();
    listSetMatchMethod(UsersToLoad, ACLListMatchLoadedUser);
    ACLLog = listCreate();
    DefaultUser = ACLCreateDefaultUser();
}