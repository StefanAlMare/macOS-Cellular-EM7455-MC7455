#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <libusb.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>


#define VID      0x1199
#define PID      0x9071
#define IF_CTRL  12
#define IF_DATA  13
#define EP_INT   0x87

static const unsigned char basic_uuid[16] = {
    0xa2,0x89,0xcc,0x33,0xbc,0xbb,0x8b,0x4f,
    0xb6,0xb0,0x13,0x3e,0xc2,0xaa,0xe6,0xdf
};

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t le64(const unsigned char *p)
{
    return (uint64_t)le32(p) |
           ((uint64_t)le32(p + 4) << 32);
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static const char *msg_name(uint32_t t)
{
    switch (t) {
        case 0x80000001: return "OPEN_DONE";
        case 0x80000002: return "CLOSE_DONE";
        case 0x80000003: return "COMMAND_DONE";
        case 0x80000004: return "FUNCTION_ERROR";
        case 0x80000007: return "INDICATE_STATUS";
        default: return "OTHER";
    }
}

static int receive_one(libusb_device_handle *h,
                       unsigned char *resp, int max)
{
    unsigned char intr[64];
    int got = 0;

    int r = libusb_interrupt_transfer(
        h, EP_INT, intr, sizeof(intr), &got, 3000
    );

    if (r != 0)
        return r;

    memset(resp, 0, max);

    r = libusb_control_transfer(
        h, 0xA1, 0x01, 0, IF_CTRL,
        resp, max, 5000
    );

    if (r >= 12) {
        printf("RX %-15s TID=%u LEN=%u\n",
               msg_name(le32(resp)),
               le32(resp + 8),
               le32(resp + 4));
    }

    return r;
}

static int wait_for(libusb_device_handle *h,
                    uint32_t type, uint32_t tid,
                    unsigned char *resp, int max)
{
    int timeouts = 0;

    for (int i = 0; i < 60; i++) {
        int r = receive_one(h, resp, max);

        if (r == LIBUSB_ERROR_TIMEOUT) {
            if (++timeouts >= 20)
                return r;
            continue;
        }

        if (r < 0)
            return r;

        if (r < 12)
            continue;

        uint32_t rt = le32(resp);
        uint32_t id = le32(resp + 8);

        if (id == tid && rt == 0x80000004) {
            printf("FUNCTION_ERROR TID=%u cod=%u\n",
                   tid, r >= 16 ? le32(resp + 12) : 0);
            return -100;
        }

        if (rt == type && id == tid)
            return r;
    }

    return -101;
}

static int send_query(libusb_device_handle *h,
                      uint32_t cid, uint32_t tid,
                      unsigned char *resp, int max)
{
    unsigned char q[48];
    memset(q, 0, sizeof(q));

    put32(q + 0,  3);      /* MBIM_COMMAND_MSG */
    put32(q + 4,  48);
    put32(q + 8,  tid);

    put32(q + 12, 1);      /* TotalFragments */
    put32(q + 16, 0);      /* CurrentFragment */

    memcpy(q + 20, basic_uuid, 16);

    put32(q + 36, cid);
    put32(q + 40, 0);      /* QUERY */
    put32(q + 44, 0);      /* InformationBufferLength */

    printf("\n===== QUERY CID %u / TID %u =====\n", cid, tid);

    int r = libusb_control_transfer(
        h, 0x21, 0x00, 0, IF_CTRL,
        q, sizeof(q), 3000
    );

    printf("SEND: %d\n", r);

    if (r < 0)
        return r;

    r = wait_for(h, 0x80000003, tid, resp, max);

    if (r < 48)
        return r;

    printf("CID=%u STATUS=%u INFOLEN=%u\n",
           le32(resp + 36),
           le32(resp + 40),
           le32(resp + 44));

    return r;
}

static const char *ready_name(uint32_t s)
{
    switch (s) {
        case 0: return "NOT_INITIALIZED";
        case 1: return "INITIALIZED";
        case 2: return "SIM_NOT_INSERTED";
        case 3: return "BAD_SIM";
        case 4: return "FAILURE";
        case 5: return "NOT_ACTIVATED";
        case 6: return "DEVICE_LOCKED";
        default: return "UNKNOWN";
    }
}

static const char *reg_name(uint32_t s)
{
    switch (s) {
        case 0: return "UNKNOWN";
        case 1: return "DEREGISTERED";
        case 2: return "SEARCHING";
        case 3: return "HOME";
        case 4: return "ROAMING";
        case 5: return "PARTNER";
        case 6: return "DENIED";
        default: return "?";
    }
}

static const char *packet_name(uint32_t s)
{
    switch (s) {
        case 0: return "UNKNOWN";
        case 1: return "ATTACHING";
        case 2: return "ATTACHED";
        case 3: return "DETACHING";
        case 4: return "DETACHED";
        default: return "?";
    }
}


#define UTUN_CONTROL_NAME "com.apple.net.utun_control"
#define UTUN_OPT_IFNAME 2


static SCDynamicStoreRef publish_lte_state(
    const char *ifname,
    const char *ip,
    const char *gateway,
    const char *dns1,
    const char *dns2)
{
    SCDynamicStoreRef store = NULL;

    CFStringRef cf_if = NULL;
    CFStringRef cf_ip = NULL;
    CFStringRef cf_gw = NULL;
    CFStringRef cf_dns1 = NULL;
    CFStringRef cf_dns2 = NULL;

    CFStringRef service = NULL;
    CFStringRef ipv4_key = NULL;
    CFStringRef dns_key = NULL;

    CFMutableDictionaryRef ipv4 = NULL;
    CFMutableDictionaryRef dns = NULL;

    CFArrayRef addresses = NULL;
    CFArrayRef masks = NULL;
    CFArrayRef servers = NULL;
    CFArrayRef match_domains = NULL;

    CFNumberRef one = NULL;
    CFNumberRef order = NULL;

    int one_i = 1;
    int order_i = 1;

    store = SCDynamicStoreCreate(
        NULL,
        CFSTR("EM7455 LTE"),
        NULL,
        NULL
    );

    if (!store) {
        fprintf(stderr, "SCDynamicStoreCreate a eȃ�uat\n");
        return NULL;
    }

    cf_if = CFStringCreateWithCString(
        NULL, ifname, kCFStringEncodingUTF8);

    cf_ip = CFStringCreateWithCString(
        NULL, ip, kCFStringEncodingUTF8);

    cf_gw = CFStringCreateWithCString(
        NULL, gateway, kCFStringEncodingUTF8);

    cf_dns1 = CFStringCreateWithCString(
        NULL, dns1, kCFStringEncodingUTF8);

    cf_dns2 = CFStringCreateWithCString(
        NULL, dns2, kCFStringEncodingUTF8);

    service = CFStringCreateWithFormat(
        NULL,
        NULL,
        CFSTR("State:/Network/Service/EM7455-LTE-%@"),
        cf_if
    );

    ipv4_key = CFStringCreateWithFormat(
        NULL, NULL, CFSTR("%@/IPv4"), service);

    dns_key = CFStringCreateWithFormat(
        NULL, NULL, CFSTR("%@/DNS"), service);

    /* ---------------- IPv4 state ---------------- */

    ipv4 = CFDictionaryCreateMutable(
        NULL,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );

    const void *addr_values[] = { cf_ip };

    addresses = CFArrayCreate(
        NULL,
        addr_values,
        1,
        &kCFTypeArrayCallBacks
    );

    CFStringRef mask = CFSTR("255.255.255.252");
    const void *mask_values[] = { mask };

    masks = CFArrayCreate(
        NULL,
        mask_values,
        1,
        &kCFTypeArrayCallBacks
    );

    one = CFNumberCreate(
        NULL,
        kCFNumberIntType,
        &one_i
    );

    CFDictionarySetValue(
        ipv4, CFSTR("Addresses"), addresses);

    CFDictionarySetValue(
        ipv4, CFSTR("SubnetMasks"), masks);

    CFDictionarySetValue(
        ipv4, CFSTR("Router"), cf_gw);

    CFDictionarySetValue(
        ipv4, CFSTR("InterfaceName"), cf_if);

    CFDictionarySetValue(
        ipv4, CFSTR("OverridePrimary"), one);

    /* ---------------- DNS state ---------------- */

    dns = CFDictionaryCreateMutable(
        NULL,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );

    const void *server_values[2];
    CFIndex server_count = 1;

    server_values[0] = cf_dns1;

    if (dns2 && dns2[0] && strcmp(dns1, dns2) != 0) {
        server_values[1] = cf_dns2;
        server_count = 2;
    }

    servers = CFArrayCreate(
        NULL,
        server_values,
        server_count,
        &kCFTypeArrayCallBacks
    );

    /*
     * Domeniul gol => resolver implicit.
     */
    const void *domain_values[] = { CFSTR("") };

    match_domains = CFArrayCreate(
        NULL,
        domain_values,
        1,
        &kCFTypeArrayCallBacks
    );

    order = CFNumberCreate(
        NULL,
        kCFNumberIntType,
        &order_i
    );

    CFDictionarySetValue(
        dns,
        CFSTR("ServerAddresses"),
        servers
    );

    CFDictionarySetValue(
        dns,
        CFSTR("SupplementalMatchDomains"),
        match_domains
    );

    CFDictionarySetValue(
        dns,
        CFSTR("SupplementalMatchDomainsNoSearch"),
        one
    );

    CFDictionarySetValue(
        dns,
        CFSTR("SearchOrder"),
        order
    );

    /*
     * Valorile sunt TEMPORARE ȃ�i aparȃ�in acestei
     * sesiuni SCDynamicStore.
     */
    Boolean ok_ipv4 =
        SCDynamicStoreAddTemporaryValue(
            store,
            ipv4_key,
            ipv4
        );

    Boolean ok_dns =
        SCDynamicStoreAddTemporaryValue(
            store,
            dns_key,
            dns
        );

    if (!ok_ipv4 || !ok_dns) {
        fprintf(
            stderr,
            "Publicarea SystemConfiguration a eȃ�uat: %s\n",
            SCErrorString(SCError())
        );

        CFRelease(store);
        store = NULL;
    } else {
        printf("SystemConfiguration LTE publicat temporar.\n");
        printf("DNS LTE: %s, %s\n", dns1, dns2);
    }

    if (order) CFRelease(order);
    if (one) CFRelease(one);

    if (match_domains) CFRelease(match_domains);
    if (servers) CFRelease(servers);
    if (masks) CFRelease(masks);
    if (addresses) CFRelease(addresses);

    if (dns) CFRelease(dns);
    if (ipv4) CFRelease(ipv4);

    if (dns_key) CFRelease(dns_key);
    if (ipv4_key) CFRelease(ipv4_key);
    if (service) CFRelease(service);

    if (cf_dns2) CFRelease(cf_dns2);
    if (cf_dns1) CFRelease(cf_dns1);
    if (cf_gw) CFRelease(cf_gw);
    if (cf_ip) CFRelease(cf_ip);
    if (cf_if) CFRelease(cf_if);

    return store;
}


static volatile sig_atomic_t bridge_stop = 0;

static void bridge_signal_handler(int sig)
{
    (void)sig;
    bridge_stop = 1;
}

static int install_bridge_signal_handlers(void)
{
    /*
     * Procesul poate fi pornit de un shell asincron/background.
     * Resetam explicit actiunile si deblocam semnalele folosite
     * pentru oprire, ca sa nu depindem de starea mostenita.
     */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGUSR1);

    if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0) {
        perror("pthread_sigmask");
        return -1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = bridge_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction SIGINT");
        return -1;
    }

    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction SIGTERM");
        return -1;
    }

    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        perror("sigaction SIGUSR1");
        return -1;
    }

    return 0;
}

