// Flash ring buffer on a fake NOR flash: it must never refuse to write and must
// find its own end again after any reset.
#include <unity.h>
#include <string.h>
#include "flashring.h"

static uint8_t mem[FLASH_LOG_SIZE];
static int erases;
const uint8_t *flashAt(uint32_t ofs) { return mem + ofs - FLASH_LOG_OFS; }
void flashHwErase(uint32_t ofs) { memset(mem + ofs - FLASH_LOG_OFS, 0xFF, SECTOR); erases++; }
void flashHwProgram(uint32_t ofs, const uint8_t *d) {  // NOR: programming only clears bits
  for (uint32_t i = 0; i < PAGE; i++) mem[ofs - FLASH_LOG_OFS + i] &= d[i];
}

void setUp() { erases = 0; }
void tearDown() {}

static uint8_t pg[PAGE];
static uint32_t writeN(uint32_t wr, int n, uint8_t tag) {
  for (int i = 0; i < n; i++) {
    memset(pg, tag, PAGE);
    uint32_t at = wr;
    wr = ringWrite(wr, pg);
    uint32_t written = wr == FLASH_LOG_OFS ? FLASH_END - PAGE : wr - PAGE;
    TEST_ASSERT_EQUAL_UINT8(tag, *flashAt(written));      // data really landed
    TEST_ASSERT_EQUAL_UINT32(wr, ringFindEnd());           // a reset here resumes right here
    (void)at;
  }
  return wr;
}

void test_blank_flash() {
  memset(mem, 0xFF, sizeof mem);
  TEST_ASSERT_EQUAL_UINT32(FLASH_LOG_OFS, ringFindEnd());
  writeN(FLASH_LOG_OFS, 100, 0x11);
}

void test_new_board_full_of_junk() {
  memset(mem, 0x00, sizeof mem);
  uint32_t wr = ringFindEnd();
  TEST_ASSERT_EQUAL_UINT32(FLASH_LOG_OFS, wr);
  writeN(wr, 50, 0x22);
}

void test_wraps_forever() {  // 3x the ring size: never blocks, keeps finding its end
  memset(mem, 0xFF, sizeof mem);
  writeN(FLASH_LOG_OFS, 3 * FLASH_LOG_SIZE / PAGE, 0x33);
}

void test_reset_mid_clear_erase() {  // old data, CLEAR erased only the first 10 sectors, then reset
  memset(mem, 0x44, sizeof mem);
  memset(mem, 0xFF, 10 * SECTOR);
  uint32_t wr = ringFindEnd();
  writeN(wr, 2 * FLASH_LOG_SIZE / PAGE, 0x55);
}

void test_resume_after_previous_run() {  // earlier boot left data, ring continues after it
  memset(mem, 0xFF, sizeof mem);
  uint32_t wr = writeN(FLASH_LOG_OFS, 37, 0x66);
  TEST_ASSERT_EQUAL_UINT32(wr, ringFindEnd());
  writeN(ringFindEnd(), 37, 0x77);
  TEST_ASSERT_EQUAL_UINT8(0x66, *flashAt(FLASH_LOG_OFS));  // first run still there
}

// ringLastPage finds the newest page from the write position, across wrap and partial erases.
void test_last_page() {
  memset(mem, 0xFF, sizeof mem);
  TEST_ASSERT_EQUAL_UINT32(0, ringLastPage(ringFindEnd()));            // empty ring
  uint32_t wr = writeN(FLASH_LOG_OFS, 20, 0x10);
  memset(pg, 0x99, PAGE); wr = ringWrite(wr, pg);                      // newest page
  uint32_t lp = ringLastPage(ringFindEnd());
  TEST_ASSERT_EQUAL_UINT8(0x99, *flashAt(lp));
  TEST_ASSERT_EQUAL_UINT8(0x10, *flashAt(ringLastPage(lp)));            // and the one before it
  wr = writeN(wr, FLASH_LOG_SIZE / PAGE + 7, 0x20);                     // wrap past the start
  memset(pg, 0x98, PAGE); ringWrite(wr, pg);
  TEST_ASSERT_EQUAL_UINT8(0x98, *flashAt(ringLastPage(ringFindEnd())));
}

void test_last_page_at_sector_end() {  // data ends exactly on a sector boundary
  memset(mem, 0xFF, sizeof mem);
  uint32_t wr = writeN(FLASH_LOG_OFS, SECTOR / PAGE - 1, 0x30);
  memset(pg, 0x97, PAGE); ringWrite(wr, pg);
  TEST_ASSERT_EQUAL_UINT8(0x97, *flashAt(ringLastPage(ringFindEnd())));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_blank_flash);
  RUN_TEST(test_new_board_full_of_junk);
  RUN_TEST(test_wraps_forever);
  RUN_TEST(test_reset_mid_clear_erase);
  RUN_TEST(test_resume_after_previous_run);
  RUN_TEST(test_last_page);
  RUN_TEST(test_last_page_at_sector_end);
  return UNITY_END();
}
