// Native API workbench. UI operations use the same ABI-1 API supplied to
// plugins.
#include "host.h"
#ifdef DM_DESKTOP_GL
#include "vendor/imgui/backends/imgui_impl_opengl2.h"
#else
#include "vendor/imgui/backends/imgui_impl_opengl3.h"
#endif
#include "vendor/imgui/backends/imgui_impl_sdl2.h"
#include "vendor/imgui/imgui.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

struct Allocation {
  uint32_t offset, size;
};
struct Menu {
  w2c_a *host;
  const DMApi *api;
  DMContext *ctx;
  bool open = false, advanced = false, core_advanced = false,
       confirm_write = false;
  std::vector<std::string> logs, messages, inputs;
  std::vector<Allocation> allocations;
  std::vector<unsigned char> preview;
  std::string status = "Ready. F2 toggles this workbench.", result;
  char filter[128] = {}, asset_filter[128] = {}, command[2048] = {};
  char key[64] = "notes", text[4096] = {}, topic[64] = "workbench",
       payload[2048] = {};
  char setting[64] = "menu_key", title[256] = "Drive Mad - Native Workbench";
  char log_text[512] = "Hello from the native API workbench";
  char hex[4096] = {}, mod_id[64] = "native-tools", mod_command[512] = "info";
  int selected_export = 0, selected_asset = 0, body = 0, level = 0, bytes = 64,
      allocation_size = 256;
  uint32_t address = 0;
  DMValue args[DM_MAX_ARGS] = {};
  float speed = 1, grav[3] = {0, -9.8f, 0}, velocity[3] = {0, 5, 0},
        spin[3] = {0, 0, 2};
  float force[3] = {0, 100, 0}, friction = 0.5f, bounce = 0.5f, lift = 4;
  int selected_allocation = -1;
  int requested_tab = -1;
  unsigned test_tick = 0, test_tabs = 0;
  uint64_t test_frames = 0;
  ImVec2 pause_target = {}, step_target = {};
};
static Menu *get(w2c_a *h) { return static_cast<Menu *>(h->menu); }
static void append(std::vector<std::string> &list, const std::string &line,
                   size_t max = 256) {
  if (list.size() >= max)
    list.erase(list.begin());
  list.push_back(line.substr(0, 4096));
}
static void status(Menu &m, const char *operation, DMStatus s) {
  m.status = std::string(operation) + ": " + m.api->status_string(s);
  append(m.logs, m.status);
}
static const char *type(DMType t) {
  const char *names[] = {"void", "i32", "i64", "f32", "f64"};
  return t >= DM_VOID && t <= DM_F64 ? names[t] : "?";
}
static std::string value(const DMValue &v) {
  char b[128];
  switch (v.type) {
  case DM_I32:
    std::snprintf(b, sizeof(b), "%u (signed %d)", v.as.i32, (int32_t)v.as.i32);
    break;
  case DM_I64:
    std::snprintf(b, sizeof(b), "%llu", (unsigned long long)v.as.i64);
    break;
  case DM_F32:
    std::snprintf(b, sizeof(b), "%.9g", v.as.f32);
    break;
  case DM_F64:
    std::snprintf(b, sizeof(b), "%.17g", v.as.f64);
    break;
  default:
    std::snprintf(b, sizeof(b), "void");
  }
  return b;
}
static DMStatus call(Menu &m, const char *name,
                     std::initializer_list<DMValue> args = {}) {
  DMValue result = {};
  DMStatus s =
      m.api->call(m.ctx, name, args.begin(), (uint32_t)args.size(), &result);
  status(m, name, s);
  if (s == DM_OK)
    m.result = value(result);
  return s;
}
static DMValue query(Menu &m, const char *name,
                     std::initializer_list<DMValue> args = {}) {
  DMValue v = {};
  if (m.api->call(m.ctx, name, args.begin(), (uint32_t)args.size(), &v) !=
      DM_OK)
    v = {};
  return v;
}
static void lines(const char *id, const std::vector<std::string> &list,
                  float height = 170) {
  ImGui::BeginChild(id, ImVec2(0, height), ImGuiChildFlags_Borders);
  for (const auto &s : list)
    ImGui::TextUnformatted(s.c_str());
  ImGui::EndChild();
}
static void bytes_view(const std::vector<unsigned char> &bytes) {
  ImGui::Text("%zu bytes; showing first %zu", bytes.size(),
              std::min(bytes.size(), size_t(2048)));
  ImGui::BeginChild("bytes", ImVec2(0, 180), ImGuiChildFlags_Borders);
  for (size_t i = 0; i < std::min(bytes.size(), size_t(2048)); i += 16) {
    char hex[80] = {}, ascii[17] = {};
    for (size_t j = 0; j < 16 && i + j < bytes.size(); j++) {
      std::snprintf(hex + j * 3, 4, "%02x ", bytes[i + j]);
      ascii[j] =
          bytes[i + j] >= 32 && bytes[i + j] < 127 ? (char)bytes[i + j] : '.';
    }
    ImGui::Text("%08zx  %-48s %s", i, hex, ascii);
  }
  ImGui::EndChild();
}
static bool parse_hex(const char *text, std::vector<unsigned char> &out) {
  int high = -1;
  out.clear();
  for (; *text; text++) {
    if (std::isspace((unsigned char)*text))
      continue;
    int c = std::tolower((unsigned char)*text);
    int v = c >= '0' && c <= '9'   ? c - '0'
            : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                   : -1;
    if (v < 0)
      return false;
    if (high < 0)
      high = v;
    else {
      out.push_back((unsigned char)(high * 16 + v));
      high = -1;
    }
  }
  return high < 0;
}
static void gameplay_page(Menu &m, const DMState &state) {
  ImGui::TextWrapped("Real game operations, not invented car offsets. World "
                     "tools affect all dynamic bodies, including bridge "
                     "pieces. Progress and rewards can persist.");
  ImGui::BeginDisabled(!state.ready);
  if (ImGui::Button(state.paused ? "Resume" : "Pause"))
    call(m, "native.pause", {dm_i32(!state.paused)});
  m.pause_target =
      ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) / 2,
             (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) / 2);
  ImGui::SameLine();
  ImGui::BeginDisabled(!state.paused);
  if (ImGui::Button("Step one host frame"))
    call(m, "native.step");
  m.step_target =
      ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) / 2,
             (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) / 2);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Local reward")) {
    if (call(m, "ad_rewarded_on_showed", {dm_i32(1)}) == DM_OK)
      call(m, "ad_rewarded_on_rewarded");
  }
  m.speed = query(m, "native.get_time_scale").as.f32;
  if (ImGui::SliderFloat("Game clock scale", &m.speed, 0.05f, 3.0f, "%.2fx"))
    call(m, "native.time_scale", {dm_f32(m.speed)});
  ImGui::TextWrapped(
      "Clock scaling affects game UI, timers and game timestamps too; it is "
      "not a physics-only clock. A step is one host frame, not a guaranteed "
      "single physics tick.");
  int drive = (int32_t)query(m, "native.get_auto_drive").as.i32 + 1;
  const char *directions[] = {"Reverse", "Off", "Forward"};
  if (ImGui::Combo("Auto drive", &drive, directions, 3))
    call(m, "native.auto_drive", {dm_i32((uint32_t)(drive - 1))});
  bool invert = query(m, "native.get_invert_controls").as.i32 != 0;
  if (ImGui::Checkbox("Invert A/D and left/right", &invert))
    call(m, "native.invert_controls", {dm_i32(invert)});
  if (ImGui::Button("Reset clock / drive / inversion"))
    call(m, "native.reset_controls");
  ImGui::TextWrapped("Driving input is captured while this menu is open. Auto "
                     "drive resumes when you close it.");
  bool profile = query(m, "native.profile_supported").as.i32 != 0;
  ImGui::SeparatorText(
      profile ? "Verified stock v166 physics profile"
              : "Memory profile disabled: edited / unknown executable");
  ImGui::BeginDisabled(!profile);
  int count = (int)query(m, "native.level_count").as.i32;
  ImGui::Text("Current level index: %u / %d available records (zero-based)",
              query(m, "native.level_index").as.i32, count);
  ImGui::InputInt("Target level index", &m.level);
  if (ImGui::Button("Start level"))
    call(m, "native.start_level", {dm_i32((uint32_t)m.level)});
  ImGui::SameLine();
  if (ImGui::Button("Restart"))
    call(m, "native.restart");
  ImGui::SameLine();
  if (ImGui::Button("Next level"))
    call(m, "native.next_level");
  ImGui::SeparatorText("World gravity");
  ImGui::Text("Current: %.3f %.3f %.3f", query(m, "native.gravity_x").as.f32,
              query(m, "native.gravity_y").as.f32,
              query(m, "native.gravity_z").as.f32);
  ImGui::InputFloat3("Gravity vector", &m.grav[0]);
  if (ImGui::Button("Apply gravity"))
    call(m, "native.gravity",
         {dm_f32(m.grav[0]), dm_f32(m.grav[1]), dm_f32(m.grav[2])});
  ImGui::SameLine();
  if (ImGui::Button("Restore captured gravity"))
    call(m, "native.gravity_reset");
  const float presets[][3] = {
      {0, 0, 0}, {0, -1.62f, 0}, {0, 9.8f, 0}, {8, -9.8f, 0}};
  const char *labels[] = {"Zero G", "Moon G", "Reverse G", "Sideways gravity"};
  for (int i = 0; i < 4; i++) {
    if (i)
      ImGui::SameLine();
    if (ImGui::Button(labels[i])) {
      std::copy(presets[i], presets[i] + 3, m.grav);
      call(m, "native.gravity",
           {dm_f32(m.grav[0]), dm_f32(m.grav[1]), dm_f32(m.grav[2])});
    }
  }
  ImGui::SeparatorText("Rigid-body inspector and alterations");
  int bodies = (int)query(m, "native.body_count").as.i32;
  ImGui::Text("%d bodies. No automatic player/car identification is claimed.",
              bodies);
  ImGui::InputInt("Body index", &m.body);
  if (ImGui::Button("Find first dynamic body")) {
    for (int i = 0; i < bodies && i < 16384; i++)
      if (query(m, "native.body_inverse_mass", {dm_i32(i)}).as.f32 > 0) {
        m.body = i;
        break;
      }
  }
  if (m.body >= 0 && m.body < bodies) {
    DMValue b = dm_i32(m.body);
    ImGui::Text("Position %.3f %.3f %.3f | inverse mass %.5g",
                query(m, "native.body_x", {b}).as.f32,
                query(m, "native.body_y", {b}).as.f32,
                query(m, "native.body_z", {b}).as.f32,
                query(m, "native.body_inverse_mass", {b}).as.f32);
    ImGui::Text("Velocity %.3f %.3f %.3f",
                query(m, "native.body_vx", {b}).as.f32,
                query(m, "native.body_vy", {b}).as.f32,
                query(m, "native.body_vz", {b}).as.f32);
    ImGui::Text("Spin %.3f %.3f %.3f rad/s | friction %.3f | bounce %.3f",
                query(m, "native.body_wx", {b}).as.f32,
                query(m, "native.body_wy", {b}).as.f32,
                query(m, "native.body_wz", {b}).as.f32,
                query(m, "native.body_friction_value", {b}).as.f32,
                query(m, "native.body_bounce_value", {b}).as.f32);
    ImGui::InputFloat3("Linear velocity", m.velocity);
    if (ImGui::Button("Set body velocity"))
      call(m, "native.body_velocity",
           {b, dm_f32(m.velocity[0]), dm_f32(m.velocity[1]),
            dm_f32(m.velocity[2])});
    ImGui::InputFloat3("Angular velocity (rad/s)", m.spin);
    if (ImGui::Button("Set body spin"))
      call(m, "native.body_spin",
           {b, dm_f32(m.spin[0]), dm_f32(m.spin[1]), dm_f32(m.spin[2])});
    ImGui::InputFloat3("Force accumulator increment", m.force);
    if (ImGui::Button("Add body force"))
      call(m, "native.body_force",
           {b, dm_f32(m.force[0]), dm_f32(m.force[1]), dm_f32(m.force[2])});
    ImGui::SliderFloat("Friction", &m.friction, 0, 10);
    if (ImGui::Button("Set body friction"))
      call(m, "native.body_friction", {b, dm_f32(m.friction)});
    ImGui::SliderFloat("Restitution", &m.bounce, 0, 1);
    if (ImGui::Button("Set body bounce"))
      call(m, "native.body_bounce", {b, dm_f32(m.bounce)});
  }
  if (ImGui::Button("Stop all dynamic motion"))
    call(m, "native.stop_motion");
  ImGui::SliderFloat("World lift velocity increment", &m.lift, -30, 30);
  if (ImGui::Button("Lift all dynamic bodies"))
    call(m, "native.world_lift", {dm_f32(m.lift)});
  ImGui::EndDisabled();
  ImGui::EndDisabled();
}
static void api_page(Menu &m) {
  uint32_t count = m.api->export_count(m.ctx);
  ImGui::Text("%u checked entries; original game exports + native gameplay API",
              count);
  ImGui::InputText("Filter exports", m.filter, sizeof(m.filter));
  ImGui::BeginChild("export-list", ImVec2(0, 185), ImGuiChildFlags_Borders);
  for (uint32_t i = 0; i < count; i++) {
    const DMExport *e = m.api->export_at(m.ctx, i);
    if (!e || (*m.filter && !std::strstr(e->name, m.filter)))
      continue;
    if (ImGui::Selectable(e->name, m.selected_export == (int)i)) {
      m.selected_export = (int)i;
      std::memset(m.args, 0, sizeof(m.args));
      m.result.clear();
      m.core_advanced = false;
    }
  }
  ImGui::EndChild();
  const DMExport *e = m.api->export_at(m.ctx, (uint32_t)m.selected_export);
  if (!e)
    return;
  ImGui::Text("%s -> %s | %u parameters | flags 0x%x", e->name, type(e->result),
              e->argument_count, e->flags);
  bool native = std::strncmp(e->name, "native.", 7) == 0;
  bool understood = native || !std::strcmp(e->name, "get_app_version") ||
                    !std::strcmp(e->name, "get_app_inited") ||
                    !std::strcmp(e->name, "app_pause") ||
                    !std::strcmp(e->name, "app_resume");
  ImGui::TextWrapped(
      "Exact machine types are known; arbitrary core parameters, pointer "
      "ownership and side effects are NOT automatically known. Prefer the "
      "Gameplay tab. Host-owned exports always remain blocked.");
  if (!understood)
    ImGui::Checkbox("Advanced: I understand this core export's semantics",
                    &m.core_advanced);
  for (uint32_t i = 0; i < e->argument_count; i++) {
    char label[64];
    std::snprintf(label, sizeof(label), "Argument %u (%s)", i,
                  type(e->arguments[i]));
    m.args[i].type = e->arguments[i];
    ImGuiDataType t = e->arguments[i] == DM_I32   ? ImGuiDataType_U32
                      : e->arguments[i] == DM_I64 ? ImGuiDataType_U64
                      : e->arguments[i] == DM_F32 ? ImGuiDataType_Float
                                                  : ImGuiDataType_Double;
    ImGui::InputScalar(label, t, &m.args[i].as);
  }
  ImGui::BeginDisabled((e->flags & DM_EXPORT_HOST_ONLY) ||
                       (!understood && !m.core_advanced));
  if (ImGui::Button("Invoke checked API call")) {
    DMValue result = {};
    DMStatus s =
        m.api->call(m.ctx, e->name, m.args, e->argument_count, &result);
    status(m, e->name, s);
    if (!s)
      m.result = value(result);
    if (!s && !std::strcmp(e->name, "free") && e->argument_count == 1) {
      // An explicitly requested advanced free must not leave an owned block
      // queued for another free on workbench shutdown.
      m.allocations.erase(
          std::remove_if(m.allocations.begin(), m.allocations.end(),
                         [&](const Allocation &block) {
                           return block.offset == m.args[0].as.i32;
                         }),
          m.allocations.end());
      m.selected_allocation = -1;
    }
  }
  ImGui::EndDisabled();
  ImGui::TextWrapped("Last result: %s", m.result.c_str());
}
static void assets_page(Menu &m) {
  ImGui::TextWrapped("Effective packed bytes, including enabled overrides. "
                     "Compressed game/block/db payloads remain compressed "
                     "here; use native:mod asset for decoded editing.");
  ImGui::InputText("Filter assets", m.asset_filter, sizeof(m.asset_filter));
  ImGui::BeginChild("asset-list", ImVec2(0, 200), ImGuiChildFlags_Borders);
  for (uint32_t i = 0; i < m.api->asset_count(m.ctx); i++) {
    const char *path = m.api->asset_path(m.ctx, i);
    if (path && (!*m.asset_filter || std::strstr(path, m.asset_filter)))
      if (ImGui::Selectable(path, m.selected_asset == (int)i))
        m.selected_asset = (int)i;
  }
  ImGui::EndChild();
  const char *path = m.api->asset_path(m.ctx, (uint32_t)m.selected_asset);
  if (path && ImGui::Button("Read effective asset")) {
    size_t size = 0;
    DMStatus s = m.api->asset_read(m.ctx, path, nullptr, 0, &size);
    if (!s) {
      m.preview.resize(size);
      s = m.api->asset_read(m.ctx, path, m.preview.data(), m.preview.size(),
                            &size);
    }
    status(m, "asset_read", s);
  }
  bytes_view(m.preview);
  if (ImGui::Button("Copy preview to own storage key 'asset-dump'"))
    status(m, "storage_write(asset-dump)",
           m.api->storage_write(m.ctx, "asset-dump", m.preview.data(),
                                m.preview.size()));
}
static void memory_page(Menu &m) {
  ImGui::TextWrapped(
      "Offsets, never host pointers. Bounds checks cannot prove semantic "
      "safety. Default writes and frees are restricted to allocations made by "
      "this menu. Arbitrary writes require advanced confirmation.");
  ImGui::InputInt("Allocation bytes", &m.allocation_size);
  if (ImGui::Button("Allocate owned block")) {
    uint32_t p = 0;
    DMStatus s = DM_EBOUNDS;
    if (m.allocation_size > 0 && m.allocation_size <= 1024 * 1024 &&
        m.allocations.size() < 64) {
      s = m.api->allocate(m.ctx, (uint32_t)m.allocation_size, &p);
      if (!s) {
        m.allocations.push_back({p, (uint32_t)m.allocation_size});
        m.selected_allocation = (int)m.allocations.size() - 1;
        m.address = p;
        std::vector<unsigned char> zeros(m.allocation_size);
        s = m.api->memory_write(m.ctx, p, zeros.data(), zeros.size());
      }
    }
    status(m, "allocate + initialize", s);
  }
  for (size_t i = 0; i < m.allocations.size(); i++) {
    char label[96];
    std::snprintf(label, sizeof(label), "%u: offset %u, %u bytes", (unsigned)i,
                  m.allocations[i].offset, m.allocations[i].size);
    if (ImGui::Selectable(label, m.selected_allocation == (int)i)) {
      m.selected_allocation = (int)i;
      m.address = m.allocations[i].offset;
    }
  }
  if (m.selected_allocation >= 0 &&
      (size_t)m.selected_allocation < m.allocations.size() &&
      ImGui::Button("Release selected owned allocation")) {
    DMStatus s =
        m.api->release(m.ctx, m.allocations[m.selected_allocation].offset);
    status(m, "release", s);
    if (!s) {
      m.allocations.erase(m.allocations.begin() + m.selected_allocation);
      m.selected_allocation = -1;
    }
  }
  if (ImGui::InputScalar("Guest offset", ImGuiDataType_U32, &m.address))
    m.confirm_write = false;
  ImGui::InputInt("Read bytes (1..2048)", &m.bytes);
  if (ImGui::Button("Read guest memory")) {
    DMStatus s = DM_EBOUNDS;
    if (m.bytes > 0 && m.bytes <= 2048) {
      m.preview.resize(m.bytes);
      s = m.api->memory_read(m.ctx, m.address, m.preview.data(),
                             m.preview.size());
      if (s)
        m.preview.clear();
    }
    status(m, "memory_read", s);
  }
  bytes_view(m.preview);
  if (ImGui::InputTextMultiline("Hex byte pairs", m.hex, sizeof(m.hex),
                                ImVec2(0, 80)))
    m.confirm_write = false;
  ImGui::Checkbox("Advanced raw write (can crash or corrupt saves)",
                  &m.advanced);
  if (m.advanced)
    ImGui::Checkbox("I confirm this offset and byte range are intentional",
                    &m.confirm_write);
  if (ImGui::Button("Write hex bytes")) {
    std::vector<unsigned char> data;
    DMStatus s = DM_EINVAL;
    if (parse_hex(m.hex, data) && !data.empty()) {
      bool owned = false;
      for (const auto &a : m.allocations)
        if (m.address >= a.offset &&
            uint64_t(m.address) + data.size() <= uint64_t(a.offset) + a.size)
          owned = true;
      s = owned || (m.advanced && m.confirm_write)
              ? m.api->memory_write(m.ctx, m.address, data.data(), data.size())
              : DM_EPROTECTED;
    }
    status(m, "memory_write", s);
  }
}
static void storage_page(Menu &m) {
  ImGui::TextWrapped(
      "This menu has its own native-menu storage namespace. It does not "
      "impersonate plugins or overwrite browser saves. Settings are read-only "
      "snapshots; builtin keys: menu_key, profile.");
  ImGui::InputText("Setting key", m.setting, sizeof(m.setting));
  if (ImGui::Button("Query setting")) {
    size_t n = 0;
    DMStatus s = m.api->setting(m.ctx, m.setting, nullptr, 0, &n);
    if (!s) {
      std::vector<char> data(n);
      s = m.api->setting(m.ctx, m.setting, data.data(), data.size(), &n);
      if (!s)
        m.result = data.data();
    }
    status(m, "setting", s);
  }
  ImGui::TextWrapped("Setting result: %s", m.result.c_str());
  ImGui::InputText("Storage key", m.key, sizeof(m.key));
  ImGui::InputTextMultiline("UTF-8 payload", m.text, sizeof(m.text),
                            ImVec2(0, 110));
  if (ImGui::Button("Write text blob"))
    status(m, "storage_write",
           m.api->storage_write(m.ctx, m.key, m.text, std::strlen(m.text)));
  ImGui::SameLine();
  if (ImGui::Button("Read blob")) {
    size_t n = 0;
    DMStatus s = m.api->storage_read(m.ctx, m.key, nullptr, 0, &n);
    if (!s) {
      m.preview.resize(n);
      s = m.api->storage_read(m.ctx, m.key, m.preview.data(), m.preview.size(),
                              &n);
      if (!s && n < sizeof(m.text)) {
        if (n)
          std::memcpy(m.text, m.preview.data(), n);
        m.text[n] = 0;
      }
    }
    status(m, "storage_read", s);
  }
  ImGui::SameLine();
  if (ImGui::Button("Remove key..."))
    ImGui::OpenPopup("Confirm removal");
  if (ImGui::BeginPopupModal("Confirm removal", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Remove native-menu/%s?", m.key);
    if (ImGui::Button("Remove")) {
      status(m, "storage_remove", m.api->storage_remove(m.ctx, m.key));
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  bytes_view(m.preview);
}
static void messages_page(Menu &m) {
  ImGui::TextWrapped(
      "Queued for the next host tick; publishing is not a synchronous "
      "callback. Plugins and this workbench receive the same broker messages.");
  ImGui::InputText("Topic", m.topic, sizeof(m.topic));
  ImGui::InputTextMultiline("Message payload", m.payload, sizeof(m.payload),
                            ImVec2(0, 90));
  if (ImGui::Button("Publish"))
    status(m, "publish",
           m.api->publish(m.ctx, m.topic, m.payload, std::strlen(m.payload)));
  lines("messages", m.messages, 300);
}
static void console_page(Menu &m) {
  ImGui::TextWrapped(
      "Built-in command dispatcher, not a shell. help, version, mods, exports "
      "[filter], pause, resume, reward, menu close, api native.time_scale 0.5, "
      "mod <id> <command> [args]. Output also goes to the launching terminal.");
  if (ImGui::InputText("Command", m.command, sizeof(m.command),
                       ImGuiInputTextFlags_EnterReturnsTrue) ||
      ImGui::Button("Execute command"))
    status(m, m.command, mods_command(m.host, m.command));
  if (ImGui::Button("List loaded / disabled mods"))
    status(m, "mods", mods_command(m.host, "mods"));
  ImGui::InputText("Plugin ID", m.mod_id, sizeof(m.mod_id));
  ImGui::InputText("Plugin command + arguments", m.mod_command,
                   sizeof(m.mod_command));
  if (ImGui::Button("Send plugin command")) {
    std::string s = std::string("mod ") + m.mod_id + " " + m.mod_command;
    status(m, s.c_str(), mods_command(m.host, s.c_str()));
  }
  ImGui::TextWrapped("Plugins load only via --mods at startup. No fake hot "
                     "reload, runtime unload, or GUI mod sandbox. Per-plugin "
                     "settings remain owned by that plugin.");
  lines("console-logs", m.logs, 250);
}
static void diagnostics_page(Menu &m, const DMState &s) {
  ImGui::Text("SDK ABI %u | game %u | ready %u | paused %u | %d x %d",
              m.api->abi_version, s.game_version, s.ready, s.paused, s.width,
              s.height);
  ImGui::Text(
      "frames %llu | draws %llu | audio %llu | assets %llu | input %llu",
      (unsigned long long)s.frames, (unsigned long long)s.draw_calls,
      (unsigned long long)s.audio_buffers, (unsigned long long)s.asset_reads,
      (unsigned long long)s.input_events);
  ImGui::Text("Host uptime %.2fs; public API structure %u bytes",
              s.uptime_seconds, m.api->struct_size);
  ImGui::InputText("Window title", m.title, sizeof(m.title));
  if (ImGui::Button("Set native window title"))
    status(m, "window_title", m.api->window_title(m.ctx, m.title));
  ImGui::InputText("Log text", m.log_text, sizeof(m.log_text));
  if (ImGui::Button("Log via SDK"))
    m.api->log(m.ctx, m.log_text);
  if (ImGui::CollapsingHeader("All API status codes"))
    for (int i = 0; i >= DM_EPROTECTED; i--)
      ImGui::Text("%d: %s", i, m.api->status_string((DMStatus)i));
  if (ImGui::CollapsingHeader("Recent physical input / UI capture"))
    lines("input-log", m.inputs, 160);
  if (ImGui::Button("Clear logs"))
    m.logs.clear();
  lines("diagnostic-logs", m.logs, 230);
  ImGui::TextWrapped(
      "Raw memory, arbitrary export calls and trusted .so plugins can still "
      "crash the process. Layout-dependent gameplay tools fail closed for "
      "edited executables; this is not a security sandbox.");
}
static bool imgui_backend_init(w2c_a *h) {
#ifdef DM_DESKTOP_GL
  return ImGui_ImplSDL2_InitForOpenGL(h->window, h->context) &&
         ImGui_ImplOpenGL2_Init();
#else
  return ImGui_ImplSDL2_InitForOpenGL(h->window, h->context) &&
         ImGui_ImplOpenGL3_Init("#version 100");
#endif
}
static void imgui_backend_new_frame(void) {
#ifdef DM_DESKTOP_GL
  ImGui_ImplOpenGL2_NewFrame();
#else
  ImGui_ImplOpenGL3_NewFrame();
#endif
}
static void imgui_backend_render(void) {
#ifdef DM_DESKTOP_GL
  ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
#else
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}
static void imgui_backend_shutdown(void) {
#ifdef DM_DESKTOP_GL
  ImGui_ImplOpenGL2_Shutdown();
#else
  ImGui_ImplOpenGL3_Shutdown();
#endif
}
extern "C" void menu_init(w2c_a *h, bool open) {
  auto *m = new Menu;
  m->host = h;
  m->api = mods_api();
  m->ctx = mods_builtin_context(h);
  h->menu = m;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::GetIO().LogFilename = nullptr;
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  if (h->menu_test)
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
  ImGui::StyleColorsDark();
  ImGui::GetStyle().FrameRounding = 4;
  if (!imgui_backend_init(h))
    fail("Native ImGui initialization failed");
  menu_set_open(h, open);
}
extern "C" void menu_set_open(w2c_a *h, bool open) {
  Menu *m = get(h);
  if (!m)
    return;
  if (open && !m->open)
    platform_release_keys(h);
  m->open = open;
}
extern "C" bool menu_is_open(w2c_a *h) {
  Menu *m = get(h);
  return m && m->open;
}
extern "C" DMStatus menu_select_tab(w2c_a *h, const char *name) {
  Menu *m = get(h);
  if (!m)
    return DM_ENOTREADY;
  const char *names[] = {"gameplay", "api",      "assets", "memory",
                         "storage",  "messages", "mods",   "diagnostics"};
  for (int i = 0; i < 8; i++)
    if (!std::strcmp(name, names[i])) {
      m->requested_tab = i;
      menu_set_open(h, true);
      return DM_OK;
    }
  return DM_EINVAL;
}
extern "C" bool menu_event(w2c_a *h, const SDL_Event *event) {
  Menu *m = get(h);
  if (!m)
    return false;
  ImGui_ImplSDL2_ProcessEvent(event);
  if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) &&
      event->key.keysym.sym == SDLK_F2) {
    if (event->type == SDL_KEYDOWN && !event->key.repeat)
      menu_set_open(h, !m->open);
    return true;
  }
  if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE &&
      m->open && !ImGui::GetIO().WantTextInput) {
    menu_set_open(h, false);
    return true;
  }
  bool input = event->type == SDL_KEYDOWN || event->type == SDL_KEYUP ||
               event->type == SDL_TEXTINPUT || event->type == SDL_MOUSEMOTION ||
               event->type == SDL_MOUSEWHEEL ||
               event->type == SDL_MOUSEBUTTONDOWN ||
               event->type == SDL_MOUSEBUTTONUP;
  if (input && event->type != SDL_MOUSEMOTION) {
    char b[128];
    std::snprintf(b, sizeof(b), "SDL event %u | %s", event->type,
                  m->open ? "captured by workbench"
                          : "forwarded to plugin/game routing");
    append(m->inputs, b, 64);
  }
  return input && m->open;
}
extern "C" void menu_render(w2c_a *h) {
  Menu *m = get(h);
  if (!m)
    return;
  if (h->paused) {
    // With no core frame, swapped backbuffers are undefined. Draw a clean
    // paused backdrop rather than accumulating old UI pixels.
    GLfloat color[4];
    GLboolean mask[4];
    bool scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, color);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.06f, 0.07f, 0.09f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(color[0], color[1], color[2], color[3]);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (scissor)
      glEnable(GL_SCISSOR_TEST);
  }
  if (!m->open) {
    // Consume hidden input too, rather than replaying accumulated gameplay
    // clicks/keys into widgets when F2 reopens the workbench.
    imgui_backend_new_frame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGui::EndFrame();
    return;
  }
  imgui_backend_new_frame();
  ImGui_ImplSDL2_NewFrame();
  if (h->menu_test) {
    // Explicit opt-in deterministic UI test. Inject into ImGui after SDL's
    // global-pointer polling; never warp the user's real desktop pointer.
    unsigned t = ++m->test_tick;
    if (t == 30 || t == 31 || t == 40 || t == 41 || t == 50 || t == 51) {
      ImVec2 p = (t == 40 || t == 41) ? m->step_target : m->pause_target;
      ImGui::GetIO().AddMousePosEvent(p.x, p.y);
      ImGui::GetIO().AddMouseButtonEvent(0, t % 10 == 0);
    }
    if (t == 34 && !h->paused)
      fail("Workbench UI pause-button test failed");
    if (t == 39)
      m->test_frames = h->frames;
    if (t == 44 && (!h->paused || h->frames != m->test_frames + 1))
      fail("Workbench UI frame-step test failed");
    if (t == 54 && h->paused)
      fail("Workbench UI resume-button test failed");
    if (t >= 60 && t <= 120 && t % 10 == 0)
      m->requested_tab = (int)((t - 60) / 10 + 1);
    if (t == 135) {
      if (m->test_tabs != 255)
        fail("Workbench UI tab coverage failed");
      SDL_Event toggle = {};
      toggle.type = SDL_KEYDOWN;
      toggle.key.keysym.sym = SDLK_F2;
      if (!menu_event(h, &toggle) || m->open)
        fail("Workbench F2 close test failed");
      toggle.key.repeat = 1;
      if (!menu_event(h, &toggle) || m->open)
        fail("Workbench F2 repeat-capture test failed");
      toggle.type = SDL_KEYUP;
      if (!menu_event(h, &toggle) || m->open)
        fail("Workbench F2 release-capture test failed");
      toggle.type = SDL_KEYDOWN;
      toggle.key.repeat = 0;
      if (!menu_event(h, &toggle) || !m->open)
        fail("Workbench F2 reopen test failed");
      fprintf(stderr, "Native workbench self-test passed: real "
                      "pause/step/resume buttons, 8 tabs.\n");
    }
  }
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(
      ImVec2(std::min(1000, h->width - 40), std::min(920, h->height - 40)),
      ImGuiCond_FirstUseEver);
  // Tiling window managers can resize the SDL window after initialization.
  // Keep the right-side controls/scrollbar reachable after that resize.
  ImVec2 available(std::max(1, h->width - 40), std::max(1, h->height - 40));
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(std::min(320.0f, available.x), std::min(240.0f, available.y)),
      available);
  bool open = m->open;
  if (ImGui::Begin("Drive Mad | Native Mod Workbench (F2)", &open)) {
    if (h->menu_test) {
      ImVec2 size = ImGui::GetWindowSize(), pos = ImGui::GetWindowPos();
      if (size.x + pos.x > h->width || size.y + pos.y > h->height)
        fail("Workbench UI viewport bounds test failed");
    }
    DMState state = {};
    state.struct_size = sizeof(state);
    DMStatus s = m->api->state(m->ctx, &state);
    if (s)
      ImGui::Text("State unavailable: %s", m->api->status_string(s));
    ImGui::TextWrapped("%s", m->status.c_str());
    if (ImGui::BeginTabBar("workbench-tabs",
                           ImGuiTabBarFlags_FittingPolicyScroll)) {
      if (ImGui::BeginTabItem(
              "Gameplay", nullptr,
              m->requested_tab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 1;
        gameplay_page(*m, state);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "API", nullptr,
              m->requested_tab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 2;
        api_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Assets", nullptr,
              m->requested_tab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 4;
        assets_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Memory", nullptr,
              m->requested_tab == 3 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 8;
        memory_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Storage", nullptr,
              m->requested_tab == 4 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 16;
        storage_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Messages", nullptr,
              m->requested_tab == 5 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 32;
        messages_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Mods / Console", nullptr,
              m->requested_tab == 6 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 64;
        console_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Diagnostics", nullptr,
              m->requested_tab == 7 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 128;
        diagnostics_page(*m, state);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
      m->requested_tab = -1;
    }
  }
  ImGui::End();
  menu_set_open(h, open);
  ImGui::Render();
  imgui_backend_render();
}
extern "C" void menu_log(w2c_a *h, const char *sender, const char *text) {
  Menu *m = get(h);
  if (m)
    append(m->logs, std::string(sender) + ": " + text);
}
extern "C" void menu_message(w2c_a *h, const char *sender, const char *topic,
                             const void *data, size_t size) {
  Menu *m = get(h);
  if (!m)
    return;
  std::string payload;
  for (size_t i = 0; i < std::min(size, size_t(256)); i++) {
    unsigned char c = static_cast<const unsigned char *>(data)[i];
    payload += (c >= 32 && c < 127) ? (char)c : '.';
  }
  append(m->messages, std::string(sender) + " / " + topic + " [" +
                          std::to_string(size) + " bytes] " + payload);
}
extern "C" void menu_close(w2c_a *h) {
  Menu *m = get(h);
  if (!m)
    return;
  for (const auto &a : m->allocations)
    m->api->release(m->ctx, a.offset);
  imgui_backend_shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  delete m;
  h->menu = nullptr;
}