static uint16_t get16le(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void put16le(unsigned char *p, uint16_t v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
}

static int create_utun(char *ifname, size_t ifname_size)
{
    int fd;
    struct ctl_info ctl;
    struct sockaddr_ctl addr;
    socklen_t len;

    fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0) {
        perror("socket utun");
        return -1;
    }

    memset(&ctl, 0, sizeof(ctl));
    strncpy(ctl.ctl_name,
            UTUN_CONTROL_NAME,
            sizeof(ctl.ctl_name) - 1);

    if (ioctl(fd, CTLIOCGINFO, &ctl) < 0) {
        perror("CTLIOCGINFO");
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sc_len = sizeof(addr);
    addr.sc_family = AF_SYSTEM;
    addr.ss_sysaddr = AF_SYS_CONTROL;
    addr.sc_id = ctl.ctl_id;
    addr.sc_unit = 0;

    if (connect(fd,
                (struct sockaddr *)&addr,
                sizeof(addr)) < 0) {
        perror("connect utun");
        close(fd);
        return -1;
    }

    memset(ifname, 0, ifname_size);
    len = (socklen_t)ifname_size;

    if (getsockopt(fd,
                   SYSPROTO_CONTROL,
                   UTUN_OPT_IFNAME,
                   ifname,
                   &len) < 0) {
        perror("UTUN_OPT_IFNAME");
        close(fd);
        return -1;
    }

    return fd;
}

struct bridge_ctx {
    libusb_device_handle *h;
    int utun_fd;

    uint16_t sequence;

    unsigned long long tx_packets;
    unsigned long long rx_packets;
};

/*
 * macOS utun -> MBIM/NCM -> USB 0x04
 */
static void *utun_to_mbim(void *arg)
{
    struct bridge_ctx *ctx = arg;

    while (!bridge_stop) {
        struct pollfd pfd;
        unsigned char frame[4096];

        pfd.fd = ctx->utun_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, 500);

        if (pr < 0) {
            if (errno == EINTR)
                continue;

            perror("poll utun");
            bridge_stop = 1;
            break;
        }

        if (pr == 0)
            continue;

        ssize_t n = read(ctx->utun_fd,
                         frame,
                         sizeof(frame));

        if (n <= 4)
            continue;

        uint32_t afnet;
        memcpy(&afnet, frame, 4);

        uint32_t af = ntohl(afnet);

        /*
         * Primul nostru bearer este IPv4 only.
         */
        if (af != AF_INET)
            continue;

        unsigned char *ip = frame + 4;
        int iplen = (int)n - 4;

        if (iplen <= 0 || iplen > 2000)
            continue;

        if ((ip[0] >> 4) != 4)
            continue;

        /*
         * Acelaȃ�i NTB16 care a funcȃ�ionat deja în testul DNS.
         */
        unsigned char ntb[4096];
        memset(ntb, 0, sizeof(ntb));

        const int ndp_offset = 12;
        const int payload_offset = 88;

        int blocklen = payload_offset + iplen;

        /*
         * NTH16: "NCMH"
         */
        put32(ntb + 0, 0x484d434e);
        put16le(ntb + 4, 12);
        put16le(ntb + 6, ctx->sequence++);
        put16le(ntb + 8, blocklen);
        put16le(ntb + 10, ndp_offset);

        /*
         * NDP16 IPS session 0
         */
        put32(ntb + ndp_offset + 0, 0x00535049);
        put16le(ntb + ndp_offset + 4, 16);
        put16le(ntb + ndp_offset + 6, 0);

        put16le(ntb + ndp_offset + 8, payload_offset);
        put16le(ntb + ndp_offset + 10, iplen);

        put16le(ntb + ndp_offset + 12, 0);
        put16le(ntb + ndp_offset + 14, 0);

        memcpy(ntb + payload_offset, ip, iplen);

        int sent = 0;

        int r = libusb_bulk_transfer(
            ctx->h,
            0x04,
            ntb,
            blocklen,
            &sent,
            5000
        );

        if (r != 0) {
            if (!bridge_stop)
                fprintf(stderr,
                        "USB TX: %s (%d)\n",
                        libusb_error_name(r), r);

            bridge_stop = 1;
            break;
        }

        ctx->tx_packets++;

        if (ctx->tx_packets <= 5 ||
            !(ctx->tx_packets % 50)) {
            printf("TX utun -> LTE: %llu packets\n",
                   ctx->tx_packets);
        }
    }

    return NULL;
}

