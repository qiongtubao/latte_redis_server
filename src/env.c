#include "env.h"
#include <stdio.h>
#include "ae/ae.h"

int env() {
    fprintf(stderr,"latte_env: \n");
    fprintf(stderr,"       %s\n", ae_get_api_name());
    return 1;
}