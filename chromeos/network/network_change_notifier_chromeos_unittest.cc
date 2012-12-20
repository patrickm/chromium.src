// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/network_change_notifier_chromeos.h"

#include <string>

#include "base/basictypes.h"
#include "chromeos/network/network_change_notifier_factory_chromeos.h"
#include "chromeos/network/network_state.h"
#include "net/base/network_change_notifier.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

using net::NetworkChangeNotifier;

TEST(NetworkChangeNotifierChromeosTest, ConnectionTypeFromShill) {
  struct TypeMapping {
    const char* shill_type;
    const char* technology;
    NetworkChangeNotifier::ConnectionType connection_type;
  };
  TypeMapping type_mappings[] = {
    { flimflam::kTypeEthernet, "", NetworkChangeNotifier::CONNECTION_ETHERNET },
    { flimflam::kTypeWifi, "", NetworkChangeNotifier::CONNECTION_WIFI },
    { flimflam::kTypeWimax, "", NetworkChangeNotifier::CONNECTION_4G },
    { "unknown type", "unknown technology",
      NetworkChangeNotifier::CONNECTION_UNKNOWN },
    { flimflam::kTypeCellular, flimflam::kNetworkTechnology1Xrtt,
      NetworkChangeNotifier::CONNECTION_2G },
    { flimflam::kTypeCellular, flimflam::kNetworkTechnologyGprs,
      NetworkChangeNotifier::CONNECTION_2G },
    { flimflam::kTypeCellular, flimflam::kNetworkTechnologyEdge,
      NetworkChangeNotifier::CONNECTION_2G },
    { flimflam::kTypeCellular, flimflam::kNetworkTechnologyEvdo,
      NetworkChangeNotifier::CONNECTION_3G },
    { flimflam::kTypeCellular, flimflam::kNetworkTechnologyGsm,
      NetworkChangeNotifier::CONNECTION_3G },
    { flimflam::kTypeCellular, flimflam::kNetworkTechnologyUmts,
      NetworkChangeNotifier::CONNECTION_3G },
    { flimflam::kTypeCellular, flimflam::kNetworkTechnologyHspa,
      NetworkChangeNotifier::CONNECTION_3G },
    { flimflam::kTypeCellular,  flimflam::kNetworkTechnologyHspaPlus,
      NetworkChangeNotifier::CONNECTION_4G },
    { flimflam::kTypeCellular, flimflam::kNetworkTechnologyLte,
      NetworkChangeNotifier::CONNECTION_4G },
    { flimflam::kTypeCellular, flimflam::kNetworkTechnologyLteAdvanced,
      NetworkChangeNotifier::CONNECTION_4G },
    { flimflam::kTypeCellular, "unknown technology",
      NetworkChangeNotifier::CONNECTION_2G }
  };

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(type_mappings); ++i) {
    NetworkChangeNotifier::ConnectionType type =
        NetworkChangeNotifierChromeos::ConnectionTypeFromShill(
            type_mappings[i].shill_type, type_mappings[i].technology);
    EXPECT_EQ(type_mappings[i].connection_type, type);
  }
}

class NetworkChangeNotifierChromeosUpdateTest : public testing::Test {
 protected:
  NetworkChangeNotifierChromeosUpdateTest() : active_network_("") {
  }
  virtual ~NetworkChangeNotifierChromeosUpdateTest() {}

  void SetNotifierState(NetworkChangeNotifier::ConnectionType type,
                        std::string service_path,
                        std::string ip_address) {
    notifier_.ip_address_ = ip_address;
    notifier_.service_path_ = service_path;
    notifier_.connection_type_ = type;
  }

  void VerifyNotifierState(NetworkChangeNotifier::ConnectionType expected_type,
                           std::string expected_service_path,
                           std::string expected_ip_address) {
    EXPECT_EQ(expected_type, notifier_.connection_type_);
    EXPECT_EQ(expected_ip_address, notifier_.ip_address_);
    EXPECT_EQ(expected_service_path, notifier_.service_path_);
  }

  // Sets the active network state used for notifier updates.
  void SetActiveNetworkState(bool is_connected,
                             std::string type,
                             std::string technology,
                             std::string service_path,
                             std::string ip_address) {
    if (is_connected)
      active_network_.state_ = flimflam::kStateOnline;
    else
      active_network_.state_ = flimflam::kStateConfiguration;
    active_network_.type_ = type;
    active_network_.technology_ = technology;
    active_network_.path_ = service_path;
    active_network_.ip_address_ = ip_address;
  }

