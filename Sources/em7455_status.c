
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

    for (int i = 0; i < 20 && used + 1 < outsz; i++) {
        unsigned char buf[1024];
        int got = 0;

        r = libusb_bulk_transfer(
            h,
            EP_IN,
            buf,
            sizeof(buf),
            &got,
            180
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

    return 0;
}

static int parse_csq(const char *response, int *rssi)
{
    const char *p = strstr(response, "+CSQ:");

    if (!p)
        return 0;

    p += 5;

    while (*p == ' ' || *p == '\t')
        p++;

    char *end = NULL;
    long value = strtol(p, &end, 10);

    if (end == p)
        return 0;

    *rssi = (int)value;
    return 1;
}

static int rssi_to_dbm(int rssi)
{
    if (rssi < 0 || rssi > 31)
        return 0;

    return -113 + 2 * rssi;
}

static int dbm_to_bars(int dbm)
{
    /*
     * 4 bare, orientativ pentru afisare de tip telefon.
     * Nu pretindem algoritmul proprietar al unui anumit telefon.
     */
    if (dbm <= -105)
        return 1;
    if (dbm <= -95)
        return 2;
    if (dbm <= -85)
        return 3;
    return 4;
}

static void print_signal(libusb_device_handle *h)
{
    char response[4096];
    int rssi = 99;

    int r = send_command(
        h,
        "AT+CSQ",
        response,
        sizeof(response)
    );

    if (r == 0 && parse_csq(response, &rssi) &&
        rssi >= 0 && rssi <= 31) {

        int dbm = rssi_to_dbm(rssi);
        int bars = dbm_to_bars(dbm);

        printf("CSQ=%d\n", rssi);
        printf("DBM=%d\n", dbm);
        printf("BARS=%d\n", bars);
        printf("SIGNAL_KNOWN=1\n");
    } else {
        printf("CSQ=99\n");
        printf("DBM=\n");
        printf("BARS=0\n");
        printf("SIGNAL_KNOWN=0\n");
    }
}

static void print_operator(libusb_device_handle *h)
{
    char response[8192];
    char oper[256];

    send_command(
        h,
        "AT+COPS=3,0",
        response,
        sizeof(response)
    );

    int r = send_command(
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
}


static void print_active_apn(libusb_device_handle *h)
{
    char response[8192];

    int r = send_command(
        h,
        "AT+CGCONTRDP",
        response,
        sizeof(response)
    );

    if (r != 0) {
        printf("ACTIVE_APN=\n");
        return;
    }

    const char *p = strstr(response, "+CGCONTRDP:");

    if (!p) {
        printf("ACTIVE_APN=\n");
        return;
    }

    int cid = -1;
    int bearer = -1;
    char apn[256] = {0};

    /*
     * Exemplu EM7455:
     * +CGCONTRDP: 2,5,Broadband,10.31.117.175,...
     */
    int parsed = sscanf(
        p,
        "+CGCONTRDP: %d,%d,%255[^,]",
        &cid,
        &bearer,
        apn
    );

    if (parsed >= 3) {
        size_t n = strlen(apn);

        while (n > 0 &&
               (apn[n - 1] == ' ' ||
                apn[n - 1] == '\r' ||
                apn[n - 1] == '\n' ||
                apn[n - 1] == '"')) {
            apn[--n] = 0;
        }

        char *start = apn;

        while (*start == ' ' || *start == '"')
            start++;

        printf("ACTIVE_APN=%s\n", start);
        printf("ACTIVE_CID=%d\n", cid);
        printf("ACTIVE_BEARER=%d\n", bearer);
    } else {
        printf("ACTIVE_APN=\n");
    }
}

int main(int argc, char **argv)
{
    int signal_only =
        argc >= 2 && strcmp(argv[1], "signal") == 0;

    int active_only =
        argc >= 2 && strcmp(argv[1], "active") == 0;

    libusb_context *ctx = NULL;
    libusb_device_handle *h = NULL;

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

    if (active_only) {
        print_active_apn(h);
    } else {
        if (!signal_only)
            print_operator(h);

        print_signal(h);
    }

    libusb_release_interface(h, IFACE);
    libusb_close(h);
    libusb_exit(ctx);

    return 0;
}
