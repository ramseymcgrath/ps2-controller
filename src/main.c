#include <stdio.h>
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    while (true) {
        printf("ps2-controller: alive on pico2_w\n");
        sleep_ms(1000);
    }
    return 0;
}
