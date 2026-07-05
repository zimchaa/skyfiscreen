// lwIP configuration for SkyFi Screen (pico_cyw43_arch_lwip_poll, NO_SYS).
// Based on pico-examples' common lwipopts.
#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// The presto board header redirects lwIP buffer pools into a ".psram_data"
// section (for the MicroPython build, which links + initialises PSRAM).
// Our pico-sdk build has neither the linker section nor PSRAM init, so undo
// that and keep lwIP in ordinary SRAM.
#ifdef LWIP_DECLARE_MEMORY_ALIGNED
#undef LWIP_DECLARE_MEMORY_ALIGNED
#endif

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    8000
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_DHCP                   1
#define LWIP_TCP_KEEPALIVE          1

#define TCP_WND                     (8 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETCONN_FULLDUPLEX     0

#define LWIP_STATS                  0
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP_DOES_ACD_CHECK    0

#define DHCP_DOES_ARP_CHECK         0
#define SYS_LIGHTWEIGHT_PROT        1

#ifndef NDEBUG
#define LWIP_DEBUG                  0
#endif

#endif
