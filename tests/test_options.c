/* options.c: the command line. Every flag and its default, every way a command
 * line is wrong — and that a wrong one leaves Options as it was and says on
 * stderr which flag is at fault. Built with ENABLE_MULTIPLAYER when the game
 * is: without it the network flags are refused, and that is tested instead. */

#include "unity.h"
#include "options.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

#define ARGS(...) ((char *[]){"doom-clone", __VA_ARGS__, NULL})

/* stderr goes to this file, in the test's working directory, so that what
 * parse_options() says can be read back. Unity reports on stdout. */
#define STDERR_FILE "test_options_stderr.txt"

/* What a refused command line must leave behind: none of it is a default. */
static const Options untouched = {LAUNCH_CLIENT, "untouched", 4242, "untouched", 42};

void setUp(void)
{}

void tearDown(void)
{}

static int count(char **args)
{
    int n = 0;
    while (args[n]) {
        n++;
    }
    return n;
}

/* The command line after the program name, for a failure to name the case. */
static const char *joined(char **args)
{
    static char line[256];
    line[0] = '\0';
    for (int i = 1; args[i]; i++) {
        strncat(line, i > 1 ? " " : "", sizeof(line) - strlen(line) - 1);
        strncat(line, args[i], sizeof(line) - strlen(line) - 1);
    }
    return line;
}

static Options accepts(char **args)
{
    Options o = untouched;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, parse_options(count(args), args, &o), joined(args));
    return o;
}

/* -1, Options as it was, and a message on stderr that has `says` in it. */
static void refuses(const char *says, char **args)
{
    const char *line = joined(args);
    Options o = untouched;
    TEST_ASSERT_NOT_NULL(freopen(STDERR_FILE, "w+", stderr));
    int r = parse_options(count(args), args, &o);
    char said[512];
    fflush(stderr);
    rewind(stderr);
    said[fread(said, 1, sizeof(said) - 1, stderr)] = '\0';

    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, r, line);
    TEST_ASSERT_EQUAL_INT_MESSAGE(untouched.mode, o.mode, line);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(untouched.connect, o.connect, line);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(untouched.port, o.port, line);
    TEST_ASSERT_EQUAL_PTR_MESSAGE(untouched.name, o.name, line);
    TEST_ASSERT_EQUAL_INT_MESSAGE(untouched.snapshot_rate, o.snapshot_rate, line);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(said, says), line);
}

#define REFUSES(says, ...) refuses(says, ARGS(__VA_ARGS__))

/* ---- what every build takes ---- */

static void test_no_flags_is_the_game_alone_with_every_default(void)
{
    Options o = accepts((char *[]){"doom-clone", NULL});
    TEST_ASSERT_EQUAL_INT(LAUNCH_SOLO, o.mode);
    TEST_ASSERT_NULL(o.connect);
    TEST_ASSERT_EQUAL_UINT16(NET_PORT_DEFAULT, o.port);
    TEST_ASSERT_EQUAL_STRING("", o.name);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_RATE, o.snapshot_rate);
}

static void test_help_is_asked_for_and_leaves_options_alone(void)
{
    char **args[] = {ARGS("-h"), ARGS("--help"), ARGS("--host", "--help"),
                     ARGS("--help", "--bogus")};
    for (size_t i = 0; i < sizeof(args) / sizeof(args[0]); i++) {
        Options o = untouched;
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, parse_options(count(args[i]), args[i], &o),
                                      joined(args[i]));
        TEST_ASSERT_EQUAL_UINT16(untouched.port, o.port);
    }
}

static void test_usage_names_the_program_and_every_flag(void)
{
    static const char *const names[] = {"usage: doom-clone ", "--host", "--dedicated",
                                        "--connect",          "--port", "--name",
                                        "--snaprate",         "--help"};
    FILE *f = fopen("test_options_usage.txt", "w+");
    TEST_ASSERT_NOT_NULL(f);
    options_usage(f, "doom-clone");
    char text[2048];
    rewind(f);
    text[fread(text, 1, sizeof(text) - 1, f)] = '\0';
    fclose(f);
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(text, names[i]), names[i]);
    }
}

static void test_unknown_argument_is_refused(void)
{
    REFUSES("'--hots'", "--hots");
    REFUSES("'-x'", "-x");
    REFUSES("'level1.txt'", "level1.txt");
    REFUSES("'--port=27015'", "--port=27015");
}

static void test_flag_without_its_value_is_refused(void)
{
    REFUSES("--connect needs a value", "--connect");
    REFUSES("--port needs a value", "--host", "--port");
    REFUSES("--name needs a value", "--connect", "a", "--name");
    REFUSES("--snaprate needs a value", "--host", "--snaprate");
}

static void test_dedicated_without_host_is_refused(void)
{
    REFUSES("--dedicated needs --host", "--dedicated");
}

static void test_port_without_a_network_game_is_refused(void)
{
    REFUSES("--port needs --host or --connect", "--port", "27016");
}

static void test_snaprate_without_host_is_refused(void)
{
    REFUSES("--snaprate needs --host", "--snaprate", "30");
}

static void test_name_without_connect_is_refused(void)
{
    REFUSES("--name needs --connect", "--name", "doomguy");
}

#ifdef ENABLE_MULTIPLAYER

/* ---- the network game ---- */

static void test_host_serves_and_plays(void)
{
    Options o = accepts(ARGS("--host"));
    TEST_ASSERT_EQUAL_INT(LAUNCH_HOST, o.mode);
    TEST_ASSERT_NULL(o.connect);
    TEST_ASSERT_EQUAL_UINT16(NET_PORT_DEFAULT, o.port);
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_RATE, o.snapshot_rate);
}