/*
 * USB 0x86 -> MBIM/NCM -> macOS utun
 */
static void *mbim_to_utun(void *arg)
{
    struct bridge_ctx *ctx = arg;

    while (!bridge_stop) {
        unsigned char rx[16384];
        int got = 0;

        int r = libusb_bulk_transfer(
            ctx->h,
            0x86,
            rx,
            sizeof(rx),
            &got,
            1000
        );

        if (r == LIBUSB_ERROR_TIMEOUT)
            continue;

        if (r != 0) {
            if (!bridge_stop)
                fprintf(stderr,
                        "USB RX: %s (%d)\n",
                        libusb_error_name(r), r);

            bridge_stop = 1;
            break;
        }

        if (got < 28)
            continue;

        if (le32(rx) != 0x484d434e)
            continue;

        int ndpo = get16le(rx + 10);
        int guard = 0;

        while (ndpo &&
               ndpo + 16 <= got &&
               guard++ < 16) {

            uint32_t sig = le32(rx + ndpo);

            int ndplen = get16le(rx + ndpo + 4);
            int next   = get16le(rx + ndpo + 6);

            /*
             * lower 24 bits = "IPS"
             * high byte = session id.
             * Noi acceptă�m numai Session 0.
             */
            if ((sig & 0x00ffffff) == 0x00535049 &&
                ((sig >> 24) & 0xff) == 0) {

                for (int pos = ndpo + 8;
                     pos + 4 <= ndpo + ndplen;
                     pos += 4) {

                    int off = get16le(rx + pos);
                    int len = get16le(rx + pos + 2);

                    if (!off || !len)
                        break;

                    if (off < 0 ||
                        len <= 0 ||
                        off + len > got ||
                        len > 4096)
                        continue;

                    unsigned char *ip = rx + off;

                    if ((ip[0] >> 4) != 4)
                        continue;

                    unsigned char packet[4100];

                    /*
                     * utun ABI:
                     * 4 bytes protocol family in network byte order.
                     */
                    uint32_t af = htonl(AF_INET);

                    memcpy(packet, &af, 4);
                    memcpy(packet + 4, ip, len);

                    ssize_t wr = write(
                        ctx->utun_fd,
                        packet,
                        len + 4
                    );

                    if (wr < 0) {
                        if (!bridge_stop)
                            perror("write utun");

                        bridge_stop = 1;
                        break;
                    }

                    ctx->rx_packets++;

                    if (ctx->rx_packets <= 5 ||
                        !(ctx->rx_packets % 50)) {
                        printf("RX LTE -> utun: %llu packets\n",
                               ctx->rx_packets);
                    }
                }
            }

            ndpo = next;
        }
    }

    return NULL;
}



