#ifndef _WLAN_H
#define _WLAN_H

#include <stdint.h>
#include "ioctl_abi.h"

/*
 * Thin helpers over the /dev/wlan0 ioctls (CACT_WLANCTL_*).
 *
 * The node is a raw 802.11 radio: wl_tx/wl_rx move management and EAPOL frames.
 * The intended caller is wljoin (the connection utility) — scan, choose an AP,
 * auth/assoc, WPA2, then install the keys.  All return 0 on success and -1 with
 * errno set on failure unless noted.
 */

/* Rescan and fill the driver's AP list; returns the AP count (>= 0) or -1. */
int wl_scan(int fd);

/* Transmit one raw 802.11 frame (management / EAPOL / data). */
int wl_tx(int fd, const void *frame, uint32_t len);

/* Fetch one queued received frame.  Returns 1 and *len when a frame was
 * copied, 0 when the queue is empty, -1 on error. */
int wl_rx(int fd, void *frame, uint32_t *len);

int wl_set_channel(int fd, uint32_t channel);
int wl_set_bssid(int fd, const uint8_t bssid[6]);

/* kind = CACT_WLAN_KEY_PAIRWISE (TK) or CACT_WLAN_KEY_GROUP (GTK); key_id is
 * the GTK key id the AP assigned (ignored for the pairwise key). */
int wl_set_key(int fd, uint32_t kind, uint32_t key_id, const void *key,
               uint32_t key_len);

int wl_status(int fd, cact_wlan_status_t *st);

/* Program the AP's basic-rate set and ERP timings (from its beacon) before the
 * first frame goes out: management and EAPOL then use the lowest basic rate,
 * as mac80211 does.  A zero basic mask leaves the driver's default (CCK 1M). */
int wl_set_rates(int fd, const cact_wlan_rates_t *rates);

/* Mark the datapath usable (1) or down (0) once association/handshake finish. */
int wl_set_link(int fd, int up);

#endif /* _WLAN_H */