static void test_dedicated_host_serves_alone_whichever_comes_first(void)
{
    TEST_ASSERT_EQUAL_INT(LAUNCH_DEDICATED, accepts(ARGS("--host", "--dedicated")).mode);
    TEST_ASSERT_EQUAL_INT(LAUNCH_DEDICATED, accepts(ARGS("--dedicated", "--host")).mode);
}

static void test_connect_joins_the_address_given(void)
{
    char **args = ARGS("--connect", "10.0.0.2");
    Options o = accepts(args);
    TEST_ASSERT_EQUAL_INT(LAUNCH_CLIENT, o.mode);
    TEST_ASSERT_EQUAL_PTR(args[2], o.connect);
    TEST_ASSERT_EQUAL_UINT16(NET_PORT_DEFAULT, o.port);
    TEST_ASSERT_EQUAL_STRING("", o.name);
}

static void test_port_is_taken_from_one_to_65535(void)
{
    TEST_ASSERT_EQUAL_UINT16(1, accepts(ARGS("--host", "--port", "1")).port);
    TEST_ASSERT_EQUAL_UINT16(65535, accepts(ARGS("--connect", "a", "--port", "65535")).port);
    TEST_ASSERT_EQUAL_UINT16(27016, accepts(ARGS("--port", "27016", "--host", "--dedicated")).port);
}

static void test_port_out_of_range_or_not_a_number_is_refused(void)
{
    const char *bad[] = {"0",
                         "65536",
                         "-1",
                         "+80",
                         " 80",
                         "80 ",
                         "abc",
                         "12x",
                         "",
                         "0x50",
                         "99999999999999999999"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        refuses("--port", ARGS("--host", "--port", (char *)bad[i]));
    }
}

static void test_name_goes_with_connect_up_to_what_join_holds(void)
{
    char **args = ARGS("--connect", "a", "--name", "doomguy");
    TEST_ASSERT_EQUAL_PTR(args[4], accepts(args).name);
    TEST_ASSERT_EQUAL_STRING("fifteen_letters",
                             accepts(ARGS("--connect", "a", "--name", "fifteen_letters")).name);
}

static void test_name_empty_or_too_long_is_refused(void)
{
    REFUSES("--name", "--connect", "a", "--name", "");
    REFUSES("--name", "--connect", "a", "--name", "sixteen_letters_");
}

static void test_name_on_a_server_is_refused(void)
{
    REFUSES("--name needs --connect", "--host", "--name", "doomguy");
    REFUSES("--name needs --connect", "--host", "--dedicated", "--name", "doomguy");
}

static void test_snaprate_is_taken_from_min_to_tick_rate(void)
{
    TEST_ASSERT_EQUAL_INT(SNAPSHOT_RATE_MIN,
                          accepts(ARGS("--host", "--snaprate", "15")).snapshot_rate);
    TEST_ASSERT_EQUAL_INT(TICK_RATE,
                          accepts(ARGS("--host", "--dedicated", "--snaprate", "60")).snapshot_rate);
}

static void test_snaprate_out_of_range_or_not_a_number_is_refused(void)
{
    const char *bad[] = {"14", "61", "0", "-20", "x", "", "20.5"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        refuses("--snaprate", ARGS("--host", "--snaprate", (char *)bad[i]));
    }
}

static void test_snaprate_on_a_client_is_refused(void)
{
    REFUSES("--snaprate needs --host", "--connect", "a", "--snaprate", "30");
}

static void test_host_and_connect_together_are_refused(void)
{
    REFUSES("--host and --connect", "--host", "--connect", "a");
    REFUSES("--host and --connect", "--connect", "a", "--host", "--dedicated");
}

static void test_empty_address_is_refused(void)
{
    REFUSES("--connect", "--connect", "");
}

#else

static void test_network_flags_are_refused_without_the_network_game(void)
{
    REFUSES("ENABLE_MULTIPLAYER=OFF", "--host");
    REFUSES("ENABLE_MULTIPLAYER=OFF", "--host", "--dedicated");
    REFUSES("ENABLE_MULTIPLAYER=OFF", "--connect", "a");
}

#endif

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_flags_is_the_game_alone_with_every_default);
    RUN_TEST(test_help_is_asked_for_and_leaves_options_alone);
    RUN_TEST(test_usage_names_the_program_and_every_flag);
    RUN_TEST(test_unknown_argument_is_refused);
    RUN_TEST(test_flag_without_its_value_is_refused);
    RUN_TEST(test_dedicated_without_host_is_refused);
    RUN_TEST(test_port_without_a_network_game_is_refused);
    RUN_TEST(test_snaprate_without_host_is_refused);
    RUN_TEST(test_name_without_connect_is_refused);
#ifdef ENABLE_MULTIPLAYER
    RUN_TEST(test_host_serves_and_plays);
    RUN_TEST(test_dedicated_host_serves_alone_whichever_comes_first);
    RUN_TEST(test_connect_joins_the_address_given);
    RUN_TEST(test_port_is_taken_from_one_to_65535);
    RUN_TEST(test_port_out_of_range_or_not_a_number_is_refused);
    RUN_TEST(test_name_goes_with_connect_up_to_what_join_holds);
    RUN_TEST(test_name_empty_or_too_long_is_refused);
    RUN_TEST(test_name_on_a_server_is_refused);
    RUN_TEST(test_snaprate_is_taken_from_min_to_tick_rate);
    RUN_TEST(test_snaprate_out_of_range_or_not_a_number_is_refused);
    RUN_TEST(test_snaprate_on_a_client_is_refused);
    RUN_TEST(test_host_and_connect_together_are_refused);
    RUN_TEST(test_empty_address_is_refused);
#else
    RUN_TEST(test_network_flags_are_refused_without_the_network_game);
#endif
    return UNITY_END();
}
