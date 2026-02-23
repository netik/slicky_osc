#ifndef CONFIG_H
#define CONFIG_H

#define MAX_STR 255
#define VENDOR_ID 0x04D8
#define PRODUCT_ID 0xEC24
#define SSDP_INTERVAL 5   /* Send SSDP announcements every 5 seconds */
#define STATUS_INTERVAL 1 /* Send /status to last sender every 1 second */
#define SSDP_PORT 1901
#define FEEDBACK_PORT 9500  /* UDP port for status/feedback (distinct from incoming OSC port) */
#define SSDP_MULTICAST_IP "239.255.255.250"

#endif /* CONFIG_H */
