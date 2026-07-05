#include "http_post.hpp"

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include <cstdio>
#include <cstring>

static struct {
    struct tcp_pcb* pcb;
    http_post_cb cb;
    char req[768];
    size_t req_len;
    char resp[1024];
    size_t resp_len;
    uint8_t poll_ticks;
    bool busy;
} s;

bool http_post_busy() { return s.busy; }

static void finish(int status, const char* body, size_t body_len) {
    if (s.pcb) {
        tcp_arg(s.pcb, nullptr);
        tcp_recv(s.pcb, nullptr);
        tcp_err(s.pcb, nullptr);
        tcp_poll(s.pcb, nullptr, 0);
        if (tcp_close(s.pcb) != ERR_OK) tcp_abort(s.pcb);
        s.pcb = nullptr;
    }
    s.busy = false;
    if (s.cb) s.cb(status, body, body_len);
}

// Parse "HTTP/1.1 200 ..." + locate the body after the blank line.
static void finish_from_response() {
    s.resp[s.resp_len] = '\0';
    int status = -1;
    if (sscanf(s.resp, "HTTP/%*d.%*d %d", &status) != 1) {
        finish(-1, nullptr, 0);
        return;
    }
    const char* body = strstr(s.resp, "\r\n\r\n");
    if (body) {
        body += 4;
        finish(status, body, s.resp_len - (size_t)(body - s.resp));
    } else {
        finish(status, nullptr, 0);
    }
}

static err_t recv_cb(void*, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    if (!p) {                       // remote closed: response complete
        finish_from_response();
        return ERR_OK;
    }
    if (err == ERR_OK) {
        size_t space = sizeof(s.resp) - 1 - s.resp_len;
        size_t n = pbuf_copy_partial(p, s.resp + s.resp_len,
                                     (u16_t)(p->tot_len < space ? p->tot_len : space), 0);
        s.resp_len += n;
        tcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static void err_cb(void*, err_t) {
    s.pcb = nullptr;                // pcb already freed by lwIP
    finish(-2, nullptr, 0);
}

static err_t poll_cb(void*, struct tcp_pcb*) {
    if (++s.poll_ticks >= 10) {     // ~5s at the default 500ms poll interval
        finish(-3, nullptr, 0);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t connected_cb(void*, struct tcp_pcb* pcb, err_t err) {
    if (err != ERR_OK) {
        finish(-4, nullptr, 0);
        return ERR_OK;
    }
    err_t w = tcp_write(pcb, s.req, (u16_t)s.req_len, TCP_WRITE_FLAG_COPY);
    if (w == ERR_OK) w = tcp_output(pcb);
    if (w != ERR_OK) finish(-5, nullptr, 0);
    return ERR_OK;
}

bool http_post(const char* host_ip, uint16_t port, const char* path,
               const char* json_body, http_post_cb cb) {
    if (s.busy || !host_ip || !path || !json_body) return false;

    ip_addr_t addr;
    if (!ipaddr_aton(host_ip, &addr)) return false;

    int n = snprintf(s.req, sizeof(s.req),
                     "POST %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",
                     path, host_ip, (unsigned)strlen(json_body), json_body);
    if (n <= 0 || (size_t)n >= sizeof(s.req)) return false;
    s.req_len = (size_t)n;
    s.resp_len = 0;
    s.poll_ticks = 0;
    s.cb = cb;

    s.pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!s.pcb) return false;
    tcp_recv(s.pcb, recv_cb);
    tcp_err(s.pcb, err_cb);
    tcp_poll(s.pcb, poll_cb, 1);    // every ~500ms

    s.busy = true;
    if (tcp_connect(s.pcb, &addr, port, connected_cb) != ERR_OK) {
        tcp_abort(s.pcb);
        s.pcb = nullptr;
        s.busy = false;
        return false;
    }
    return true;
}
