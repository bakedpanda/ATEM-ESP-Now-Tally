#include <unity.h>
#include "tally_packet.h"

void setUp(void) {}
void tearDown(void) {}

void test_set_get_state_unit1_program() {
    TallyPacket pkt = {};
    tallyPacketSetState(&pkt, 1, TALLY_PROGRAM);
    TEST_ASSERT_EQUAL(TALLY_PROGRAM, tallyPacketGetState(&pkt, 1));
}

void test_set_get_state_unit8_preview() {
    TallyPacket pkt = {};
    tallyPacketSetState(&pkt, 8, TALLY_PREVIEW);
    TEST_ASSERT_EQUAL(TALLY_PREVIEW, tallyPacketGetState(&pkt, 8));
}

void test_set_get_state_unit20_standby() {
    TallyPacket pkt = {};
    tallyPacketSetState(&pkt, 20, TALLY_STANDBY);
    TEST_ASSERT_EQUAL(TALLY_STANDBY, tallyPacketGetState(&pkt, 20));
}

void test_multiple_units_independent() {
    TallyPacket pkt = {};
    tallyPacketSetState(&pkt, 1, TALLY_PROGRAM);
    tallyPacketSetState(&pkt, 2, TALLY_PREVIEW);
    tallyPacketSetState(&pkt, 3, TALLY_STANDBY);
    tallyPacketSetState(&pkt, 20, TALLY_PROGRAM);
    TEST_ASSERT_EQUAL(TALLY_PROGRAM,  tallyPacketGetState(&pkt, 1));
    TEST_ASSERT_EQUAL(TALLY_PREVIEW,  tallyPacketGetState(&pkt, 2));
    TEST_ASSERT_EQUAL(TALLY_STANDBY,  tallyPacketGetState(&pkt, 3));
    TEST_ASSERT_EQUAL(TALLY_PROGRAM,  tallyPacketGetState(&pkt, 20));
}

void test_overwrite_state() {
    TallyPacket pkt = {};
    tallyPacketSetState(&pkt, 5, TALLY_PROGRAM);
    tallyPacketSetState(&pkt, 5, TALLY_PREVIEW);
    TEST_ASSERT_EQUAL(TALLY_PREVIEW, tallyPacketGetState(&pkt, 5));
}

void test_packet_size_is_3_bytes() {
    TEST_ASSERT_EQUAL(3, sizeof(TallyPacket));
}

void test_heartbeat_size_is_2_bytes() {
    TEST_ASSERT_EQUAL(2, sizeof(HeartbeatPacket));
}

void test_flags_atem_connected() {
    TallyPacket pkt = {};
    pkt.flags = 0x01;
    TEST_ASSERT_EQUAL(1, pkt.flags & 0x01);
    pkt.flags = 0x00;
    TEST_ASSERT_EQUAL(0, pkt.flags & 0x01);
}

void test_identify_packet_size_is_1_byte() {
    TEST_ASSERT_EQUAL(1, sizeof(IdentifyPacket));
}

void test_identify_packet_type_field() {
    IdentifyPacket pkt = { 0x03 };
    TEST_ASSERT_EQUAL(0x03, pkt.type);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_set_get_state_unit1_program);
    RUN_TEST(test_set_get_state_unit8_preview);
    RUN_TEST(test_set_get_state_unit20_standby);
    RUN_TEST(test_multiple_units_independent);
    RUN_TEST(test_overwrite_state);
    RUN_TEST(test_packet_size_is_3_bytes);
    RUN_TEST(test_heartbeat_size_is_2_bytes);
    RUN_TEST(test_flags_atem_connected);
    RUN_TEST(test_identify_packet_size_is_1_byte);
    RUN_TEST(test_identify_packet_type_field);
    return UNITY_END();
}
