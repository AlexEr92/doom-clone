/* ENet as the build links it: the library built from vendor/enet/ and its
 * headers must be one ENet and must work. Nothing from src/ is linked here. */

#include <enet/enet.h>

#include "unity.h"

void setUp(void)
{}

void tearDown(void)
{}

/* A system enet.h found ahead of vendor/enet/include would compile and link
 * fine against the vendored library and then disagree with it on struct
 * layouts at run time. */
static void test_linked_library_matches_the_headers(void)
{
    TEST_ASSERT_EQUAL_HEX32(ENET_VERSION, enet_linked_version());
}

/* On Windows this is WSAStartup() and timeBeginPeriod() — the ws2_32 and
 * winmm the build links explicitly. */
static void test_initialize_succeeds(void)
{
    TEST_ASSERT_EQUAL_INT(0, enet_initialize());
    enet_deinitialize();
}

static void test_host_binds_to_loopback(void)
{
    TEST_ASSERT_EQUAL_INT(0, enet_initialize());

    ENetAddress address;
    TEST_ASSERT_EQUAL_INT(0, enet_address_set_host(&address, "127.0.0.1"));
    address.port = ENET_PORT_ANY;
    ENetHost *host = enet_host_create(&address, 1, 2, 0, 0);
    TEST_ASSERT_NOT_NULL(host);

    enet_host_destroy(host);
    enet_deinitialize();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_linked_library_matches_the_headers);
    RUN_TEST(test_initialize_succeeds);
    RUN_TEST(test_host_binds_to_loopback);
    return UNITY_END();
}
