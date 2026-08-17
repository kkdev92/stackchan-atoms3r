// ProvisioningScope: fixes the rule that only requests arriving on the
// access point's own interface may configure the device.
//
// This is what keeps credentials from being overwritten by anything else on
// the network. Changing any cell of this table is a change to the device's
// security.

#include <unity.h>

#include <optional>

#include "stackchan/domain/provisioning_scope.hpp"

using stackchan::domain::Ipv4;
using stackchan::domain::provisioning_allowed;

void setUp() {}
void tearDown() {}

namespace {

const Ipv4 kApAddress = Ipv4::of(192, 168, 4, 1);
const Ipv4 kStaAddress = Ipv4::of(192, 168, 1, 23);

}  // namespace

void test_request_via_access_point_is_allowed() {
  TEST_ASSERT_TRUE(provisioning_allowed(true, kApAddress, kApAddress));
}

// The heart of it: a request that arrived on the network interface is
// refused even while the access point is up.
void test_request_via_station_is_denied_even_while_ap_is_up() {
  TEST_ASSERT_FALSE(provisioning_allowed(true, kApAddress, kStaAddress));
}

void test_everything_is_denied_while_ap_is_down() {
  // A connection left over from before the access point closed is refused
  // too. A matching address is not sufficient.
  TEST_ASSERT_FALSE(provisioning_allowed(false, kApAddress, kApAddress));
  TEST_ASSERT_FALSE(provisioning_allowed(false, kApAddress, kStaAddress));
}

void test_unknown_origin_is_denied() {
  // The interface could not be determined — the lookup failed, or the
  // address was not one that maps to IPv4. When in doubt, refuse.
  TEST_ASSERT_FALSE(provisioning_allowed(true, kApAddress, std::nullopt));
}

void test_custom_ap_address_is_respected() {
  // Proof that no particular address is hard-coded: the rule remains "was
  // it the access point's interface" whatever that interface is numbered.
  const Ipv4 custom = Ipv4::of(10, 0, 0, 1);
  TEST_ASSERT_TRUE(provisioning_allowed(true, custom, custom));
  TEST_ASSERT_FALSE(provisioning_allowed(true, custom, kApAddress));
}

void test_ipv4_equality_is_octet_wise() {
  TEST_ASSERT_TRUE(Ipv4::of(1, 2, 3, 4) == Ipv4::of(1, 2, 3, 4));
  TEST_ASSERT_TRUE(Ipv4::of(1, 2, 3, 4) != Ipv4::of(1, 2, 3, 5));
  TEST_ASSERT_TRUE(Ipv4::of(1, 2, 3, 4) != Ipv4::of(4, 3, 2, 1));  // order matters
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_request_via_access_point_is_allowed);
  RUN_TEST(test_request_via_station_is_denied_even_while_ap_is_up);
  RUN_TEST(test_everything_is_denied_while_ap_is_down);
  RUN_TEST(test_unknown_origin_is_denied);
  RUN_TEST(test_custom_ap_address_is_respected);
  RUN_TEST(test_ipv4_equality_is_octet_wise);
  return UNITY_END();
}
