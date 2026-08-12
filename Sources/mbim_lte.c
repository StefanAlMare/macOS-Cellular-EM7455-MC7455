#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
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



static const unsigned char internet_context_uuid[16] = {
    0x7e,0x5e,0x2a,0x7e,
    0x4e,0x6f,
    0x72,0x72,
    0x73,0x6b,
    0x65,0x6e,0x7e,0x5e,0x2a,0x7e
};

static int mbim_activate_session0_apn(
    libusb_device_handle *h,
    const char *apn,
    uint32_t tid,
    unsigned char *resp,
    size_t resp_size,
    uint32_t *activation_state_out,
    int max_wait_seconds)
{
    size_t apn_chars = apn ? strlen(apn) : 0;

    if (apn_chars > 100) {
        fprintf(stderr, "APN prea lung\n");
        return 0;
    }

    size_t access_size = apn_chars * 2;
    size_t access_padded = (access_size + 3u) & ~3u;
    size_t info_len = 60 + access_padded;
    size_t msg_len = 48 + info_len;

    if (msg_len > 512) {
        fprintf(stderr, "CONNECT message prea mare\n");
        return 0;
    }

    unsigned char con[512];
    memset(con, 0, sizeof(con));

    put32(con + 0,  3);                  /* MBIM_COMMAND */
    put32(con + 4,  (uint32_t)msg_len);
    put32(con + 8,  tid);

    put32(con + 12, 1);
    put32(con + 16, 0);

    memcpy(con + 20, basic_uuid, 16);

    put32(con + 36, 12);                 /* MBIM_CID_CONNECT */
    put32(con + 40, 1);                  /* SET */
    put32(con + 44, (uint32_t)info_len);

    unsigned char *ci = con + 48;

    put32(ci + 0,  0);                   /* SessionId = 0 */
    put32(ci + 4,  1);                   /* ACTIVATE */

    /*
     * AccessString NULL/empty = network default APN.
     * Daca apn are continut, il trimitem UTF-16LE.
     */
    if (access_size > 0) {
        put32(ci + 8,  60);
        put32(ci + 12, (uint32_t)access_size);

        for (size_t i = 0; i < apn_chars; i++) {
            ci[60 + i * 2]     = (unsigned char)apn[i];
            ci[60 + i * 2 + 1] = 0;
        }
    } else {
        put32(ci + 8,  0);
        put32(ci + 12, 0);
    }

    put32(ci + 16, 0);                   /* UsernameOffset */
    put32(ci + 20, 0);                   /* UsernameSize */
    put32(ci + 24, 0);                   /* PasswordOffset */
    put32(ci + 28, 0);                   /* PasswordSize */
    put32(ci + 32, 0);                   /* Compression NONE */
    put32(ci + 36, 0);                   /* Auth NONE */
    put32(ci + 40, 1);                   /* IPv4 - v2.5 */
    memcpy(ci + 44, internet_context_uuid, 16);

    printf("\n============================================\n");
    printf(" MBIM CONNECT ACTIVATE\n");
    printf("============================================\n");
    printf("Session : 0\n");
    printf("APN     : %s\n",
           access_size ? apn : "(network default / NULL)");
    printf("IP type : IPv4\n");

    int r = libusb_control_transfer(
        h,
        0x21, 0x00, 0, IF_CTRL,
        con, (uint16_t)msg_len, 5000
    );

    printf("CONNECT ACTIVATE SEND: %d\n", r);

    if (r < 0)
        return 0;

    r = wait_for(
        h,
        0x80000003,
        tid,
        resp,
        (int)resp_size
    );

    if (r < 48)
        return 0;

    uint32_t cid = le32(resp + 36);
    uint32_t status = le32(resp + 40);
    uint32_t infolen = le32(resp + 44);

    printf("\n===== CONNECT ACTIVATE RESPONSE =====\n");
    printf("CID            : %u\n", cid);
    printf("Command status : %u\n", status);
    printf("Info length    : %u\n", infolen);

    if (status != 0 || infolen < 36 || r < 84)
        return 0;

    unsigned char *info = resp + 48;
    uint32_t activation_state = le32(info + 4);

    if (activation_state_out)
        *activation_state_out = activation_state;

    printf("Session ID      : %u\n", le32(info + 0));
    printf("Activation state: %u\n", activation_state);
    printf("Voice state     : %u\n", le32(info + 8));
    printf("IP type         : %u\n", le32(info + 12));
    printf("Network error   : %u\n", le32(info + 32));

    if (activation_state == 1) {
        printf("\n*** BEARER ACTIVATED ***\n");
        return 1;
    }

    if (activation_state != 2)
        return 0;

    printf("\nBearer-ul este ACTIVATING; asteptam...\n");

    if (max_wait_seconds < 1)
        max_wait_seconds = 1;

    for (int attempt = 0; attempt < max_wait_seconds; attempt++) {
        sleep(1);

        uint32_t qtid = tid + 10 + (uint32_t)attempt;

        unsigned char cq[84];
        memset(cq, 0, sizeof(cq));

        put32(cq + 0,  3);
        put32(cq + 4,  84);
        put32(cq + 8,  qtid);
        put32(cq + 12, 1);
        put32(cq + 16, 0);

        memcpy(cq + 20, basic_uuid, 16);

        put32(cq + 36, 12);              /* CONNECT */
        put32(cq + 40, 0);               /* QUERY */
        put32(cq + 44, 36);              /* fixed connect query info */

        r = libusb_control_transfer(
            h, 0x21, 0x00, 0, IF_CTRL,
            cq, sizeof(cq), 5000
        );

        if (r < 0)
            break;

        r = wait_for(
            h,
            0x80000003,
            qtid,
            resp,
            (int)resp_size
        );

        if (r >= 84 && le32(resp + 40) == 0) {
            info = resp + 48;
            activation_state = le32(info + 4);

            if (activation_state_out)
                *activation_state_out = activation_state;

            printf(
                "CONNECT QUERY #%d: state=%u, nwerr=%u\n",
                attempt + 1,
                activation_state,
                le32(info + 32)
            );

            if (activation_state == 1) {
                printf("\n*** BEARER ACTIVATED ***\n");
                return 1;
            }

            if (activation_state == 3)
                break;
        }
    }

    return 0;
}

