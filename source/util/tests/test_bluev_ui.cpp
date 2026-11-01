#include "test_harness.h"

#include "bluev_ui.hpp"
#include "settings_page_update.hpp"

#include <algorithm>
#include <string>

using onion::shellui::bluev::Snapshot;
namespace bluev = onion::shellui::bluev;

static bool has_id(const ps5ui::Page &page, const std::string &id) {
  for (const auto &node : page.root().children)
    if (node.attrs.id == id) return true;
  return false;
}

static int test_status_and_adapters() {
  Snapshot s;
  TEST_ASSERT_TRUE(bluev::parse_status(
      R"({"state":"idle","message":"ready","peer":{"name":"","addr":""},)"
      R"("volume":-1,"quality":1,"network":"local",)"
      R"("radio":{"on":false,"adapter":"internal","device":"","autostart":false}})", s));
  TEST_ASSERT_TRUE(s.online && !s.radio_on && s.high_quality && s.local_only);
  TEST_ASSERT_TRUE(bluev::parse_adapters(
      R"({"internal":true,"external":[)"
      R"({"device":"ugen1.2","class":224,"product":"CSR dongle"},)"
      R"({"device":"ugen2.2","class":8,"product":"Disk"},)"
      R"({"device":"ugen5.2\n","class":255,"product":"Bad"}]})", s));
  TEST_ASSERT_EQ_INT(1, static_cast<int>(s.adapters.size()));
  TEST_ASSERT_STREQ("ugen1.2", s.adapters[0].node.c_str());

  ps5ui::Page page("id_bluev_settings", "Bluetooth");
  bluev::build_page(page, s);
  TEST_ASSERT_TRUE(has_id(page, "id_bluev_start_ugen1.2"));
  TEST_ASSERT_TRUE(has_id(page, "id_bluev_start_internal"));
  TEST_ASSERT_TRUE(onion::shellui::settings::valid_live_model(page.root()));
  TEST_ASSERT_STREQ("/api/radio/on?adapter=external&dev=ugen1.2",
                    bluev::post_endpoint("id_bluev_start_ugen1.2", "", s).c_str());
  TEST_ASSERT_TRUE(bluev::post_endpoint("id_bluev_start_ugen2.2", "", s).empty());
  TEST_ASSERT_STREQ("/api/radio/on?adapter=internal",
                    bluev::post_endpoint("id_bluev_start_internal", "", s).c_str());
  TEST_ASSERT_STREQ("/api/quality?v=0",
                    bluev::post_endpoint("id_bluev_quality", "0", s).c_str());
  TEST_ASSERT_STREQ("/api/network?v=local",
                    bluev::post_endpoint("id_bluev_local_only", "1", s).c_str());
  return 0;
}

static int test_devices_and_commands() {
  Snapshot s;
  TEST_ASSERT_TRUE(bluev::parse_status(
      R"({"state":"idle","radio":{"on":true,"adapter":"external","device":"ugen1.2","autostart":true},)"
      R"("quality":0,"network":"lan","volume":-1})", s));
  TEST_ASSERT_TRUE(bluev::parse_devices(
      R"([{"addr":"AA:BB:CC:DD:EE:FF","name":"Headphones & <blue>",)"
      R"("bonded":true,"connected":false,"other":false},)"
      R"({"addr":"11:22:33:44:55:66","name":"Unpaired","bonded":false,"connected":false}])", s));

  ps5ui::Page page("id_bluev_settings", "Bluetooth");
  bluev::build_page(page, s);
  TEST_ASSERT_TRUE(onion::shellui::settings::valid_live_model(page.root()));
  TEST_ASSERT_TRUE(has_id(page, "id_bluev_connect_aabbccddeeff"));
  TEST_ASSERT_TRUE(has_id(page, "id_bluev_forget_aabbccddeeff"));
  TEST_ASSERT_TRUE(!has_id(page, "id_bluev_forget_112233445566"));
  TEST_ASSERT_TRUE(page.build().find("Headphones &amp; &lt;blue&gt;") != std::string::npos);
  TEST_ASSERT_STREQ("/api/connect?addr=AA:BB:CC:DD:EE:FF",
                    bluev::post_endpoint("id_bluev_connect_aabbccddeeff", "", s).c_str());
  TEST_ASSERT_STREQ("/api/forget?addr=AA:BB:CC:DD:EE:FF",
                    bluev::post_endpoint("id_bluev_forget_aabbccddeeff", "", s).c_str());
  TEST_ASSERT_TRUE(bluev::post_endpoint("id_bluev_forget_112233445566", "", s).empty());
  TEST_ASSERT_TRUE(bluev::post_endpoint("id_bluev_connect_123456789012", "", s).empty());
  TEST_ASSERT_STREQ("/api/autostart?on=0",
                    bluev::post_endpoint("id_bluev_autostart", "0", s).c_str());
  TEST_ASSERT_STREQ("/api/scan", bluev::post_endpoint("id_bluev_scan", "", s).c_str());
  TEST_ASSERT_STREQ("/api/radio/off", bluev::post_endpoint("id_bluev_radio_off", "", s).c_str());
  return 0;
}

