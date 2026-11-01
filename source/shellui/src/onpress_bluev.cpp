/* Native PS5 Settings actions for the optional BlueV audio plugin.
 * The page is owned by onionHEN; BlueV is controlled over 127.0.0.1:7733.
 */
#include "onpress.hpp"

#include "bluev_ui.hpp"
#include "settings_page_refresh.hpp"

namespace {

OnPressResult bluev_action(OnPressContext &ctx) {
  ctx.dirty = false;
  if (ctx.id == "id_bluev_refresh") {
    onion::shellui::settings::refresh(ctx.instance);
    return OnPressResult::Consumed;
  }

  onion::shellui::bluev::Snapshot snapshot;
  if (!onion::shellui::bluev::load_snapshot(snapshot)) {
    notify("%s", "BlueV is not responding. Start the plugin first.");
    return OnPressResult::Consumed;
  }

  const std::string endpoint =
      onion::shellui::bluev::post_endpoint(ctx.id, ctx.value, snapshot);
  if (endpoint.empty()) {
    notify("%s", "BlueV action is unavailable. Refresh the page.");
    return OnPressResult::Consumed;
  }

  std::string error;
  if (!onion::shellui::bluev::post_command(endpoint, error)) {
    notify("%s", error.c_str());
    return OnPressResult::Consumed;
  }

  notify("%s", "BlueV accepted the command. Press Refresh to update.");
  return OnPressResult::Consumed;
}

const OnPressPrefixEntry kBluevActions[] = {
    {"id_bluev_", bluev_action},
};

} // namespace

const OnPressPrefixEntry *onpress_bluev_prefix(size_t *count) {
  *count = sizeof(kBluevActions) / sizeof(kBluevActions[0]);
  return kBluevActions;
}