static void hex_dump(const unsigned char *p, size_t n)
{
    for (size_t i = 0; i < n; i += 16) {
        printf("%04zx  ", i);

        for (size_t j = 0; j < 16; j++) {
            if (i + j < n)
                printf("%02x ", p[i + j]);
            else
                printf("   ");
        }

        printf(" |");

        for (size_t j = 0; j < 16 && i + j < n; j++) {
            unsigned char c = p[i + j];
            putchar((c >= 32 && c <= 126) ? c : '.');
        }

        printf("|\n");
    }
}

static void scan_utf16le_ascii(const unsigned char *p, size_t n)
{
    printf("\nUTF16LE strings found:\n");

    int found = 0;

    for (size_t i = 0; i + 5 < n; ) {
        if (p[i] >= 32 && p[i] <= 126 && p[i + 1] == 0) {
            size_t start = i;
            char buf[512];
            size_t used = 0;

            while (i + 1 < n &&
                   p[i] >= 32 && p[i] <= 126 &&
                   p[i + 1] == 0 &&
                   used + 1 < sizeof(buf)) {
                buf[used++] = (char)p[i];
                i += 2;
            }

            buf[used] = 0;

            if (used >= 3) {
                printf("  @0x%04zx: \"%s\"\n", start, buf);
                found = 1;
            }
        } else {
            i++;
        }
    }

    if (!found)
        printf("  (none)\n");
}

