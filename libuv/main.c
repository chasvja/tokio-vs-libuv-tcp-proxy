#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <uv.h>

// 定义全局的事件循环指针
uv_loop_t *loop;
// 定义监听地址的结构体，用于存储服务器监听的地址和端口
struct sockaddr_in listen_addr;
// 定义目标服务器地址的结构体，用于存储代理连接的目标服务器地址和端口
struct sockaddr_in server_addr;

// 定义连接上下文结构体，用于存储客户端和服务器连接的相关信息
typedef struct conn_ctx {
    // 客户端连接的 TCP 句柄指针
    uv_tcp_t   *client_conn;
    // 客户端读取缓冲区的指针
    char       *client_rbuf;
    // 客户端写入请求的指针
    uv_write_t *client_wreq;

    // 服务器连接的 TCP 句柄指针
    uv_tcp_t   *server_conn;
    // 服务器读取缓冲区的指针
    char       *server_rbuf;
    // 服务器写入请求的指针
    uv_write_t *server_wreq;
} conn_ctx;

// 为客户端连接分配读取缓冲区的函数
void alloc_client_buf(uv_handle_t *handle, size_t size, uv_buf_t *buf) {
    // 获取连接上下文结构体指针
    conn_ctx *ctx = (conn_ctx *)handle->data;
    // 如果客户端读取缓冲区未分配，则分配 8192 字节的内存
    if (!ctx->client_rbuf) {
        ctx->client_rbuf = (char *)malloc(8192);
    }

    // 设置缓冲区的基地址和长度
    buf->base = ctx->client_rbuf;
    buf->len = 8192;
}

// 为服务器连接分配读取缓冲区的函数
void alloc_server_buf(uv_handle_t *handle, size_t size, uv_buf_t *buf) {
    // 获取连接上下文结构体指针
    conn_ctx *ctx = (conn_ctx *)handle->data;
    // 如果服务器读取缓冲区未分配，则分配 8192 字节的内存
    if (!ctx->server_rbuf) {
        ctx->server_rbuf = (char *)malloc(8192);
    }

    // 设置缓冲区的基地址和长度
    buf->base = ctx->server_rbuf;
    buf->len = 8192;
}

// 写入操作完成后的回调函数
void after_write(uv_write_t *req, int status) {
    // 如果写入操作出现错误，输出错误信息
    if (status) {
        fprintf(stderr, "Write error %s\n", uv_strerror(status));
    }
}

// 客户端读取数据的回调函数
void on_client_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    // 如果读取数据出现错误
    if (nread < 0) {
        // 如果不是 EOF 错误，输出错误信息并关闭客户端连接
        if (nread != UV_EOF) {
            fprintf(stderr, "Client read error %s\n", uv_err_name(nread));
            uv_close((uv_handle_t *)client, NULL);
        }
    } 
    // 如果成功读取到数据
    else if (nread > 0) {
        // 获取连接上下文结构体指针
        conn_ctx *ctx = (conn_ctx *)client->data;
        // 初始化写入缓冲区
        uv_buf_t wbuf = uv_buf_init(buf->base, nread);
        // 将数据写入服务器连接，并在完成后调用 after_write 回调函数
        uv_write(ctx->server_wreq, (uv_stream_t *)ctx->server_conn, &wbuf, 1, after_write);
    }
}

// 服务器读取数据的回调函数
void on_server_read(uv_stream_t *server, ssize_t nread, const uv_buf_t *buf) {
    // 如果读取数据出现错误
    if (nread < 0) {
        // 如果不是 EOF 错误，输出错误信息并关闭服务器连接
        if (nread != UV_EOF) {
            fprintf(stderr, "Server read error %s\n", uv_err_name(nread));
            uv_close((uv_handle_t *)server, NULL);
        }
    } 
    // 如果成功读取到数据
    else if (nread > 0) {
        // 获取连接上下文结构体指针
        conn_ctx *ctx = (conn_ctx *)server->data;
        // 初始化写入缓冲区
        uv_buf_t wbuf = uv_buf_init(buf->base, nread);
        // 将数据写入客户端连接，并在完成后调用 after_write 回调函数
        uv_write(ctx->client_wreq, (uv_stream_t *)ctx->client_conn, &wbuf, 1, after_write);
    }
}

