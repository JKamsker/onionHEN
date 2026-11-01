#include "bluev_ui.hpp"

#include "settings_page_refresh.hpp"
#include "onion_cjson.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace onion::shellui::bluev {
namespace {

constexpr uint16_t kBluevPort = 7733;
constexpr size_t kMaxHttpBytes = 64 * 1024;
constexpr size_t kMaxHeaderBytes = 4096;

// BlueV 0.4's API uses short-lived HTTP/1.0 connections with Content-Length.
// Only localhost:7733 and fixed /api endpoints are ever contacted.
bool request(const char *method, std::string_view endpoint,
             std::string &body, int &status) {
  body.clear();
  status = 0;
  if (!endpoint.starts_with("/api/") || endpoint.size() > 192 ||
      endpoint.find_first_of("\r\n \t") != std::string_view::npos)
    return false;

  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;

  timeval timeout{};
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
  const int no_sigpipe = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                   &no_sigpipe, sizeof(no_sigpipe));
#endif

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(kBluevPort);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, reinterpret_cast<const sockaddr *>(&address),
              sizeof(address)) != 0) {
    close(fd);
    return false;
  }

  const std::string message = std::string(method) + " " + std::string(endpoint) +
                              " HTTP/1.0\r\nHost: 127.0.0.1\r\n"
                              "Connection: close\r\nContent-Length: 0\r\n\r\n";
  size_t sent = 0;
  bool ok = true;
  while (sent < message.size()) {
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    const ssize_t count = send(fd, message.data() + sent,
                               message.size() - sent, flags);
    if (count <= 0) {
      if (count < 0 && errno == EINTR) continue;
      ok = false;
      break;
    }
    sent += static_cast<size_t>(count);
  }

  std::string reply;
  char buffer[2048];
  while (ok && reply.size() <= kMaxHttpBytes + kMaxHeaderBytes) {
    const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
    if (count == 0) break;
    if (count < 0) {
      if (errno == EINTR) continue;
      ok = false;
      break;
    }
    reply.append(buffer, static_cast<size_t>(count));
  }
  close(fd);
  if (!ok || reply.size() > kMaxHttpBytes + kMaxHeaderBytes) return false;

  const size_t separator = reply.find("\r\n\r\n");
  if (separator == std::string::npos || separator > kMaxHeaderBytes)
    return false;
  const std::string header = reply.substr(0, separator);
  int code = 0;
  if (std::sscanf(header.c_str(), "HTTP/%*s %d", &code) != 1 ||
      code < 100 || code > 599)
    return false;
  status = code;
  body = reply.substr(separator + 4);
  return body.size() <= kMaxHttpBytes;
}

bool get_json(std::string_view path, std::string &json) {
  int status = 0;
  return request("GET", path, json, status) && status == 200;
}

bool model(ps5ui::Node &out) {
  Snapshot snapshot;
  (void)load_snapshot(snapshot);
  ps5ui::Page page("id_bluev_settings", "Bluetooth audio (BlueV)");
  build_page(page, snapshot);
  out = page.root();
  return true;
}

} // namespace

bool load_snapshot(Snapshot &out) {
  out = {};
  std::string json;
  if (!get_json("/api/status", json) || !parse_status(json, out))
    return false;
  if (get_json("/api/adapters", json))
    (void)parse_adapters(json, out);
  if (get_json("/api/devices", json))
    (void)parse_devices(json, out);
  return true;
}

bool post_command(std::string_view endpoint, std::string &error) {
  std::string json;
  int status = 0;
  if (!request("POST", endpoint, json, status)) {
    error = "Cannot contact BlueV at localhost:7733";
    return false;
  }
  onion_cjson::Root root(json);
  const bool success = status == 200 && root &&
                       cJSON_IsTrue(onion_cjson::item(root.get(), "ok"));
  if (!success) {
    error = root ? onion_cjson::string_item(root.get(), "message", "BlueV refused the command")
                 : "BlueV returned an invalid response";
  }
  return success;
}

void generate_xml(std::string &xml) {
  ps5ui::Node snapshot;
  model(snapshot);
  xml = settings::publish(snapshot, model);
}

} // namespace onion::shellui::bluev
