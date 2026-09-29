/*
 * /dev/wlan0 helpers (CACT_WLANCTL_*).  See include/wlan.h.
 *
 * These are plain ioctl relays on the open node, in the same style as the rest
 * of the relay layer: build the argument struct, issue one ioctl, map the
 * kernel's -errno onto the POSIX convention with nio_map().
 */

#include <stdint.h>
#include <string.h>
#include "unistd.h"
#include "ioctl_abi.h"
#include "wlan.h"
#include "nodeio.h"

int wl_scan(int fd) {
    return nio_map(ioctl(fd, CACT_WLANCTL_SCAN, 0));
}

int wl_tx(int fd, const void *frame, uint32_t len) {
    cact_wlan_frame_t f;

    if (!frame || len == 0 || len > CACT_WLAN_FRAME_MAX) {
        return nio_map(-22 /* EINVAL */);
    }
    memset(&f, 0, sizeof(f));
    f.len = len;
    memcpy(f.data, frame, len);
    return nio_map(ioctl(fd, CACT_WLANCTL_TX, &f));
}

int wl_rx(int fd, void *frame, uint32_t *len) {
    cact_wlan_frame_t f;
    int r = nio_map(ioctl(fd, CACT_WLANCTL_RX, &f));

    if (r < 0)
        return -1;
    if (f.len == 0)
        return 0;
    if (len)
        *len = f.len;
    if (frame)
        memcpy(frame, f.data, f.len);
    return 1;
}

int wl_set_channel(int fd, uint32_t channel) {
    cact_wlan_channel_t c;

    c.channel = channel;
    return nio_map(ioctl(fd, CACT_WLANCTL_SET_CHANNEL, &c));
}

int wl_set_bssid(int fd, const uint8_t bssid[6]) {
    cact_wlan_bssid_t b;

    memcpy(b.bssid, bssid, 6);
    return nio_map(ioctl(fd, CACT_WLANCTL_SET_BSSID, &b));
}

int wl_set_key(int fd, uint32_t kind, uint32_t key_id, const void *key,
               uint32_t key_len) {
    cact_wlan_key_t k;

    if (!key || key_len == 0 || key_len > CACT_WLAN_KEY_MAX)
        return nio_map(-22 /* EINVAL */);
    memset(&k, 0, sizeof(k));
    k.kind = kind;
    k.key_id = key_id;
    k.key_len = key_len;
    memcpy(k.key, key, key_len);
    return nio_map(ioctl(fd, CACT_WLANCTL_SET_KEY, &k));
}

int wl_status(int fd, cact_wlan_status_t *st) {
    if (!st)
        return nio_map(-22 /* EINVAL */);
    return nio_map(ioctl(fd, CACT_WLANCTL_STATUS, st));
}

int wl_set_rates(int fd, const cact_wlan_rates_t *rates) {
    cact_wlan_rates_t r;

    if (!rates)
        return nio_map(-22 /* EINVAL */);
    r = *rates;
    return nio_map(ioctl(fd, CACT_WLANCTL_SET_RATES, &r));
}

int wl_set_link(int fd, int up) {
    int v = up ? 1 : 0;

    return nio_map(ioctl(fd, CACT_WLANCTL_SET_LINK, &v));
}