  // Process an active network update based on the state of |active_network_|.
  void ProcessActiveNetworkUpdate(bool* type_changed,
                                  bool* ip_changed,
                                  bool* dns_changed) {
    notifier_.UpdateState(&active_network_, type_changed, ip_changed,
                          dns_changed);
  }

 private:
  NetworkState active_network_;
  NetworkChangeNotifierChromeos notifier_;
};

TEST_F(NetworkChangeNotifierChromeosUpdateTest, UpdateActiveNetworkOffline) {
  // Test that Online to Offline transitions are correctly handled.
  SetNotifierState(NetworkChangeNotifier::CONNECTION_ETHERNET, "/service/1",
                   "192.168.1.1");
  SetActiveNetworkState(false,  // offline.
                        flimflam::kTypeEthernet, "", "/service/1", "");
  bool type_changed = false, ip_changed = false, dns_changed = false;
  ProcessActiveNetworkUpdate(&type_changed, &ip_changed, &dns_changed);
  VerifyNotifierState(NetworkChangeNotifier::CONNECTION_NONE, "", "");
  EXPECT_TRUE(type_changed);
  EXPECT_TRUE(ip_changed);
  EXPECT_TRUE(dns_changed);
}

TEST_F(NetworkChangeNotifierChromeosUpdateTest, UpdateActiveNetworkOnline) {
  // Test that Offline to Online transitions are correctly handled.
  SetNotifierState(NetworkChangeNotifier::CONNECTION_NONE, "", "");

  SetActiveNetworkState(false,  // offline.
                        flimflam::kTypeEthernet, "",
                        "192.168.0.1", "/service/1");
  bool type_changed = false, ip_changed = false, dns_changed = false;
  ProcessActiveNetworkUpdate(&type_changed, &ip_changed, &dns_changed);
  // If the new active network is still offline, nothing should have changed.
  VerifyNotifierState(NetworkChangeNotifier::CONNECTION_NONE, "", "");
  EXPECT_FALSE(type_changed);
  EXPECT_FALSE(ip_changed);
  EXPECT_FALSE(dns_changed);

  SetActiveNetworkState(true,  // online.
                        flimflam::kTypeEthernet, "", "/service/1",
                        "192.168.0.1");
  ProcessActiveNetworkUpdate(&type_changed, &ip_changed, &dns_changed);
  // Now the new active network is online, so this should trigger a notifier
  // state change.
  VerifyNotifierState(NetworkChangeNotifier::CONNECTION_ETHERNET, "/service/1",
                      "192.168.0.1");
  EXPECT_TRUE(type_changed);
  EXPECT_TRUE(ip_changed);
  EXPECT_TRUE(dns_changed);
}

TEST_F(NetworkChangeNotifierChromeosUpdateTest, UpdateActiveNetworkChanged) {
  // Test that Online to Online transisions (active network changes) are
  // correctly handled.
  SetNotifierState(NetworkChangeNotifier::CONNECTION_ETHERNET, "/service/1",
                   "192.168.1.1");

  SetActiveNetworkState(true,  // online.
                        flimflam::kTypeWifi, "", "/service/2", "192.168.1.2");
  bool type_changed = false, ip_changed = false, dns_changed = false;
  ProcessActiveNetworkUpdate(&type_changed, &ip_changed, &dns_changed);
  VerifyNotifierState(NetworkChangeNotifier::CONNECTION_WIFI, "/service/2",
                      "192.168.1.2" );
  EXPECT_TRUE(type_changed);
  EXPECT_TRUE(ip_changed);
  EXPECT_TRUE(dns_changed);

  SetActiveNetworkState(true,  // online.
                        flimflam::kTypeWifi, "", "/service/3", "192.168.1.2");
  ProcessActiveNetworkUpdate(&type_changed, &ip_changed, &dns_changed);
  VerifyNotifierState(NetworkChangeNotifier::CONNECTION_WIFI, "/service/3",
                      "192.168.1.2" );
  EXPECT_FALSE(type_changed);
  // A service path change (even with a corresponding IP change) should still
  // trigger an IP address update to observers.
  EXPECT_TRUE(ip_changed);
  EXPECT_TRUE(dns_changed);
}

}  // namespace chromeos
