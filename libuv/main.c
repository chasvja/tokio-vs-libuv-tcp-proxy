#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <uv.h>

uv_loop_t *loop;
struct sockaddr_in listen_addr;
struct sockaddr_in server_addr;

typedef struct conn_ctx {
  uv_tcp_t   *client_conn;
  char       *client_rbuf;
  uv_write_t *client_wreq;

  uv_tcp_t   *server_conn;
  char       *server_rbuf;
  uv_write_t *server_wreq;
} conn_ctx;

void alloc_client_buf(uv_handle_t *handle, size_t size, uv_buf_t *buf) {
  conn_ctx *ctx = (conn_ctx *)handle->data;
  if (!ctx->client_rbuf) {
    ctx->client_rbuf = (char *)malloc(8192);
  }

  buf->base = ctx->client_rbuf;
  buf->len = 8192;
}

void alloc_server_buf(uv_handle_t *handle, size_t size, uv_buf_t *buf) {
  conn_ctx *ctx = (conn_ctx *)handle->data;
  if (!ctx->server_rbuf) {
    ctx->server_rbuf = (char *)malloc(8192);
  }

  buf->base = ctx->server_rbuf;
  buf->len = 8192;
}

void after_write(uv_write_t *req, int status) {
    if (status) {
        fprintf(stderr, "Write error %s\n", uv_strerror(status));
    }
}

void on_client_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "Client read error %s\n", uv_err_name(nread));
            uv_close((uv_handle_t *)client, NULL);
        }
    } else if (nread > 0) {
        conn_ctx *ctx = (conn_ctx *)client->data;
        uv_buf_t wbuf = uv_buf_init(buf->base, nread);
        uv_write(ctx->server_wreq, (uv_stream_t *)ctx->server_conn, &wbuf, 1, after_write);
    }
}

void on_server_read(uv_stream_t *server, ssize_t nread, const uv_buf_t *buf) {
    if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "Server read error %s\n", uv_err_name(nread));
            uv_close((uv_handle_t *)server, NULL);
        }
    } else if (nread > 0) {
        conn_ctx *ctx = (conn_ctx *)server->data;
        uv_buf_t wbuf = uv_buf_init(buf->base, nread);
        uv_write(ctx->client_wreq, (uv_stream_t *)ctx->client_conn, &wbuf, 1, after_write);
    }
}

void on_server_conn(uv_connect_t *req, int status) {
  if (status < 0) {
    fprintf(stderr, "New server connection error %s\n", uv_strerror(status));
    return;
  }

  conn_ctx *ctx = (conn_ctx *)req->data;
  ctx->server_conn = (uv_tcp_t *)req->handle;
  ctx->server_wreq = (uv_write_t *)malloc(sizeof(uv_write_t));

  ctx->client_conn->data = ctx;
  ctx->server_conn->data = ctx;

  printf("server connected, proxying...");

  uv_read_start((uv_stream_t *)ctx->client_conn, alloc_client_buf, on_client_read);
  uv_read_start((uv_stream_t *)ctx->server_conn, alloc_server_buf, on_server_read);
}

void on_client_conn(uv_stream_t *server, int status) {
    if (status < 0) {
        fprintf(stderr, "New client connection error %s\n", uv_strerror(status));
        return;
    }

    uv_tcp_t *client = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);

    conn_ctx *ctx = (conn_ctx *)malloc(sizeof(conn_ctx));
    ctx->client_conn = client;
    ctx->client_wreq = (uv_write_t *)malloc(sizeof(uv_write_t));

    if (uv_accept(server, (uv_stream_t *)client) == 0) {
        printf("got new client connection...");

        uv_tcp_t *server = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
        uv_tcp_init(loop, server);

        uv_connect_t *server_req = (uv_connect_t *)malloc(sizeof(uv_connect_t));
        server_req->data = ctx;

        if (uv_tcp_connect(server_req, server, (const struct sockaddr*)&server_addr, on_server_conn) == 0) {
            printf("connecting to server...");
        }
    }
}

int main() {
    loop = uv_default_loop();

    uv_tcp_t server;
    uv_tcp_init(loop, &server);

    uv_ip4_addr("0.0.0.0", 6380, &listen_addr);
    uv_ip4_addr("127.0.0.1", 6379, &server_addr);

    uv_tcp_bind(&server, (const struct sockaddr*)&listen_addr, 0);
    int r = uv_listen((uv_stream_t*)&server, 128, on_client_conn);
    if (r) {
        fprintf(stderr, "Listen error %s\n", uv_strerror(r));
        return 1;
    }
    return uv_run(loop, UV_RUN_DEFAULT);
}
