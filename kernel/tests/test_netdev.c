/* test_netdev.c - Network Device Tests */

#include "test_framework.h"
#include "../net/netdev.h"
#include "../net/ip.h"

TEST_CASE(loopback_exists) {
    /* Loopback device should be registered */
    netdev_t *lo = netdev_get_by_ip(IP_LOCALHOST);
    TEST_ASSERT_NOT_NULL(lo);
    TEST_ASSERT_EQ(lo->ip, IP_LOCALHOST);
    TEST_ASSERT(lo->flags & NETDEV_LOOPBACK);
}

TEST_CASE(loopback_properties) {
    netdev_t *lo = netdev_get_by_ip(IP_LOCALHOST);
    TEST_ASSERT_NOT_NULL(lo);

    /* Check loopback is up and running */
    TEST_ASSERT(lo->flags & NETDEV_UP);
    TEST_ASSERT(lo->flags & NETDEV_RUNNING);

    /* Check MTU */
    TEST_ASSERT_IN_RANGE(lo->mtu, 1500, 65536);
}

TEST_CASE(netdev_count) {
    /* At minimum, loopback should be present */
    /* If virtio-net is detected, there will be 2+ devices */
    int count = 0;
    netdev_t *dev;

    /* Count loopback */
    dev = netdev_get_by_ip(IP_LOCALHOST);
    if (dev) count++;

    /* Note: We can't easily enumerate all netdevs without
     * modifying the netdev API, so this is a basic check */
    TEST_ASSERT(count >= 1);
}

TEST_CASE(packet_alloc_free) {
    packet_t *pkt = packet_alloc(1500);
    TEST_ASSERT_NOT_NULL(pkt);
    TEST_ASSERT_NOT_NULL(pkt->data);
    TEST_ASSERT_EQ(pkt->len, 0);  /* Should be 0 until filled */
    packet_free(pkt);
}

TEST_SUITE(netdev) {
    RUN_TEST(loopback_exists);
    RUN_TEST(loopback_properties);
    RUN_TEST(netdev_count);
    RUN_TEST(packet_alloc_free);
}
