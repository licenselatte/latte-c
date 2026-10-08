/*
 * machineid_test: prints the machine_id this machine sends for a fixed app ID.
 *
 * Usage:
 *   ./machineid_test
 */
#include <stdio.h>

#include "../src/machineid.h"

#define APP_ID "test_1234567890"

int main(void)
{
    char out[65];
    if (ll_machine_id_protected(APP_ID, out) < 0) {
        fprintf(stderr, "ll_machine_id_protected failed\n");
        return 1;
    }
    printf("%s\n", out);
    return 0;
}
