#include "quickjs-debugger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <poll.h>
#include <arpa/inet.h>

struct js_transport_data {
    int handle;
} js_transport_data;

static size_t js_transport_read(void *udata, char *buffer, size_t length) {
    struct js_transport_data* data = (struct js_transport_data *)udata;
    if (data->handle <= 0)
        return -1;

    if (length == 0)
        return -2;

    if (buffer == NULL)
        return -3;

    ssize_t ret = read(data->handle, (void *)buffer, length);
    if (ret < 0)
        return -4;

    if (ret == 0)
        return -5;

    if (ret > length)
        return -6;

    return ret;
}

static size_t js_transport_write(void *udata, const char *buffer, size_t length) {
    struct js_transport_data* data = (struct js_transport_data *)udata;
    if (data->handle <= 0)
        return -1;

    if (length == 0)
        return -2;

    if (buffer == NULL)
        return -3;

    size_t ret = write(data->handle, (const void *) buffer, length);
    if (ret <= 0 || ret > (ssize_t) length)
        return -4;

    return ret;
}

static size_t js_transport_peek(void *udata) {
    struct pollfd fds[1];
    int poll_rc;

    struct js_transport_data* data = (struct js_transport_data *)udata;
    if (data->handle <= 0)
        return -1;

    fds[0].fd = data->handle;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    poll_rc = poll(fds, 1, 0);
    if (poll_rc < 0)
        return -2;
    if (poll_rc > 1)
        return -3;
    // no data
    if (poll_rc == 0)
        return 0;
    // has data
    return 1;
}

static void js_transport_close(JSRuntime* rt, void *udata) {
    struct js_transport_data* data = (struct js_transport_data *)udata;
    if (data->handle <= 0)
        return;
    close(data->handle);
    data->handle = 0;
    free(udata);
}

// todo: fixup asserts to return errors.
static struct sockaddr_in js_debugger_parse_sockaddr(const char* address) {
    char* port_string = strstr(address, ":");
    assert(port_string);

    int port = atoi(port_string + 1);
    assert(port);

    char host_string[256];
    strcpy(host_string, address);
    host_string[port_string - address] = 0;

    struct hostent *host = gethostbyname(host_string);
    assert(host);
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy((char *)&addr.sin_addr.s_addr, (char *)host->h_addr, host->h_length);
    addr.sin_port = htons(port);

    return addr;
}

// timeout_ms < 0 = wait indefinitely; >= 0 = return -3 after that many milliseconds.
// Returns >= 0 (fd) on success, -1 on socket error, -2 on bind error, -3 on timeout.
int js_debugger_accept_connection(const char *address, int timeout_ms) {
    struct sockaddr_in addr = js_debugger_parse_sockaddr(address);

    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) return -1;

    int reuseAddress = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuseAddress, sizeof(reuseAddress));

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server);
        return -2;
    }

    listen(server, 1);

    int elapsed_ms = 0;
    const int interval_ms = 500;
    while (timeout_ms < 0 || elapsed_ms < timeout_ms) {
        int wait = interval_ms;
        if (timeout_ms >= 0 && (timeout_ms - elapsed_ms) < wait)
            wait = timeout_ms - elapsed_ms;

        struct pollfd fds[1];
        fds[0].fd = server;
        fds[0].events = POLLIN;
        fds[0].revents = 0;

        int rc = poll(fds, 1, wait);
        if (rc > 0 && (fds[0].revents & POLLIN)) {
            struct sockaddr_in client_addr;
            socklen_t client_addr_size = (socklen_t)sizeof(client_addr);
            int client = accept(server, (struct sockaddr *)&client_addr, &client_addr_size);
            close(server);
            return client;
        }
        elapsed_ms += wait;
    }

    close(server);
    return -3; /* timeout */
}

void js_debugger_attach_handle(JSContext *ctx, int handle) {
    struct js_transport_data *data = (struct js_transport_data *)malloc(sizeof(struct js_transport_data));
    memset(data, 0, sizeof(js_transport_data));
    data->handle = handle;
    js_debugger_attach(ctx, js_transport_read, js_transport_write, js_transport_peek, js_transport_close, data);
}

void js_debugger_connect(JSContext *ctx, const char *address) {
    struct sockaddr_in addr = js_debugger_parse_sockaddr(address);

    int client = socket(AF_INET, SOCK_STREAM, 0);
    assert(client > 0);

    assert(!connect(client, (const struct sockaddr *)&addr, sizeof(addr)));

    struct js_transport_data *data = (struct js_transport_data *)malloc(sizeof(struct js_transport_data));
    memset(data, 0, sizeof(js_transport_data));
    data->handle = client;
    js_debugger_attach(ctx, js_transport_read, js_transport_write, js_transport_peek, js_transport_close, data);
}

void js_debugger_wait_connection(JSContext *ctx, const char* address) {
    struct sockaddr_in addr = js_debugger_parse_sockaddr(address);

    int server = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);

    int reuseAddress = 1;
    assert(setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char *) &reuseAddress, sizeof(reuseAddress)) >= 0);

    assert(bind(server, (struct sockaddr *) &addr, sizeof(addr)) >= 0);

    listen(server, 1);

    struct sockaddr_in client_addr;
    socklen_t client_addr_size = (socklen_t) sizeof(addr);
    int client = accept(server, (struct sockaddr *) &client_addr, &client_addr_size);
    close(server);
    assert(client >= 0);

    struct js_transport_data *data = (struct js_transport_data *)malloc(sizeof(struct js_transport_data));
    memset(data, 0, sizeof(js_transport_data));
    data->handle = client;
    js_debugger_attach(ctx, js_transport_read, js_transport_write, js_transport_peek, js_transport_close, data);
}
