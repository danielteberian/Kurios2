/* test_socket.c - Socket and TCP Tests */

#include "test_framework.h"
#include "../net/socket.h"
#include "../net/tcp.h"
#include "../net/ip.h"

TEST_CASE(socket_create_udp) {
    socket_t *sock = socket_create(SOCK_DGRAM);
    TEST_ASSERT_NOT_NULL(sock);
    TEST_ASSERT_EQ(sock->type, SOCK_DGRAM);
    TEST_ASSERT_FALSE(sock->bound);
    TEST_ASSERT_FALSE(sock->connected);
    socket_destroy(sock);
}

TEST_CASE(socket_create_tcp) {
    socket_t *sock = socket_create(SOCK_STREAM);
    TEST_ASSERT_NOT_NULL(sock);
    TEST_ASSERT_EQ(sock->type, SOCK_STREAM);
    TEST_ASSERT_NOT_NULL(sock->tcp_conn);
    TEST_ASSERT_FALSE(sock->bound);
    TEST_ASSERT_FALSE(sock->connected);
    TEST_ASSERT_FALSE(sock->listening);
    socket_destroy(sock);
}

TEST_CASE(socket_bind) {
    socket_t *sock = socket_create(SOCK_DGRAM);
    TEST_ASSERT_NOT_NULL(sock);

    /* Bind to loopback */
    int result = socket_bind(sock, IP_LOCALHOST, 8080);
    TEST_ASSERT_EQ(result, 0);
    TEST_ASSERT_TRUE(sock->bound);
    TEST_ASSERT_EQ(sock->local_port, 8080);

    socket_destroy(sock);
}

TEST_CASE(tcp_connection_create) {
    tcp_connection_t *conn = tcp_connection_create();
    TEST_ASSERT_NOT_NULL(conn);
    TEST_ASSERT_EQ(conn->state, TCP_CLOSED);
    TEST_ASSERT_NOT_NULL(conn->send_buf);
    TEST_ASSERT_NOT_NULL(conn->recv_buf);
    TEST_ASSERT_EQ(conn->send_buf_size, 64 * 1024);
    TEST_ASSERT_EQ(conn->recv_buf_size, 16 * 1024);
    tcp_connection_destroy(conn);
}

TEST_CASE(tcp_listen) {
    socket_t *sock = socket_create(SOCK_STREAM);
    TEST_ASSERT_NOT_NULL(sock);

    /* Bind first */
    socket_bind(sock, IP_LOCALHOST, 9000);

    /* Listen */
    int result = socket_listen(sock, 5);
    TEST_ASSERT_EQ(result, 0);
    TEST_ASSERT_TRUE(sock->listening);
    TEST_ASSERT_EQ(sock->tcp_conn->state, TCP_LISTEN);
    TEST_ASSERT_EQ(sock->tcp_conn->backlog, 5);

    socket_destroy(sock);
}

TEST_SUITE(socket) {
    RUN_TEST(socket_create_udp);
    RUN_TEST(socket_create_tcp);
    RUN_TEST(socket_bind);
    RUN_TEST(tcp_connection_create);
    RUN_TEST(tcp_listen);
}
