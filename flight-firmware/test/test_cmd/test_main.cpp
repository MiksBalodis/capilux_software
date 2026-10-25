#include <unity.h>
#include <stddef.h>
#include "cmd.h"
#include "flashrec.h"
#include "camparse.h"
#include "config.h"

void setUp() {}
void tearDown() {}

void test_valid() {
  TEST_ASSERT_EQUAL(C_PING, parseCmd("PING").id);
  TEST_ASSERT_EQUAL(C_CLEAR, parseCmd("CLEAR_MEMORY").id);
  TEST_ASSERT_EQUAL(C_CLEAR_OK, parseCmd("CLEAR_MEMORY CONFIRM").id);
  Parsed p = parseCmd("LED 3 200");
  TEST_ASSERT_EQUAL(C_LED, p.id);
  TEST_ASSERT_EQUAL(3, p.a);
  TEST_ASSERT_EQUAL(200, p.b);
  TEST_ASSERT_EQUAL(128, parseCmd("LED_ALL 128").a);
  TEST_ASSERT_EQUAL(C_LED_OFF, parseCmd("LED_OFF").id);
  TEST_ASSERT_EQUAL(7, parseCmd("TV 7").a);
  TEST_ASSERT_EQUAL(8, parseCmd("TV 8").a);  // automatic cycling
  TEST_ASSERT_EQUAL(C_REARM, parseCmd("REARM").id);
  TEST_ASSERT_EQUAL(C_SELF_TEST, parseCmd("SELF_TEST").id);
}

void test_rejects_corrupt() {
  const char *bad[] = {"", "PIN", "PINGG", "PING ", "ping", "CLEAR_MEMORY CONFIRMX", "CLEAR_MEMORY X",
                       "LED 5 10", "LED 1", "LED 1 256", "LED_ALL -1", "LED_ALL", "TV 9", "TV 1x", "REBOOT now", "REARM now", "REAR", "SELF_TEST 1", "SELF"};
  for (auto s : bad) TEST_ASSERT_EQUAL_MESSAGE(C_BAD, parseCmd(s).id, s);
}

// flash record layout must match tools/decode_flash.py ("<IHBB4ff3f3f4hhH")
void test_flash_record() {
  TEST_ASSERT_EQUAL(0, offsetof(FlashRec, tSoeMs));
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16((const uint8_t *)"123456789", 9));
  TEST_ASSERT_EQUAL(8, offsetof(FlashRec, chP));
  TEST_ASSERT_EQUAL(28, offsetof(FlashRec, acc));
  TEST_ASSERT_EQUAL(52, offsetof(FlashRec, chT));
  TEST_ASSERT_EQUAL(62, offsetof(FlashRec, crc));
}

// GET_SETTINGS root reply as seen on the bench (Split 4 V2)
void test_cam_storage() {
  const char raw[] = "\x00" "CHARSET\0BF\0" "\x01" "COLUMNS\0" "30\0" "\x03" "SDCARD_CAPACITY\0" "120874/122067\0"
                     "\x04" "REMAINING_TIME\0" "21756\0" "\x05" "RESOLUTION\0" "1080P60FPS\0";
  int32_t sec, fr, tot;
  TEST_ASSERT_TRUE(parseCamStorage((const uint8_t *)raw, sizeof raw - 1, sec, fr, tot));
  TEST_ASSERT_EQUAL(21756, sec);
  TEST_ASSERT_EQUAL(120874, fr);
  TEST_ASSERT_EQUAL(122067, tot);
  TEST_ASSERT_FALSE(parseCamStorage((const uint8_t *)raw, 30, sec, fr, tot));  // truncated reply
}

// The whole stop sequence must be over STOP_MARGIN_S before RXSM power-off, whatever the config.
void test_timeline() {
  int64_t offFromSoe = (int64_t)(T_POWER_OFF_S - T_SOE_S) * 1000;             // power-off, ms after SOE
  int64_t allOff = (int64_t)T_EXP_END_MS + CAM_STOP_MS;                       // cameras unpowered
  TEST_ASSERT_EQUAL_INT64(offFromSoe - STOP_MARGIN_S * 1000, allOff);
  TEST_ASSERT_TRUE(allOff < offFromSoe);
  TEST_ASSERT_EQUAL_INT64((int64_t)(T_POWER_OFF_S - STOP_MARGIN_S) * 1000, (int64_t)T_EXP_END_FROM_LO_MS + CAM_STOP_MS);
  if (T_SOE_S == -45 && T_POWER_OFF_S == 600 && STOP_MARGIN_S == 20 && CAM_STOP_MS == 3000)
    TEST_ASSERT_EQUAL_UINT32(622000, T_EXP_END_MS);  // SOE+622 s = T+577 s stop, T+580 s all off
  if (T_SOE_S == -45 && T_POWER_OFF_S == 300 && STOP_MARGIN_S == 20 && CAM_STOP_MS == 3000)
    TEST_ASSERT_EQUAL_UINT32(322000, T_EXP_END_MS);  // SOE+322 s = T+277 s stop, T+280 s all off
  TEST_ASSERT_TRUE(HK_FILE_BYTES >= 4u * 3600u * 300u);   // >= 4 h of HK
  TEST_ASSERT_TRUE(IMU_FILE_BYTES >= 4u * 3600u * 5700u); // >= 4 h of IMU at the measured worst rate
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valid);
  RUN_TEST(test_rejects_corrupt);
  RUN_TEST(test_flash_record);
  RUN_TEST(test_cam_storage);
  RUN_TEST(test_timeline);
  return UNITY_END();
}
