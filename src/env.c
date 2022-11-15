#include "env.h"
#include <stdio.h>
#include "ae/ae.h"

int env() {
    fprintf(stderr,"latte_env: \n");
    fprintf(stderr,"       %s\n", aeGetApiName());
    return 1;
}