static void mbim_deactivate_session0_quiet(
    libusb_device_handle *h,
    uint32_t tid,
    unsigned char *resp,
    size_t resp_size)
{
    unsigned char deact[108];
    memset(deact, 0, sizeof(deact));

    put32(deact + 0,  3);
    put32(deact + 4,  108);
    put32(deact + 8,  tid);

    put32(deact + 12, 1);
    put32(deact + 16, 0);

    memcpy(deact + 20, basic_uuid, 16);

    put32(deact + 36, 12);                /* CONNECT */
    put32(deact + 40, 1);                 /* SET */
    put32(deact + 44, 60);

    unsigned char *di = deact + 48;

    put32(di + 0,  0);                    /* Session 0 */
    put32(di + 4,  0);                    /* DEACTIVATE */
    put32(di + 32, 0);
    put32(di + 36, 0);
    put32(di + 40, 1);                    /* IPv4 */
    memcpy(di + 44, internet_context_uuid, 16);

    int r = libusb_control_transfer(
        h,
        0x21, 0x00, 0, IF_CTRL,
        deact, sizeof(deact), 5000
    );

    if (r >= 0) {
        (void)wait_for(
            h,
            0x80000003,
            tid,
            resp,
            (int)resp_size
        );
    }
}


static struct timespec cellular_t0;

static void cellular_timer_start(void)
{
    clock_gettime(CLOCK_MONOTONIC, &cellular_t0);
}