static void dump_command_info(const unsigned char *resp, int r)
{
    if (r < 48) {
        printf("Response too short: %d\n", r);
        return;
    }

    uint32_t cid = le32(resp + 36);
    uint32_t status = le32(resp + 40);
    uint32_t info_len = le32(resp + 44);

    printf("CID=%u STATUS=%u INFOLEN=%u TOTAL_RX=%d\n",
           cid, status, info_len, r);

    if (status != 0 || info_len == 0)
        return;

    if ((uint64_t)48 + info_len > (uint64_t)r) {
        printf("TRUNCATED: need %u bytes after header, got %d total\n",
               info_len, r);
        return;
    }

    const unsigned char *info = resp + 48;

    printf("\nInformationBuffer HEX:\n");
    hex_dump(info, info_len);
    scan_utf16le_ascii(info, info_len);
}

int main(void)
{
    libusb_context *ctx = NULL;
    libusb_device_handle *h = NULL;

    /*
     * EXACT ca motorul stabil:
     * - response buffer 4096
     * - IF12/IF13
     * - alt setting 1 -> 0 -> GET_NTB -> 1
     * - MBIM_OPEN MaxControlTransfer 4096
     */
    unsigned char resp[4096], ntb[64];
    int r;

    printf("CELLULAR MBIM APN PROBE v1.1 - READ ONLY\n");
    printf("Same MBIM OPEN path as the validated engine.\n");
    printf("No SET, no CONNECT, no profile modification.\n\n");

    if (libusb_init(&ctx) != 0)
        return 1;

    h = libusb_open_device_with_vid_pid(ctx, VID, PID);

    if (!h) {
        printf("EM7455 not found\n");
        libusb_exit(ctx);
        return 2;
    }

    r = libusb_claim_interface(h, IF_CTRL);
    printf("CLAIM IF12: %s\n", libusb_error_name(r));
    if (r != 0) goto done;

    r = libusb_claim_interface(h, IF_DATA);
    printf("CLAIM IF13: %s\n", libusb_error_name(r));
    if (r != 0) goto rel12;

    libusb_set_interface_alt_setting(h, IF_DATA, 1);
    usleep(20000);
    libusb_set_interface_alt_setting(h, IF_DATA, 0);

    r = libusb_control_transfer(
        h, 0xA1, 0x80, 0, IF_CTRL,
        ntb, sizeof(ntb), 3000
    );

    printf("GET_NTB_PARAMETERS: %d\n", r);
    if (r < 0) goto cleanup;

    usleep(20000);
    libusb_set_interface_alt_setting(h, IF_DATA, 1);
    usleep(20000);

    /* OPEN - identical to stable engine */
    unsigned char openmsg[16] = {0};
    put32(openmsg + 0, 1);
    put32(openmsg + 4, 16);
    put32(openmsg + 8, 500);
    put32(openmsg + 12, 4096);

    r = libusb_control_transfer(
        h, 0x21, 0x00, 0, IF_CTRL,
        openmsg, sizeof(openmsg), 3000
    );

    printf("MBIM_OPEN SEND: %d\n", r);
    if (r < 0) goto cleanup;

    r = wait_for(h, 0x80000001, 500, resp, sizeof(resp));

    printf("MBIM_OPEN wait result: %d\n", r);

    if (r < 16) {
        printf("MBIM_OPEN FAILED before OPEN_DONE\n");
        goto cleanup;
    }

    printf("OPEN_DONE status: %u\n", le32(resp + 12));

    if (le32(resp + 12) != 0) {
        printf("MBIM_OPEN FAILED status=%u\n", le32(resp + 12));
        goto cleanup;
    }

    printf("\n*** MBIM OPEN OK ***\n");

    /*
     * Existing stable engine already queries CID 9 successfully.
     * Add the two APN/provider-related read-only queries around it.
     */
    r = send_query(h, 6, 501, resp, sizeof(resp));
    printf("\n===== HOME_PROVIDER (CID 6) =====\n");
    dump_command_info(resp, r);

    r = send_query(h, 9, 502, resp, sizeof(resp));
    printf("\n===== REGISTER_STATE (CID 9) =====\n");
    dump_command_info(resp, r);

    r = send_query(h, 13, 503, resp, sizeof(resp));
    printf("\n===== PROVISIONED_CONTEXTS (CID 13) =====\n");
    dump_command_info(resp, r);

    /* CLOSE - same format as stable engine */
    unsigned char closemsg[12] = {0};
    put32(closemsg + 0, 2);
    put32(closemsg + 4, 12);
    put32(closemsg + 8, 599);

    printf("\n===== MBIM_CLOSE =====\n");

    r = libusb_control_transfer(
        h, 0x21, 0x00, 0, IF_CTRL,
        closemsg, sizeof(closemsg), 3000
    );

    printf("CLOSE SEND: %d\n", r);

    if (r >= 0) {
        r = wait_for(h, 0x80000002, 599, resp, sizeof(resp));
        printf("CLOSE wait result: %d\n", r);

        if (r >= 16)
            printf("CLOSE_DONE status: %u\n", le32(resp + 12));
    }

cleanup:
    libusb_set_interface_alt_setting(h, IF_DATA, 0);
    libusb_release_interface(h, IF_DATA);

rel12:
    libusb_release_interface(h, IF_CTRL);

done:
    libusb_close(h);
    libusb_exit(ctx);
    return 0;
}
