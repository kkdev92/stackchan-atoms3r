#include <unity.h>

#include <set>
#include <string>

#include "stackchan/domain/ids.hpp"

using stackchan::domain::BootId;
using stackchan::domain::Id;
using stackchan::domain::IdGenerator;

namespace {

void test_unset_boot_id_reports_itself_as_unset() {
  const BootId id = BootId::unset();
  TEST_ASSERT_FALSE(id.is_set());
  // Still a valid length as a string, so logging it cannot break anything.
  TEST_ASSERT_EQUAL_UINT32(BootId::kTextLength, id.text().size());
}

void test_boot_id_is_twenty_six_characters() {
  const BootId id = BootId::from_entropy(0x0123456789ABCDEFull, 0xFEDCBA9876543210ull);
  TEST_ASSERT_TRUE(id.is_set());
  TEST_ASSERT_EQUAL_UINT32(26, id.text().size());
}

void test_boot_id_uses_only_the_readable_alphabet() {
  // Crockford base32, which leaves out the characters that are easily
  // confused when read aloud or retyped from a log.
  const std::string allowed = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
  const std::uint64_t samples[] = {0, 1, 0xFFFFFFFFFFFFFFFFull, 0x5A5A5A5A5A5A5A5Aull};
  for (const std::uint64_t high : samples) {
    for (const std::uint64_t low : samples) {
      const BootId id = BootId::from_entropy(high, low);
      for (const char c : id.text()) {
        TEST_ASSERT_TRUE_MESSAGE(allowed.find(c) != std::string::npos,
                                 "an easily misread character got in");
      }
    }
  }
}

void test_different_entropy_yields_different_boot_ids() {
  // Always different after a restart. This is what stops two recordings
  // from sharing an identifier.
  const BootId a = BootId::from_entropy(1, 2);
  const BootId b = BootId::from_entropy(1, 3);
  const BootId c = BootId::from_entropy(2, 2);

  TEST_ASSERT_TRUE(a != b);
  TEST_ASSERT_TRUE(a != c);
  TEST_ASSERT_TRUE(b != c);
  TEST_ASSERT_FALSE(a.text() == b.text());
  TEST_ASSERT_FALSE(a.text() == c.text());
}

void test_the_same_entropy_yields_the_same_boot_id() {
  const BootId a = BootId::from_entropy(0xDEADBEEFull, 0xCAFEBABEull);
  const BootId b = BootId::from_entropy(0xDEADBEEFull, 0xCAFEBABEull);
  TEST_ASSERT_TRUE(a == b);
  TEST_ASSERT_TRUE(a.text() == b.text());
}

void test_the_high_word_reaches_the_text() {
  // All 128 bits are used. Taking only the low bits would map values that
  // differ higher up onto the same string.
  const BootId a = BootId::from_entropy(0, 0);
  const BootId b = BootId::from_entropy(1, 0);
  TEST_ASSERT_FALSE(a.text() == b.text());
}

void test_all_zero_entropy_encodes_to_all_zeroes() {
  // The boundary case: what appears when no randomness was available is
  // defined rather than arbitrary.
  const BootId id = BootId::from_entropy(0, 0);
  TEST_ASSERT_EQUAL_STRING("00000000000000000000000000", std::string{id.text()}.c_str());
  // But is_set() is false, so the absence is detectable.
  TEST_ASSERT_FALSE(id.is_set());
}

void test_generated_ids_start_at_one() {
  // Zero is deliberately not used, so it stays distinguishable from unset.
  IdGenerator gen{BootId::from_entropy(1, 1)};
  TEST_ASSERT_EQUAL_UINT32(0, gen.issued());
  const Id first = gen.next();
  TEST_ASSERT_EQUAL_UINT64(1, first.sequence());
  TEST_ASSERT_EQUAL_UINT32(1, gen.issued());
}

void test_the_sequence_only_goes_up() {
  // Increases indefinitely, rather than wrapping back to a value already
  // seen.
  IdGenerator gen{BootId::from_entropy(1, 1)};
  std::uint64_t previous = 0;
  for (int i = 0; i < 1000; ++i) {
    const Id id = gen.next();
    TEST_ASSERT_TRUE(id.sequence() > previous);
    previous = id.sequence();
  }
}

void test_ids_are_unique_within_a_boot() {
  IdGenerator gen{BootId::from_entropy(0xAAAA, 0xBBBB)};
  std::set<std::string> seen;
  for (int i = 0; i < 500; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(seen.insert(std::string{gen.next().text()}).second,
                             "the same id came out twice");
  }
}

void test_ids_carry_the_boot_id_so_the_origin_is_visible() {
  // Which boot it came from is visible at a glance, which matters when
  // following a log.
  const BootId boot = BootId::from_entropy(0x1234, 0x5678);
  IdGenerator gen{boot};
  const Id id = gen.next();

  const std::string text{id.text()};
  const std::string boot_text{boot.text()};
  TEST_ASSERT_EQUAL_UINT32(0, text.find(boot_text));
  TEST_ASSERT_EQUAL_CHAR('-', text[boot_text.size()]);
  TEST_ASSERT_EQUAL_STRING("1", text.substr(boot_text.size() + 1).c_str());
}

void test_ids_from_different_boots_never_collide() {
  // The same sequence number in a different boot is a different id.
  IdGenerator first_boot{BootId::from_entropy(1, 1)};
  IdGenerator second_boot{BootId::from_entropy(1, 2)};

  const Id a = first_boot.next();
  const Id b = second_boot.next();
  TEST_ASSERT_EQUAL_UINT64(a.sequence(), b.sequence());  // the same sequence
  TEST_ASSERT_FALSE(a.text() == b.text());               // and still distinct
}

void test_large_sequence_numbers_are_formatted_correctly() {
  // Still correct as the number grows: sequences get long on a device that
  // runs for a long time.
  IdGenerator gen{BootId::from_entropy(1, 1)};
  for (int i = 0; i < 12345; ++i) {
    (void)gen.next();
  }
  const Id id = gen.next();
  TEST_ASSERT_EQUAL_UINT64(12346, id.sequence());

  const std::string text{id.text()};
  TEST_ASSERT_EQUAL_STRING("12346", text.substr(BootId::kTextLength + 1).c_str());
  TEST_ASSERT_TRUE(text.size() <= Id::kMaxTextLength);
}

void test_the_text_never_exceeds_the_declared_maximum() {
  // The caller can size a fixed buffer for it.
  IdGenerator gen{BootId::from_entropy(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull)};
  const Id id = gen.next();
  TEST_ASSERT_TRUE(id.text().size() <= Id::kMaxTextLength);
  TEST_ASSERT_EQUAL_UINT32(BootId::kTextLength + 2, id.text().size());  // "...-1"
}

void test_ids_compare_by_their_text() {
  IdGenerator gen{BootId::from_entropy(1, 1)};
  const Id a = gen.next();
  const Id b = gen.next();
  TEST_ASSERT_FALSE(a == b);

  const Id copy = a;
  TEST_ASSERT_TRUE(a == copy);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_unset_boot_id_reports_itself_as_unset);
  RUN_TEST(test_boot_id_is_twenty_six_characters);
  RUN_TEST(test_boot_id_uses_only_the_readable_alphabet);
  RUN_TEST(test_different_entropy_yields_different_boot_ids);
  RUN_TEST(test_the_same_entropy_yields_the_same_boot_id);
  RUN_TEST(test_the_high_word_reaches_the_text);
  RUN_TEST(test_all_zero_entropy_encodes_to_all_zeroes);
  RUN_TEST(test_generated_ids_start_at_one);
  RUN_TEST(test_the_sequence_only_goes_up);
  RUN_TEST(test_ids_are_unique_within_a_boot);
  RUN_TEST(test_ids_carry_the_boot_id_so_the_origin_is_visible);
  RUN_TEST(test_ids_from_different_boots_never_collide);
  RUN_TEST(test_large_sequence_numbers_are_formatted_correctly);
  RUN_TEST(test_the_text_never_exceeds_the_declared_maximum);
  RUN_TEST(test_ids_compare_by_their_text);
  return UNITY_END();
}