static long cellular_elapsed_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long sec = (long)(now.tv_sec - cellular_t0.tv_sec);
    long nsec = now.tv_nsec - cellular_t0.tv_nsec;

    if (nsec < 0) {
        sec--;
        nsec += 1000000000L;
    }

    return sec * 1000L + nsec / 1000000L;
}

static void cellular_milestone(const char *name)
{
    printf("[TIMING +%ld ms] %s\n", cellular_elapsed_ms(), name);
    fflush(stdout);
}

int main(void)
{
    cellular_timer_start();
    cellular_milestone("engine start");

    /*
     * Instalam handlers imediat, inainte de orice operatie MBIM/USB.
     * Astfel Disconnect functioneaza si daca este cerut in timpul
     * secventei de conectare, nu numai dupa pornirea bridge-ului.
     */
    if (install_bridge_signal_handlers() != 0)
        return 1;

    libusb_context *ctx = NULL;
    libusb_device_handle *h = NULL;
    unsigned char resp[4096], ntb[64];
    int r;

    if (libusb_init(&ctx) != 0)
        return 1;

    h = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (!h) {
        printf("EM7455 nu a fost gă�sit\n");
        return 1;
    }

    r = libusb_claim_interface(h, IF_CTRL);
    printf("CLAIM IF12: %s\n", libusb_error_name(r));
    if (r != 0) goto done;

    r = libusb_claim_interface(h, IF_DATA);
    printf("CLAIM IF13: %s\n", libusb_error_name(r));
    if (r != 0) goto rel12;

    /* Sierra EM7455 / Linux-like initialization */
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

    /* OPEN TID 500 */
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
    if (r < 16 || le32(resp + 12) != 0) {
        printf("MBIM_OPEN NU A REUȃ�IT\n");
        goto cleanup;
    }

    printf("\n*** MBIM OPEN OK ***\n");
    cellular_milestone("MBIM OPEN complete");

    cellular_milestone("preflight begin");

    /* CID 2: Subscriber Ready */
    r = send_query(h, 2, 501, resp, sizeof(resp));
    if (r >= 52 && le32(resp + 40) == 0) {
        unsigned char *info = resp + 48;
        printf("Subscriber Ready: %u (%s)\n",
               le32(info), ready_name(le32(info)));
    }

    /* CID 9: Register State */
    r = send_query(h, 9, 502, resp, sizeof(resp));
    if (r >= 56 && le32(resp + 40) == 0) {
        unsigned char *info = resp + 48;
        printf("Network error : %u\n", le32(info + 0));
        printf("Register state: %u (%s)\n",
               le32(info + 4), reg_name(le32(info + 4)));
        printf("Register mode : %u\n", le32(info + 8));
        printf("Data classes  : 0x%08x\n", le32(info + 12));
    }

    /* CID 10: Packet Service */
    r = send_query(h, 10, 503, resp, sizeof(resp));
    if (r >= 76 && le32(resp + 40) == 0) {
        unsigned char *info = resp + 48;
        printf("Network error : %u\n", le32(info + 0));
        printf("Packet service: %u (%s)\n",
               le32(info + 4), packet_name(le32(info + 4)));
        printf("Data class    : 0x%08x\n", le32(info + 8));
        printf("Uplink        : %llu bit/s\n",
               (unsigned long long)le64(info + 12));
        printf("Downlink      : %llu bit/s\n",
               (unsigned long long)le64(info + 20));
    }

    /* CID 11: Signal State */
    r = send_query(h, 11, 504, resp, sizeof(resp));
    if (r >= 68 && le32(resp + 40) == 0) {
        unsigned char *info = resp + 48;
        printf("RSSI           : %u\n", le32(info + 0));
        printf("Error rate     : %u\n", le32(info + 4));
        printf("Signal interval: %u\n", le32(info + 8));
        printf("RSSI threshold : %u\n", le32(info + 12));
    }


    cellular_milestone("preflight complete");

    /*
     * ================================================================
     * CONNECT ACTIVATE - SESSION 0
     *
     * Cellular 2.5 APN AUTO:
     *  1) AccessString NULL / network default
     *  2) daca reteaua/modemul refuza -> fallback "broadband"
     *
     * IPv4 ramane neschimbat in aceasta versiune.
     * ================================================================
     */
    {
        static const unsigned char internet_uuid[16] = {
            0x7e,0x5e,0x2a,0x7e,
            0x4e,0x6f,
            0x72,0x72,
            0x73,0x6b,
            0x65,0x6e,0x7e,0x5e,0x2a,0x7e
        };

        int bearer_activated = 0;
        uint32_t activation_state = 0;
        const char *apn_result = "none";

        const char *preferred_env = getenv("CELLULAR_APN_PREFERRED");
        char preferred[101] = {0};

        if (preferred_env && preferred_env[0])
            snprintf(preferred, sizeof(preferred), "%s", preferred_env);

        cellular_milestone("CONNECT strategy begin");
        printf("\n============================================\n");
        printf(" CELLULAR APN AUTO 2.6\n");
        printf("============================================\n");

        /*
         * Daca helper-ul a invatat deja APN-ul pentru operatorul curent,
         * il incercam direct. Pentru TELEKOM.RO cache-ul este seed-uit
         * cu broadband pe baza bearer-ului activ demonstrat prin
         * AT+CGCONTRDP.
         *
         * Asta elimina discovery-ul lent la fiecare reconectare.
         */
        if (preferred[0]) {
            printf("Cached APN first: %s\n", preferred);

            bearer_activated = mbim_activate_session0_apn(
                h,
                preferred,
                700,
                resp,
                sizeof(resp),
                &activation_state,
                8
            );

            if (bearer_activated) {
                apn_result = preferred;
                printf("\n*** APN CACHE HIT: %s ***\n", preferred);
                cellular_milestone("bearer activated via cached APN");
            } else {
                printf("\n*** CACHED APN FAILED: %s ***\n", preferred);

                mbim_deactivate_session0_quiet(
                    h,
                    740,
                    resp,
                    sizeof(resp)
                );

                sleep(1);
                activation_state = 0;
            }
        }

        /*
         * Operator necunoscut sau cache invalid:
         * incercam network-default o singura data, cu timeout redus.
         */
        if (!bearer_activated) {
            printf("Discovery: network default (NULL APN)\n");

            bearer_activated = mbim_activate_session0_apn(
                h,
                "",
                750,
                resp,
                sizeof(resp),
                &activation_state,
                8
            );

            if (bearer_activated) {
                apn_result = "network-default";
                printf("\n*** APN DISCOVERY: NETWORK DEFAULT OK ***\n");
                cellular_milestone("bearer activated via network default");
            } else {
                printf("\n*** APN DISCOVERY: NETWORK DEFAULT FAILED ***\n");

                mbim_deactivate_session0_quiet(
                    h,
                    790,
                    resp,
                    sizeof(resp)
                );

                sleep(1);
                activation_state = 0;
            }
        }

        /*
         * Fallback universal deja validat pe acest modem.
         * Nu il repetam daca tocmai acesta a fost cache-ul esuat.
         */
        if (!bearer_activated &&
            strcasecmp(preferred, "broadband") != 0) {

            printf("Fallback: broadband\n");

            bearer_activated = mbim_activate_session0_apn(
                h,
                "broadband",
                800,
                resp,
                sizeof(resp),
                &activation_state,
                8
            );

            if (bearer_activated) {
                apn_result = "broadband";
                printf("\n*** APN FALLBACK: BROADBAND OK ***\n");
                cellular_milestone("bearer activated via broadband fallback");
            }
        }

        if (!bearer_activated)
            printf("\n*** APN AUTO: ALL ATTEMPTS FAILED ***\n");

        printf("APN strategy result: %s\n", apn_result);

        /*
         * ============================================================
         * IP CONFIGURATION QUERY
         * ============================================================
         */
        if (bearer_activated) {
            cellular_milestone("IP configuration query #1 begin");
            printf("\n============================================\n");
            printf(" MBIM IP CONFIGURATION\n");
            printf("============================================\n");

            /*
             * MBIM_IP_CONFIGURATION query:
             * fixed InformationBuffer = 60 bytes
             * numai SessionId trebuie setat; restul zero.
             */
            unsigned char ipq[108];
            memset(ipq, 0, sizeof(ipq));

            put32(ipq + 0,  3);
            put32(ipq + 4,  108);
            put32(ipq + 8,  730);

            put32(ipq + 12, 1);
            put32(ipq + 16, 0);

            memcpy(ipq + 20, basic_uuid, 16);

            put32(ipq + 36, 15);       /* IP_CONFIGURATION */
            put32(ipq + 40, 0);        /* QUERY */
            put32(ipq + 44, 60);

            /* ipq + 48: SessionId = 0 ȃ�i restul zero */

            r = libusb_control_transfer(
                h,
                0x21, 0x00, 0, IF_CTRL,
                ipq, sizeof(ipq), 5000
            );

            printf("IP CONFIG SEND: %d\n", r);

            if (r >= 0) {
                r = wait_for(
                    h,
                    0x80000003,
                    730,
                    resp,
                    sizeof(resp)
                );

                if (r >= 108) {
                    uint32_t status  = le32(resp + 40);
                    uint32_t infolen = le32(resp + 44);

                    printf("Command status : %u\n", status);
                    printf("Info length    : %u\n", infolen);

                    if (status == 0 && infolen >= 60) {
                        unsigned char *ip = resp + 48;

                        uint32_t session = le32(ip + 0);

                        uint32_t v4flags = le32(ip + 4);
                        uint32_t v6flags = le32(ip + 8);

                        uint32_t v4count = le32(ip + 12);
                        uint32_t v4off   = le32(ip + 16);

                        uint32_t v6count = le32(ip + 20);
                        uint32_t v6off   = le32(ip + 24);

                        uint32_t gw4off  = le32(ip + 28);
                        uint32_t gw6off  = le32(ip + 32);

                        uint32_t dns4count = le32(ip + 36);
                        uint32_t dns4off   = le32(ip + 40);

                        uint32_t dns6count = le32(ip + 44);
                        uint32_t dns6off   = le32(ip + 48);

                        uint32_t mtu4 = le32(ip + 52);
                        uint32_t mtu6 = le32(ip + 56);

                        printf("\n===== NEGOTIATED IP SETTINGS =====\n");
                        printf("Session ID : %u\n", session);
                        printf("IPv4 flags : 0x%08x\n", v4flags);
                        printf("IPv6 flags : 0x%08x\n", v6flags);

                        if ((v4flags & 0x1) &&
                            v4count > 0 &&
                            v4off != 0 &&
                            v4off + 8 <= infolen) {

                            unsigned char *e = ip + v4off;
                            uint32_t prefix = le32(e);

                            printf("IPv4       : %u.%u.%u.%u/%u\n",
                                   e[4], e[5], e[6], e[7], prefix);
                        } else {
                            printf("IPv4       : indisponibil\n");
                        }

                        if ((v4flags & 0x2) &&
                            gw4off != 0 &&
                            gw4off + 4 <= infolen) {

                            unsigned char *g = ip + gw4off;

                            printf("Gateway    : %u.%u.%u.%u\n",
                                   g[0], g[1], g[2], g[3]);
                        }

                        if ((v4flags & 0x4) &&
                            dns4count > 0 &&
                            dns4off != 0 &&
                            dns4off + (dns4count * 4) <= infolen) {

                            for (uint32_t i = 0; i < dns4count; i++) {
                                unsigned char *d = ip + dns4off + i * 4;

                                printf("DNS %-6u : %u.%u.%u.%u\n",
                                       i + 1,
                                       d[0], d[1], d[2], d[3]);
                            }
                        }

                        if (v4flags & 0x8)
                            printf("IPv4 MTU   : %u\n", mtu4);

                        printf("IPv6 count : %u\n", v6count);
                        printf("IPv6 offset: %u\n", v6off);
                        printf("IPv6 GW off: %u\n", gw6off);
                        printf("IPv6 DNS   : %u\n", dns6count);
                        printf("IPv6 DNSoff: %u\n", dns6off);
                        printf("IPv6 MTU   : %u\n", mtu6);

                        printf("\n*** IP CONFIGURATION REUSIT ***\n");
                        cellular_milestone("IP configuration query #1 complete");
                    }
                }
            }
        }

        cellular_milestone("bridge/IP configuration #2 begin");

        /*
         * ============================================================
         * REAL macOS UTUN <-> MBIM BRIDGE
         * ============================================================
         */
        if (bearer_activated) {

            printf("\n============================================\n");
            printf(" CREATE REAL macOS LTE INTERFACE\n");
            printf("============================================\n");

            /*
             * Citim din nou IP_CONFIGURATION, ca să� configură�m
             * interfaȃ�a utun cu valorile reale negociate.
             */
            unsigned char bq[108];
            memset(bq, 0, sizeof(bq));

            put32(bq + 0,  3);
            put32(bq + 4,  108);
            put32(bq + 8,  741);
            put32(bq + 12, 1);
            put32(bq + 16, 0);

            memcpy(bq + 20, basic_uuid, 16);

            put32(bq + 36, 15);   /* IP_CONFIGURATION */
            put32(bq + 40, 0);    /* QUERY */
            put32(bq + 44, 60);

            r = libusb_control_transfer(
                h,
                0x21, 0x00, 0, IF_CTRL,
                bq, sizeof(bq), 5000
            );

            if (r >= 0) {
                r = wait_for(
                    h,
                    0x80000003,
                    741,
                    resp,
                    sizeof(resp)
                );
            }

            if (r >= 108 &&
                le32(resp + 40) == 0) {

                unsigned char *ipcfg = resp + 48;

                uint32_t flags     = le32(ipcfg + 4);
                uint32_t v4count   = le32(ipcfg + 12);
                uint32_t v4off     = le32(ipcfg + 16);
                uint32_t gwoff     = le32(ipcfg + 28);
                uint32_t dnscount  = le32(ipcfg + 36);
                uint32_t dnsoff    = le32(ipcfg + 40);
                uint32_t mtu       = le32(ipcfg + 52);
                uint32_t infolen   = le32(resp + 44);

                if ((flags & 0x0f) == 0x0f &&
                    v4count > 0 &&
                    dnscount > 0 &&
                    v4off + 8 <= infolen &&
                    gwoff + 4 <= infolen &&
                    dnsoff + 4 <= infolen) {

                    unsigned char *v4e = ipcfg + v4off;
                    unsigned char *src = v4e + 4;
                    unsigned char *gw  = ipcfg + gwoff;
                    unsigned char *dns = ipcfg + dnsoff;

                    char srcs[32], gws[32], dnss[32], dns2s[32];

                    snprintf(srcs, sizeof(srcs),
                             "%u.%u.%u.%u",
                             src[0],src[1],src[2],src[3]);

                    snprintf(gws, sizeof(gws),
                             "%u.%u.%u.%u",
                             gw[0],gw[1],gw[2],gw[3]);

                    snprintf(dnss, sizeof(dnss),
                             "%u.%u.%u.%u",
                             dns[0],dns[1],dns[2],dns[3]);

                    if (dnscount > 1 &&
                        dnsoff + 8 <= infolen) {
                        unsigned char *dns2 = ipcfg + dnsoff + 4;

                        snprintf(dns2s, sizeof(dns2s),
                                 "%u.%u.%u.%u",
                                 dns2[0],dns2[1],dns2[2],dns2[3]);
                    } else {
                        snprintf(dns2s, sizeof(dns2s),
                                 "%s", dnss);
                    }

                    if (!mtu)
                        mtu = 1430;

                    char ifname[64];
                    int utun_fd = create_utun(
                        ifname,
                        sizeof(ifname)
                    );

                    if (utun_fd >= 0) {

                        printf("UTUN       : %s\n", ifname);
                        printf("IPv4       : %s\n", srcs);
                        printf("Peer/GW    : %s\n", gws);
                        printf("DNS test   : %s\n", dnss);
                        printf("MTU        : %u\n", mtu);

                        char cmd[512];

                        snprintf(
                            cmd, sizeof(cmd),
                            "/sbin/ifconfig %s inet %s %s mtu %u up",
                            ifname, srcs, gws, mtu
                        );

                        printf("\n$ %s\n", cmd);

                        int cfg = system(cmd);

                        if (cfg == 0) {

                            /*
                             * Nu schimbă�m default route.
                             * Numai DNS1 este trimis prin LTE.
                             */
                            snprintf(
                                cmd, sizeof(cmd),
                                "/sbin/route -n add -net 0.0.0.0/1 -interface %s && /sbin/route -n add -net 128.0.0.0/1 -interface %s",
                                ifname, ifname
                            );

                            printf("$ %s\n", cmd);

                            int rr = system(cmd);

                            if (rr == 0) {

                                SCDynamicStoreRef lte_store =
                                    publish_lte_state(
                                        ifname,
                                        srcs,
                                        gws,
                                        dnss,
                                        dns2s
                                    );

                                if (!lte_store) {
                                    fprintf(stderr,
                                            "ATENȃ�IE: DNS LTE nu a fost publicat.\n");
                                }

                                /*
                                 * Explicit NTB16 + input buffer.
                                 */
                                libusb_control_transfer(
                                    h,
                                    0x21,
                                    0x84,
                                    0,
                                    IF_CTRL,
                                    NULL,
                                    0,
                                    3000
                                );

                                unsigned char insz[4] = {
                                    0x00,0x40,0x00,0x00
                                };

                                libusb_control_transfer(
                                    h,
                                    0x21,
                                    0x86,
                                    0,
                                    IF_CTRL,
                                    insz,
                                    sizeof(insz),
                                    3000
                                );

                                struct bridge_ctx bc;
                                memset(&bc, 0, sizeof(bc));

                                bc.h = h;
                                bc.utun_fd = utun_fd;

                                pthread_t tx_thread;
                                pthread_t rx_thread;

                                bridge_stop = 0;

                                if (install_bridge_signal_handlers() != 0) {
                                    fprintf(stderr,
                                            "Nu pot instala signal handlers\n");
                                    close(utun_fd);
                                    goto cleanup;
                                }

                                int txok = pthread_create(
                                    &tx_thread,
                                    NULL,
                                    utun_to_mbim,
                                    &bc
                                );

                                int rxok = pthread_create(
                                    &rx_thread,
                                    NULL,
                                    mbim_to_utun,
                                    &bc
                                );

                                if (txok == 0 &&
                                    rxok == 0) {

                                    printf("\n");
                                    printf("***********************************************\n");
                                    printf("* LTE UTUN BRIDGE ESTE ACTIV                  *\n");
                                    printf("* Interface: %-31s*\n", ifname);
                                    printf("***********************************************\n");
                                    cellular_milestone("utun + routes ready");

                                    printf("\nÃ�n TERMINALUL 2 rulează�:\n\n");
                                    printf("route -n get 1.1.1.1 | grep interface\n");
                                    printf("scutil --dns | head -60\n");
                                    printf("dig example.com A +short\n");
                                    printf("curl -4 -I https://example.com\n");
                                    printf("\nDacă� merg, poȃ�i testa ȃ�i Safari.\n");
                                    printf("Pentru oprire revino aici ȃ�i apasă� Ctrl-C.\n\n");

                                    while (!bridge_stop)
                                        sleep(1);

                                    bridge_stop = 1;

                                    pthread_join(
                                        tx_thread,
                                        NULL
                                    );

                                    pthread_join(
                                        rx_thread,
                                        NULL
                                    );

                                    printf("\nBridge oprit.\n");
                                    printf("TX packets: %llu\n",
                                           bc.tx_packets);
                                    printf("RX packets: %llu\n",
                                           bc.rx_packets);

                                } else {
                                    fprintf(stderr,
                                            "pthread_create a eȃ�uat\n");

                                    bridge_stop = 1;

                                    if (txok == 0)
                                        pthread_join(
                                            tx_thread, NULL);

                                    if (rxok == 0)
                                        pthread_join(
                                            rx_thread, NULL);
                                }

                                /*
                                 * Eliberarea sesiunii ȃ�terge valorile
                                 * temporare SystemConfiguration.
                                 */
                                if (lte_store) {
                                    CFRelease(lte_store);
                                    lte_store = NULL;
                                    printf("DNS/SystemConfiguration LTE eliminat.\n");
                                }

                                /*
                                 * Elimină�m numai rutele /1 create de noi.
                                 * Default route original nu a fost modificat.
                                 */
                                snprintf(
                                    cmd, sizeof(cmd),
                                    "/sbin/route -n delete -net 0.0.0.0/1 -interface %s >/dev/null 2>&1; "
                                    "/sbin/route -n delete -net 128.0.0.0/1 -interface %s >/dev/null 2>&1",
                                    ifname, ifname
                                );

                                system(cmd);

                                printf("Rutele LTE eliminate.\n");

                            } else {
                                printf("Nu am putut adă�uga ruta de test.\n");
                            }
                        } else {
                            printf("Nu am putut configura utun.\n");
                        }

                        close(utun_fd);

                        printf("UTUN închis.\n");
                    }
                }
            }
        }


        /*
         * ============================================================
         * DEACTIVATE SESSION 0
         * Testul nu lasă� bearer-ul OS activ după� terminare.
         * ============================================================
         */
        if (bearer_activated) {
            printf("\n============================================\n");
            printf(" DEACTIVATE SESSION 0\n");
            printf("============================================\n");

            unsigned char deact[108];
            memset(deact, 0, sizeof(deact));

            put32(deact + 0,  3);
            put32(deact + 4,  108);
            put32(deact + 8,  790);

            put32(deact + 12, 1);
            put32(deact + 16, 0);

            memcpy(deact + 20, basic_uuid, 16);

            put32(deact + 36, 12);      /* CONNECT */
            put32(deact + 40, 1);       /* SET */
            put32(deact + 44, 60);      /* fixed connect info */

            unsigned char *di = deact + 48;

            put32(di + 0,  0);          /* Session 0 */
            put32(di + 4,  0);          /* DEACTIVATE */

            put32(di + 32, 0);          /* Compression NONE */
            put32(di + 36, 0);          /* Auth NONE */
            put32(di + 40, 1);          /* IPv4 */
            memcpy(di + 44, internet_uuid, 16);

            r = libusb_control_transfer(
                h,
                0x21, 0x00, 0, IF_CTRL,
                deact, sizeof(deact), 5000
            );

            printf("DEACTIVATE SEND: %d\n", r);

            if (r >= 0) {
                r = wait_for(
                    h,
                    0x80000003,
                    790,
                    resp,
                    sizeof(resp)
                );

                if (r >= 84) {
                    printf("Command status  : %u\n", le32(resp + 40));

                    if (le32(resp + 40) == 0) {
                        unsigned char *info = resp + 48;

                        printf("Activation state: %u\n",
                               le32(info + 4));
                        printf("Network error   : %u\n",
                               le32(info + 32));

                        printf("*** SESSION 0 DEACTIVATED ***\n");
                    }
                }
            }
        }
    }

    /* CLOSE TID 599 */
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
        if (r >= 16)
            printf("*** MBIM CLOSE OK, status=%u ***\n",
                   le32(resp + 12));
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
