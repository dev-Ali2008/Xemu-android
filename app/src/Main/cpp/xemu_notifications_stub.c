#include <stdio.h>

void xemu_queue_notification(const char *msg)
{
    if (msg) {
        fprintf(stderr, "[xemu] %s\n", msg);
    }
}

void xemu_queue_error_message(const char *msg)
{
    if (msg) {
        fprintf(stderr, "[xemu][error] %s\n", msg);
    }
}