static int test_streaming_volume_and_stale_controls() {
  Snapshot s;
  TEST_ASSERT_TRUE(bluev::parse_status(
      R"({"state":"streaming","volume":95,"quality":1,"network":"local",)"
      R"("radio":{"on":true,"adapter":"internal","device":"","autostart":false}})", s));
  ps5ui::Page page("id_bluev_settings", "Bluetooth");
  bluev::build_page(page, s);
  TEST_ASSERT_TRUE(onion::shellui::settings::valid_live_model(page.root()));
  TEST_ASSERT_TRUE(has_id(page, "id_bluev_disconnect"));
  TEST_ASSERT_TRUE(has_id(page, "id_bluev_volume_up"));
  TEST_ASSERT_TRUE(!has_id(page, "id_bluev_scan"));
  TEST_ASSERT_TRUE(bluev::post_endpoint("id_bluev_scan", "", s).empty());
  TEST_ASSERT_TRUE(bluev::post_endpoint("id_bluev_radio_off", "", s).empty());
  TEST_ASSERT_TRUE(bluev::post_endpoint("id_bluev_autostart", "1", s).empty());
  TEST_ASSERT_STREQ("/api/volume?v=100",
                    bluev::post_endpoint("id_bluev_volume_up", "", s).c_str());
  TEST_ASSERT_STREQ("/api/volume?v=85",
                    bluev::post_endpoint("id_bluev_volume_down", "", s).c_str());
  TEST_ASSERT_STREQ("/api/disconnect",
                    bluev::post_endpoint("id_bluev_disconnect", "", s).c_str());
  TEST_ASSERT_TRUE(bluev::post_endpoint("id_bluev_quality", "2", s).empty());
  return 0;
}

static int test_invalid_and_offline() {
  Snapshot s;
  TEST_ASSERT_TRUE(!bluev::parse_status("{}", s));
  TEST_ASSERT_TRUE(!bluev::parse_status(R"({"state":"bogus","radio":{"on":true}})", s));
  TEST_ASSERT_TRUE(!bluev::valid_usb_node("ugen0"));
  TEST_ASSERT_TRUE(!bluev::valid_usb_node("ugen1.2&x=y"));
  TEST_ASSERT_TRUE(bluev::valid_usb_node("ugen12.34"));
  TEST_ASSERT_TRUE(bluev::address_key("00:01:02:FE:FF:AB") == "000102feffab");
  TEST_ASSERT_TRUE(bluev::address_key("00:01:02:FE:FF:%0").empty());
  TEST_ASSERT_TRUE(bluev::post_endpoint("id_bluev_quality", "1", s).empty());
  ps5ui::Page page("id_bluev_settings", "Bluetooth");
  bluev::build_page(page, s);
  TEST_ASSERT_TRUE(onion::shellui::settings::valid_live_model(page.root()));
  TEST_ASSERT_TRUE(has_id(page, "id_bluev_unavailable"));
  return 0;
}

extern "C" int test_bluev_ui_suite(void) {
  int fails = 0;
  fails += onion_test_run("bluev.status_adapters", test_status_and_adapters);
  fails += onion_test_run("bluev.devices_actions", test_devices_and_commands);
  fails += onion_test_run("bluev.streaming", test_streaming_volume_and_stale_controls);
  fails += onion_test_run("bluev.invalid_offline", test_invalid_and_offline);
  return fails;
}