// 与服务器建立连接后的回调函数
void on_server_conn(uv_connect_t *req, int status) {
    // 如果连接失败，输出错误信息并返回
    if (status < 0) {
        fprintf(stderr, "New server connection error %s\n", uv_strerror(status));
        return;
    }

    // 获取连接上下文结构体指针
    conn_ctx *ctx = (conn_ctx *)req->data;
    // 将连接的服务器句柄赋值给上下文结构体
    ctx->server_conn = (uv_tcp_t *)req->handle;
    // 为服务器写入请求分配内存
    ctx->server_wreq = (uv_write_t *)malloc(sizeof(uv_write_t));

    // 将连接上下文结构体指针分别赋值给客户端和服务器连接的 data 字段
    ctx->client_conn->data = ctx;
    ctx->server_conn->data = ctx;

    // 输出服务器连接成功并开始代理的信息
    printf("server connected, proxying...");

    // 开始从客户端连接读取数据，并在读取到数据后调用 on_client_read 回调函数
    uv_read_start((uv_stream_t *)ctx->client_conn, alloc_client_buf, on_client_read);
    // 开始从服务器连接读取数据，并在读取到数据后调用 on_server_read 回调函数
    uv_read_start((uv_stream_t *)ctx->server_conn, alloc_server_buf, on_server_read);
}

// 有新的客户端连接时的回调函数
void on_client_conn(uv_stream_t *server, int status) {
    // 如果连接出现错误，输出错误信息并返回
    if (status < 0) {
        fprintf(stderr, "New client connection error %s\n", uv_strerror(status));
        return;
    }

    // 为客户端连接分配内存
    uv_tcp_t *client = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
    // 初始化客户端连接的 TCP 句柄
    uv_tcp_init(loop, client);

    // 为连接上下文结构体分配内存
    conn_ctx *ctx = (conn_ctx *)malloc(sizeof(conn_ctx));
    // 将客户端连接句柄赋值给上下文结构体
    ctx->client_conn = client;
    // 为客户端写入请求分配内存
    ctx->client_wreq = (uv_write_t *)malloc(sizeof(uv_write_t));

    // 如果成功接受客户端连接
    if (uv_accept(server, (uv_stream_t *)client) == 0) {
        // 输出获取到新客户端连接的信息
        printf("got new client connection...");

        // 为服务器连接分配内存
        uv_tcp_t *server = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
        // 初始化服务器连接的 TCP 句柄
        uv_tcp_init(loop, server);

        // 为连接请求分配内存
        uv_connect_t *server_req = (uv_connect_t *)malloc(sizeof(uv_connect_t));
        // 将连接上下文结构体指针赋值给连接请求的 data 字段
        server_req->data = ctx;

        // 如果成功发起与服务器的连接
        if (uv_tcp_connect(server_req, server, (const struct sockaddr*)&server_addr, on_server_conn) == 0) {
            // 输出正在连接服务器的信息
            printf("connecting to server...");
        }
    }
}

// 程序入口函数
int main() {
    // 获取默认的事件循环
    loop = uv_default_loop();

    // 定义服务器监听的 TCP 句柄
    uv_tcp_t server;
    // 初始化服务器监听的 TCP 句柄
    uv_tcp_init(loop, &server);

    // 将 IP 地址 "0.0.0.0" 和端口 6380 转换为 sockaddr_in 结构体
    uv_ip4_addr("0.0.0.0", 6380, &listen_addr);
    // 将 IP 地址 "127.0.0.1" 和端口 6379 转换为 sockaddr_in 结构体
    uv_ip4_addr("127.0.0.1", 6379, &server_addr);

    // 将服务器监听的 TCP 句柄绑定到指定的监听地址
    uv_tcp_bind(&server, (const struct sockaddr*)&listen_addr, 0);
    // 开始监听客户端连接，最大连接队列长度为 128，有新连接时调用 on_client_conn 回调函数
    int r = uv_listen((uv_stream_t*)&server, 128, on_client_conn);
    // 如果监听失败，输出错误信息并返回 1
    if (r) {
        fprintf(stderr, "Listen error %s\n", uv_strerror(r));
        return 1;
    }
    // 运行事件循环，直到没有活动的句柄或请求
    return uv_run(loop, UV_RUN_DEFAULT);
}