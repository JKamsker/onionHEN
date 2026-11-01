#pragma once

#include "ps5_settings_ui.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace onion::shellui::bluev {

// BlueV remains an external, optional ELF. The native UI talks to its localhost API.
inline constexpr std::string_view kPluginId = "BLUE00001";
inline constexpr std::string_view kResource = "bluev.xml";

struct Adapter {
  std::string node;       // e.g. ugen1.2
  std::string name;
};

struct Device {
  std::string address;    // canonical AA:BB:CC:DD:EE:FF
  std::string name;
  bool bonded = false;
  bool connected = false;
  bool other = false;
};

struct Snapshot {
  bool online = false;
  bool radio_on = false;
  bool usb_radio = false;
  bool adapter_autostart = false;
  bool high_quality = false;
  bool local_only = false;
  int volume = -1;
  std::string state;
  std::string message;
  std::string peer_name;
  std::string adapter_node;
  std::vector<Adapter> adapters;
  std::vector<Device> devices;
};

bool valid_usb_node(std::string_view node);
std::string address_key(std::string_view address);
bool parse_status(std::string_view json, Snapshot &out);
bool parse_adapters(std::string_view json, Snapshot &out);
bool parse_devices(std::string_view json, Snapshot &out);

// The model is flat so settings_page_refresh can update it while the native page
// is active. In particular, nested lists/text fields cannot use live refresh.
void build_page(ps5ui::Page &page, const Snapshot &snapshot);

// Returns an allowlisted relative BlueV POST endpoint or empty for a stale,
// invalid, or unavailable control. Never accepts an arbitrary URL.
std::string post_endpoint(std::string_view control_id, std::string_view value,
                          const Snapshot &snapshot);

// Console-only networking implementation (bluev_client.cpp).
bool load_snapshot(Snapshot &out);
bool post_command(std::string_view endpoint, std::string &error);
void generate_xml(std::string &xml);

} // namespace onion::shellui::bluev
