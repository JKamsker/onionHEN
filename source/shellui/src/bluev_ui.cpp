#include "bluev_ui.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include "onion_cjson.hpp"

namespace onion::shellui::bluev {
namespace {

bool hex(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}

std::string str(const cJSON *root, const char *name) {
  return onion_cjson::string_item(root, name, "");
}

bool is_idle(const Snapshot &s) { return s.state == "idle"; }

bool device_matches(const Snapshot &s, std::string_view key, bool require_bond) {
  return std::any_of(s.devices.begin(), s.devices.end(), [&](const Device &d) {
    return address_key(d.address) == key && (!require_bond || d.bonded);
  });
}

std::string device_address(const Snapshot &s, std::string_view key) {
  for (const Device &d : s.devices)
    if (address_key(d.address) == key)
      return d.address;
  return {};
}

} // namespace

bool valid_usb_node(std::string_view node) {
  if (node.size() < 7 || node.size() > 20 || !node.starts_with("ugen"))
    return false;
  bool dot = false;
  for (size_t i = 4; i < node.size(); ++i) {
    if (node[i] == '.' && !dot && i > 4 && i + 1 < node.size()) {
      dot = true;
    } else if (node[i] < '0' || node[i] > '9') {
      return false;
    }
  }
  return dot;
}

std::string address_key(std::string_view address) {
  if (address.size() != 17)
    return {};
  std::string key;
  key.reserve(12);
  for (size_t i = 0; i < address.size(); ++i) {
    if (i % 3 == 2) {
      if (address[i] != ':') return {};
    } else {
      if (!hex(address[i])) return {};
      key += static_cast<char>(std::tolower(static_cast<unsigned char>(address[i])));
    }
  }
  return key;
}

bool parse_status(std::string_view json, Snapshot &out) {
  onion_cjson::Root root(json.data(), json.size());
  if (!root || !cJSON_IsObject(root.get()))
    return false;
  const cJSON *radio = onion_cjson::item(root.get(), "radio");
  const cJSON *state = onion_cjson::item(root.get(), "state");
  if (!cJSON_IsObject(radio) || !cJSON_IsString(state) ||
      !cJSON_IsBool(onion_cjson::item(radio, "on")))
    return false;

  Snapshot updated;
  updated.online = true;
  updated.state = str(root.get(), "state");
  if (updated.state != "idle" && updated.state != "scanning" &&
      updated.state != "connecting" && updated.state != "streaming")
    return false;
  updated.message = str(root.get(), "message");
  updated.volume = onion_cjson::int_item(root.get(), "volume", -1);
  updated.high_quality = onion_cjson::int_item(root.get(), "quality") == 1;
  updated.local_only = str(root.get(), "network") == "local";
  const cJSON *peer = onion_cjson::item(root.get(), "peer");
  if (cJSON_IsObject(peer))
    updated.peer_name = str(peer, "name");
  updated.radio_on = onion_cjson::bool_item(radio, "on");
  updated.usb_radio = str(radio, "adapter") == "external";
  updated.adapter_node = str(radio, "device");
  updated.adapter_autostart = onion_cjson::bool_item(radio, "autostart");
  out = std::move(updated);
  return true;
}

bool parse_adapters(std::string_view json, Snapshot &out) {
  onion_cjson::Root root(json.data(), json.size());
  const cJSON *items = root ? onion_cjson::item(root.get(), "external") : nullptr;
  if (!cJSON_IsArray(items))
    return false;
  std::vector<Adapter> adapters;
  const int count = std::min(cJSON_GetArraySize(items), 32);
  for (int i = 0; i < count; ++i) {
    const cJSON *item = cJSON_GetArrayItem(items, i);
    const std::string node = str(item, "device");
    const int cls = onion_cjson::int_item(item, "class", -1);
    if (!valid_usb_node(node) || (cls != 224 && cls != 255) ||
        std::any_of(adapters.begin(), adapters.end(),
                    [&](const Adapter &a) { return a.node == node; }))
      continue;
    std::string name = str(item, "product");
    if (name.empty()) name = str(item, "vendor");
    if (name.empty()) name = "USB Bluetooth";
    adapters.push_back({node, std::move(name)});
  }
  out.adapters = std::move(adapters);
  return true;
}

bool parse_devices(std::string_view json, Snapshot &out) {
  onion_cjson::Root root(json.data(), json.size());
  if (!root || !cJSON_IsArray(root.get()))
    return false;
  std::vector<Device> devices;
  const int count = std::min(cJSON_GetArraySize(root.get()), 32);
  for (int i = 0; i < count; ++i) {
    const cJSON *item = cJSON_GetArrayItem(root.get(), i);
    const std::string addr = str(item, "addr");
    const std::string key = address_key(addr);
    if (key.empty() ||
        std::any_of(devices.begin(), devices.end(), [&](const Device &d) {
          return address_key(d.address) == key;
        }))
      continue;
    Device device;
    device.address = addr;
    device.name = str(item, "name");
    device.bonded = onion_cjson::bool_item(item, "bonded");
    device.connected = onion_cjson::bool_item(item, "connected");
    device.other = onion_cjson::bool_item(item, "other");
    devices.push_back(std::move(device));
  }
  out.devices = std::move(devices);
  return true;
}

void build_page(ps5ui::Page &page, const Snapshot &s) {
  page.button("id_bluev_refresh", "Refresh status and devices");
  if (!s.online) {
    page.label("id_bluev_unavailable",
               "BlueV is not responding. Start the BlueV plugin first.");
    return;
  }

  std::string state = "BlueV: " + s.state;
  if (!s.peer_name.empty()) state += " - " + s.peer_name;
  page.label("id_bluev_state", state);
  if (!s.message.empty())
    page.label("id_bluev_message", s.message);

  if (!s.radio_on) {
    page.label("id_bluev_radio_off_status", "Bluetooth adapter: off");
    for (const Adapter &adapter : s.adapters)
      page.button("id_bluev_start_" + adapter.node,
                  "Start USB: " + adapter.name, adapter.node);
    if (s.adapters.empty())
      page.label("id_bluev_no_usb", "No supported USB Bluetooth adapter detected.");
    page.button("id_bluev_start_internal", "Start internal PS5 Bluetooth",
                "May interfere with DualSense controllers", std::nullopt,
                std::nullopt, ps5ui::Style::None,
                "The internal radio is shared with controllers. Continue?", "Continue");
  } else {
    page.label("id_bluev_adapter_status",
               s.usb_radio ? "USB adapter: " + s.adapter_node
                           : "Internal Bluetooth adapter (shared with controller)");
    if (is_idle(s)) {
      page.button("id_bluev_scan", "Scan for Bluetooth speakers/headphones");
      page.button("id_bluev_radio_off", "Turn Bluetooth adapter off");
    } else if (s.state == "connecting" || s.state == "streaming") {
      page.button("id_bluev_disconnect", "Disconnect Bluetooth audio");
    }

    if (s.usb_radio)
      page.toggle("id_bluev_autostart", "Auto-start USB adapter",
                  s.adapter_autostart, "When BlueV starts");
    if (s.devices.empty()) {
      page.label("id_bluev_no_devices", "No devices yet. Scan, then Refresh.");
    } else {
      for (const Device &device : s.devices) {
        const std::string key = address_key(device.address);
        const std::string name = device.name.empty() ? device.address : device.name;
        std::string detail = device.address;
        if (device.connected) detail += " - connected";
        else if (device.bonded) detail += " - paired";
        if (device.other) detail += " - may not be audio";
        page.label("id_bluev_dev_" + key, name + " (" + detail + ")");
        if (is_idle(s)) {
          page.button("id_bluev_connect_" + key, "Connect: " + name);
          if (device.bonded)
            page.button("id_bluev_forget_" + key, "Forget: " + name,
                        "Remove stored pairing", std::nullopt, std::nullopt,
                        ps5ui::Style::None, "Forget this Bluetooth device?", "Forget");
        }
      }
    }
  }

  page.toggle("id_bluev_quality", "High audio quality", s.high_quality,
              "Applies to the next stream");
  page.toggle("id_bluev_local_only", "Limit BlueV access to this PS5",
              s.local_only, "Recommended: disable unauthenticated LAN access");

  if (s.state == "streaming") {
    if (s.volume >= 0 && s.volume <= 100) {
      page.label("id_bluev_volume", "Headphone volume: " + std::to_string(s.volume) + "%");
      page.button("id_bluev_volume_down", "Volume -10%");
      page.button("id_bluev_volume_up", "Volume +10%");
    } else {
      page.label("id_bluev_no_avrcp", "Headphone does not report AVRCP volume.");
    }
  }
}

std::string post_endpoint(std::string_view id, std::string_view value,
                          const Snapshot &s) {
  if (!s.online)
    return {};
  if (id == "id_bluev_quality" && (value == "0" || value == "1"))
    return std::string("/api/quality?v=") + std::string(value);
  if (id == "id_bluev_local_only" && (value == "0" || value == "1"))
    return value == "1" ? "/api/network?v=local" : "/api/network?v=lan";
  if (id == "id_bluev_autostart" && s.radio_on && s.usb_radio &&
      (value == "0" || value == "1"))
    return std::string("/api/autostart?on=") + std::string(value);
  if (id == "id_bluev_start_internal" && !s.radio_on)
    return "/api/radio/on?adapter=internal";
  if (id.starts_with("id_bluev_start_") && !s.radio_on) {
    const std::string_view node = id.substr(sizeof("id_bluev_start_") - 1);
    if (valid_usb_node(node) &&
        std::any_of(s.adapters.begin(), s.adapters.end(),
                    [&](const Adapter &a) { return a.node == node; }))
      return "/api/radio/on?adapter=external&dev=" + std::string(node);
  }
  if (id == "id_bluev_radio_off" && s.radio_on && is_idle(s))
    return "/api/radio/off";
  if (id == "id_bluev_scan" && s.radio_on && is_idle(s))
    return "/api/scan";
  if (id == "id_bluev_disconnect" &&
      (s.state == "connecting" || s.state == "streaming"))
    return "/api/disconnect";
  if (id == "id_bluev_volume_up" && s.state == "streaming" &&
      s.volume >= 0 && s.volume <= 100)
    return "/api/volume?v=" + std::to_string(std::min(100, s.volume + 10));
  if (id == "id_bluev_volume_down" && s.state == "streaming" &&
      s.volume >= 0 && s.volume <= 100)
    return "/api/volume?v=" + std::to_string(std::max(0, s.volume - 10));

  constexpr std::string_view connect = "id_bluev_connect_";
  constexpr std::string_view forget = "id_bluev_forget_";
  if (!s.radio_on || !is_idle(s))
    return {};
  if (id.starts_with(connect)) {
    const auto key = id.substr(connect.size());
    if (key.size() == 12 && device_matches(s, key, false))
      return "/api/connect?addr=" + device_address(s, key);
  }
  if (id.starts_with(forget)) {
    const auto key = id.substr(forget.size());
    if (key.size() == 12 && device_matches(s, key, true))
      return "/api/forget?addr=" + device_address(s, key);
  }
  return {};
}

} // namespace onion::shellui::bluev
