
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

    return libusb_open_device_with_vid_pid(ctx, 0x413c, 0x81b6);
}

static int claim_with_retry(libusb_device_handle *h)
{
    /*
     * Helper-ul de semnal foloseste aceeasi interfata AT periodic.
     * Incercam de mai multe ori, fara sa oprim helper-ul sau data plane-ul.
     */
    for (int i = 0; i < 25; i++) {
        int r = libusb_claim_interface(h, IFACE);

        if (r == 0)
            return 0;

        if (r != LIBUSB_ERROR_BUSY)
            return r;

        usleep(120000);
    }

    return LIBUSB_ERROR_BUSY;
}

static void drain(libusb_device_handle *h)
{
    unsigned char buf[4096];
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
    char wire[512];
    int sent = 0;
    int r;

    out[0] = 0;

    snprintf(wire, sizeof(wire), "%s\r", cmd);

    drain(h);

    r = libusb_bulk_transfer(
        h,
        EP_OUT,
        (unsigned char *)wire,
        (int)strlen(wire),
        &sent,
        1500
    );

    if (r != 0)
        return r;

    size_t used = 0;
    int idle_timeouts = 0;

    for (int i = 0; i < 40 && used + 1 < outsz; i++) {
        unsigned char buf[2048];
        int got = 0;

        r = libusb_bulk_transfer(
            h,
            EP_IN,
            buf,
            sizeof(buf),
            &got,
            250
        );

        if (r == LIBUSB_ERROR_TIMEOUT) {
            if (++idle_timeouts >= 4)
                break;
            continue;
        }

        if (r != 0)
            return r;

        idle_timeouts = 0;

        if (got > 0) {
            size_t copy = (size_t)got;

            if (copy > outsz - used - 1)
                copy = outsz - used - 1;

            memcpy(out + used, buf, copy);
            used += copy;
            out[used] = 0;

            if (strstr(out, "\r\nOK\r\n") ||
                strstr(out, "\r\nERROR\r\n") ||
                strstr(out, "+CME ERROR:"))
                break;
        }
    }

    return 0;
}

static void query(libusb_device_handle *h, const char *cmd)
{
    char response[32768];

    printf("\n==================================================\n");
    printf("%s\n", cmd);
    printf("==================================================\n");

    int r = send_command(
        h,
        cmd,
        response,
        sizeof(response)
    );

    if (r != 0) {
        printf("LIBUSB ERROR: %s\n", libusb_error_name(r));
        return;
    }

    printf("%s\n", response[0] ? response : "(empty response)");
}

int main(void)
{
    libusb_context *ctx = NULL;
    libusb_device_handle *h = NULL;

    printf("CELLULAR ACTIVE CONTEXT PROBE - READ ONLY\n");
    printf("Safe to run while Cellular data is connected.\n");
    printf("No SET commands, no profile modification.\n\n");

    int r = libusb_init(&ctx);

    if (r != 0) {
        printf("libusb_init: %s\n", libusb_error_name(r));
        return 1;
    }

    h = open_modem(ctx);

    if (!h) {
        printf("EM7455 not found.\n");
        libusb_exit(ctx);
        return 2;
    }

    r = claim_with_retry(h);

    if (r != 0) {
        printf(
            "Cannot claim AT interface 3: %s\n",
            libusb_error_name(r)
        );
        printf(
            "The signal telemetry helper may have collided with this probe. "
            "Run it again once.\n"
        );
        libusb_close(h);
        libusb_exit(ctx);
        return 3;
    }

    /*
     * All commands below are QUERY/read-only commands.
     */
    query(h, "AT+COPS?");
    query(h, "AT+CGATT?");
    query(h, "AT+CGACT?");
    query(h, "AT+CGDCONT?");
    query(h, "AT+CGCONTRDP");
    query(h, "AT+CGPADDR");
    query(h, "AT!GSTATUS?");

    libusb_release_interface(h, IFACE);
    libusb_close(h);
    libusb_exit(ctx);

    return 0;
}
