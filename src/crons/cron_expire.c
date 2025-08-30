
int active_expire_effort = 1;
#define ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP 20 /* Keys for each DB loop. */
void activeExpireCycle(int type) {
    unsigned long
        effort = active_expire_effort-1, /* Rescale from 0 to 9. */
        config_keys_per_loop = ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP +
                           ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP/4*effort,
}


