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

int wl_get_bss(int fd, cact_wlan_bss_t *bss) {
    if (!bss)
        return nio_map(-22 /* EINVAL */);
    return nio_map(ioctl(fd, CACT_WLANCTL_GET_BSS, bss));
}

int wl_connect(int fd, const uint8_t bssid[6], const uint8_t *ssid,
               uint8_t ssid_len) {
    cact_wlan_connect_t c;

    if (!bssid)
        return nio_map(-22 /* EINVAL */);
    memset(&c, 0, sizeof(c));
    memcpy(c.bssid, bssid, 6);
    if (ssid && ssid_len) {
        if (ssid_len > CACT_WLAN_SSID_MAX - 1)
            ssid_len = CACT_WLAN_SSID_MAX - 1;
        memcpy(c.ssid, ssid, ssid_len);
        c.ssid_len = ssid_len;
    }
    return nio_map(ioctl(fd, CACT_WLANCTL_CONNECT, &c));
}

int wl_disconnect(int fd) {
    return nio_map(ioctl(fd, CACT_WLANCTL_DISCONNECT, 0));
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

int wl_eapol_tx(int fd, const uint8_t dst[6], const void *eapol, uint32_t len) {
    static cact_wlan_eapol_t e;

    if (!eapol || len == 0 || len > CACT_WLAN_EAPOL_MAX)
        return nio_map(-22 /* EINVAL */);
    memset(&e, 0, sizeof(e));
    if (dst)
        memcpy(e.dst, dst, 6);
    e.len = len;
    memcpy(e.data, eapol, len);
    return nio_map(ioctl(fd, CACT_WLANCTL_EAPOL_TX, &e));
}

int wl_eapol_rx(int fd, void *eapol, uint32_t *len, uint8_t src[6]) {
    static cact_wlan_eapol_t e;
    int r;

    e.len = 0;
    r = nio_map(ioctl(fd, CACT_WLANCTL_EAPOL_RX, &e));
    if (r < 0)
        return -1;
    if (e.len == 0)
        return 0;
    if (len)
        *len = e.len;
    if (eapol)
        memcpy(eapol, e.data, e.len);
    if (src)
        memcpy(src, e.dst, 6);
    return 1;
}
