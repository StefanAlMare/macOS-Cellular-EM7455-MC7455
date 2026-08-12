
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libusb.h>

#define IFACE  3
#define EP_OUT 0x03
#define EP_IN  0x84

static libusb_device_handle *open_modem(libusb_context *ctx)
{
    libusb_device_handle *h;

    h = libusb_open_device_with_vid_pid(ctx, 0x1199, 0x9071);
    if (h)
        return h;

    /* Dell-branded fallback, daca firmware-ul expune acest ID. */
    return libusb_open_device_with_vid_pid(ctx, 0x413c, 0x81b6);
}

static void drain(libusb_device_handle *h)
{
    unsigned char buf[2048];
    int got = 0;

    for (;;) {
        int r = libusb_bulk_transfer(
            h, EP_IN, buf, sizeof(buf), &got, 60
        );

        if (r != 0)
            break;
    }
}

static int send_command(
    libusb_device_handle *h,
    const char *cmd,
    char *out,
    size_t outsz)
{
    char wire[256];
    int sent = 0;
    int r;

    if (out && outsz)
        out[0] = 0;

    snprintf(wire, sizeof(wire), "%s\r", cmd);

    drain(h);

    r = libusb_bulk_transfer(
        h,
        EP_OUT,
        (unsigned char *)wire,
        (int)strlen(wire),
        &sent,
        1200
    );

    if (r != 0)
        return r;

    if (!out || outsz == 0)
        return 0;

    size_t used = 0;

    for (int i = 0; i < 24 && used + 1 < outsz; i++) {
        unsigned char buf[1024];
        int got = 0;

        r = libusb_bulk_transfer(
            h,
            EP_IN,
            buf,
            sizeof(buf),
            &got,
            250
        );

        if (r == LIBUSB_ERROR_TIMEOUT)
            continue;

        if (r != 0)
            return r;

        if (got > 0) {
            size_t copy = (size_t)got;

            if (copy > outsz - used - 1)
                copy = outsz - used - 1;

            memcpy(out + used, buf, copy);
            used += copy;
            out[used] = 0;

            if (strstr(out, "\r\nOK\r\n") ||
                strstr(out, "\r\nERROR\r\n"))
                break;
        }
    }

    return 0;
}

static int parse_operator(
    const char *response,
    char *name,
    size_t namesz)
{
    const char *p = strstr(response, "+COPS:");

    if (!p)
        return 0;

    const char *q1 = strchr(p, '"');

    if (q1) {
        const char *q2 = strchr(q1 + 1, '"');

        if (q2 && q2 > q1 + 1) {
            size_t n = (size_t)(q2 - q1 - 1);

            if (n >= namesz)
                n = namesz - 1;

            memcpy(name, q1 + 1, n);
            name[n] = 0;

            return 1;
        }
    }

    /*
     * Fallback numeric.
     */
    const char *c1 = strchr(p, ',');
    if (!c1)
        return 0;

    const char *c2 = strchr(c1 + 1, ',');
    if (!c2)
        return 0;

    const char *start = c2 + 1;

    while (*start == ' ' || *start == '\t')
        start++;

    const char *end = start;

    while (*end &&
           *end != ',' &&
           *end != '\r' &&
           *end != '\n')
        end++;

    if (end <= start)
        return 0;

    size_t n = (size_t)(end - start);

    if (n >= namesz)
        n = namesz - 1;

    memcpy(name, start, n);
    name[n] = 0;

    return 1;
}

int main(void)
{
    libusb_context *ctx = NULL;
    libusb_device_handle *h = NULL;

    char response[8192];
    char oper[256];

    int r = libusb_init(&ctx);

    if (r != 0)
        return 2;

    h = open_modem(ctx);

    if (!h) {
        libusb_exit(ctx);
        return 3;
    }

    r = libusb_claim_interface(h, IFACE);

    if (r != 0) {
        libusb_close(h);
        libusb_exit(ctx);
        return 4;
    }

    /*
     * Cerem format alfanumeric lung pentru numele operatorului.
     * Nu schimbam inregistrarea in retea.
     */
    send_command(h, "AT+COPS=3,0", response, sizeof(response));

    r = send_command(
        h,
        "AT+COPS?",
        response,
        sizeof(response)
    );

    if (r == 0 &&
        parse_operator(response, oper, sizeof(oper))) {

        printf("OPERATOR=%s\n", oper);
    } else {
        printf("OPERATOR=\n");
    }

    libusb_release_interface(h, IFACE);
    libusb_close(h);
    libusb_exit(ctx);

    return r == 0 ? 0 : 5;
}
