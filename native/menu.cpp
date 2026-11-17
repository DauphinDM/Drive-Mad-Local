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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <png.h>
#include <string>
#include <vector>

extern const unsigned char dm_ui_font[];
extern const unsigned int dm_ui_font_size;

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
  std::string status = "Ready. F2 toggles this mod menu.", result;
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
  GLuint atlas_texture = 0;
  int atlas_width = 0, atlas_height = 0;
  ImFont *font = nullptr, *heading_font = nullptr;
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
struct AtlasIcon {
  float u0, v0, u1, v1;
};
static AtlasIcon atlas_uv(const Menu &m, float x, float y, float w, float h) {
  return {x / m.atlas_width, y / m.atlas_height, (x + w) / m.atlas_width,
          (y + h) / m.atlas_height};
}
// A pressable game-atlas sprite. The widget is a real ImGui item, so hover,
// tooltips, keyboard navigation and BeginDisabled behave normally.
static bool sprite_button(Menu &m, const char *id, const AtlasIcon &icon,
                          ImVec2 size, const char *tooltip) {
  if (!m.atlas_texture)
    return false;
  ImGui::PushID(id);
  bool pressed = ImGui::InvisibleButton("##sprite", size);
  bool hovered = ImGui::IsItemHovered();
  bool active = ImGui::IsItemActive();
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  ImDrawList *draw = ImGui::GetWindowDrawList();
  ImU32 fill = ImGui::GetColorU32(active    ? ImGuiCol_ButtonActive
                                  : hovered ? ImGuiCol_ButtonHovered
                                            : ImGuiCol_Button);
  draw->AddRectFilled(a, b, fill, 7.0f);
  draw->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_Border), 7.0f, 0, 2.0f);
  float pad = 5;
  draw->AddImage(
      (ImTextureID)(intptr_t)m.atlas_texture, ImVec2(a.x + pad, a.y + pad),
      ImVec2(b.x - pad, b.y - pad), ImVec2(icon.u0, icon.v0),
      ImVec2(icon.u1, icon.v1),
      IM_COL32(255, 255, 255, (int)(ImGui::GetStyle().Alpha * 255.0f)));
  if (hovered && tooltip)
    ImGui::SetTooltip("%s", tooltip);
  ImGui::PopID();
  return pressed;
}
static void icon_bar(Menu &m, const DMState &state) {
  if (!m.atlas_texture)
    return;
  const ImVec2 button(36, 32);
  if (sprite_button(m, "quick-pause",
                    atlas_uv(m, state.paused ? 142 : 1808,
                             state.paused ? 404 : 398, 100, 100),
                    button,
                    state.paused ? "Resume the game" : "Pause the game"))
    call(m, "native.pause", {dm_i32(!state.paused)});
  ImGui::SameLine();
  ImGui::BeginDisabled(!state.paused);
  if (sprite_button(m, "quick-step", atlas_uv(m, 149, 538, 101, 89), button,
                    "Step one host frame"))
    call(m, "native.step");
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (sprite_button(m, "quick-restart", atlas_uv(m, 400, 536, 97, 92), button,
                    "Restart the current level"))
    call(m, "native.restart");
  ImGui::SameLine();
  if (sprite_button(m, "quick-fun", atlas_uv(m, 1281, 513, 126, 126), button,
                    "Fun experiments"))
    m.requested_tab = 1;
  ImGui::SameLine();
  if (sprite_button(m, "quick-assets", atlas_uv(m, 654, 261, 98, 115), button,
                    "Shipped game assets"))
    m.requested_tab = 3;
  ImGui::SameLine();
  if (sprite_button(m, "quick-mods", atlas_uv(m, 14, 392, 100, 112), button,
                    "Mods and console"))
    m.requested_tab = 7;
  ImGui::SameLine();
  if (sprite_button(m, "quick-help", atlas_uv(m, 926, 411, 66, 86), button,
                    "Diagnostics and status"))
    m.requested_tab = 8;
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
static void fun_gravity(Menu &m, float x, float y, float z) {
  m.grav[0] = x;
  m.grav[1] = y;
  m.grav[2] = z;
  call(m, "native.gravity", {dm_f32(x), dm_f32(y), dm_f32(z)});
}
static void fun_materials(Menu &m, float friction, float bounce) {
  uint32_t bodies = query(m, "native.body_count").as.i32;
  uint32_t changed = 0;
  for (uint32_t i = 0; i < bodies && i < 4096; i++) {
    DMValue args[2] = {dm_i32(i), dm_f32(friction)};
    if (m.api->call(m.ctx, "native.body_friction", args, 2, nullptr) == DM_OK)
      changed++;
    args[1] = dm_f32(bounce);
    m.api->call(m.ctx, "native.body_bounce", args, 2, nullptr);
  }
  m.status = "Fun preset applied to " + std::to_string(changed) +
             " bodies (world, not just the truck).";
  append(m.logs, m.status);
}
static void fun_page(Menu &m, const DMState &state) {
  bool profile = query(m, "native.profile_supported").as.i32 != 0;
  ImGui::TextWrapped("Playful one-shot experiments built from the checked "
                     "native API. These alter the whole simulated world, not "
                     "one identified player car.");
  ImGui::BeginDisabled(!state.ready || !profile);
  ImGui::SeparatorText("Gravity toys");
  if (ImGui::Button("Moon"))
    fun_gravity(m, 0, -1.62f, 0);
  ImGui::SameLine();
  if (ImGui::Button("Zero G"))
    fun_gravity(m, 0, 0, 0);
  ImGui::SameLine();
  if (ImGui::Button("Heavy"))
    fun_gravity(m, 0, -24, 0);
  ImGui::SameLine();
  if (ImGui::Button("Sideways"))
    fun_gravity(m, 9, -9.8f, 0);
  ImGui::SameLine();
  if (ImGui::Button("Gravity roulette")) {
    double t = ImGui::GetTime() * 1.7;
    fun_gravity(m, (float)std::cos(t) * 9, -9.8f, (float)std::sin(t) * 9);
  }
  if (ImGui::Button("Restore original gravity"))
    call(m, "native.gravity_reset");
  ImGui::SeparatorText("World materials");
  if (ImGui::Button("Bouncy castle"))
    fun_materials(m, 0.15f, 0.95f);
  ImGui::SameLine();
  if (ImGui::Button("Ice rink"))
    fun_materials(m, 0, 0.05f);
  ImGui::SameLine();
  if (ImGui::Button("Sticky rubber"))
    fun_materials(m, 2.5f, 0);
  ImGui::SeparatorText("Instant chaos");
  if (ImGui::Button("Launch the world"))
    call(m, "native.world_lift", {dm_f32(14)});
  ImGui::SameLine();
  if (ImGui::Button("Freeze everything"))
    call(m, "native.stop_motion");
  ImGui::SameLine();
  if (ImGui::Button("Restart the level"))
    call(m, "native.restart");
  ImGui::SeparatorText("Time control");
  if (ImGui::Button("Slow motion"))
    call(m, "native.time_scale", {dm_f32(0.35f)});
  ImGui::SameLine();
  if (ImGui::Button("Normal speed"))
    call(m, "native.time_scale", {dm_f32(1)});
  ImGui::SameLine();
  if (ImGui::Button("Turbo"))
    call(m, "native.time_scale", {dm_f32(2.5f)});
  ImGui::EndDisabled();
  ImGui::SeparatorText("Original Versus: recorded ghost races");
  ImGui::TextWrapped(
      "Race downloaded runs using the game's original cars, tracks and race "
      "UI. These are recorded opponents, not a live friend session. Only "
      "original replay downloads are enabled; uploads stay blocked.");
  ImGui::BeginDisabled(!state.ready || !profile);
  if (ImGui::Button("Enable replay downloads and open Versus")) {
    if (versus_enable_online(m.host)) {
      versus_enter(m.host);
      menu_set_open(m.host, false);
    } else {
      m.status = "Versus unavailable or a request is still pending.";
    }
  }
  ImGui::EndDisabled();
  ImGui::TextWrapped("%s", versus_status(m.host));
  ImGui::SeparatorText("Local multiplayer: Couch Chaos");
  ImGui::TextWrapped(
      "The separate Couch Chaos example mod is local hot-seat, not online "
      "racing: two players take turns on the same "
      "level, with a live timer and persistent wins.");
  ImGui::TextWrapped("Copy native/sdk/examples/couch-chaos into "
                     "native/build/mods, then run npm run native:mod -- build "
                     "native/build/mods/couch-chaos. Launch with --mods "
                     "native/build/mods. F7 restarts your run, F8 hands over, "
                     "F9 claims the win.");
  ImGui::BeginDisabled(!profile);
  if (ImGui::Button("Open Mods / Console tab"))
    m.requested_tab = 7;
  ImGui::EndDisabled();
  if (!profile)
    ImGui::TextWrapped("Physics toys are disabled for this executable: the "
                       "stock v166 profile check failed closed.");
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
static ImVec4 rgba(unsigned rgb, float alpha = 1) {
  return ImVec4(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f,
                (rgb & 255) / 255.0f, alpha);
}
// Palette sampled from the game atlas: cream paper, navy ink, cyan sky,
// bubblegum pink and lemon-yellow accents.
static void game_style() {
  ImGui::StyleColorsLight();
  ImGuiStyle &s = ImGui::GetStyle();
  s.WindowRounding = 12;
  s.ChildRounding = 8;
  s.FrameRounding = 7;
  s.PopupRounding = 8;
  s.ScrollbarRounding = 10;
  s.GrabRounding = 7;
  s.TabRounding = 8;
  s.WindowBorderSize = 2;
  s.ChildBorderSize = 2;
  s.FrameBorderSize = 2;
  s.PopupBorderSize = 2;
  s.WindowPadding = ImVec2(14, 12);
  s.FramePadding = ImVec2(10, 6);
  s.ItemSpacing = ImVec2(9, 7);
  s.ItemInnerSpacing = ImVec2(7, 5);
  s.ScrollbarSize = 17;
  s.GrabMinSize = 11;
  ImVec4 *c = s.Colors;
  const ImVec4 ink = rgba(0x25204A), cream = rgba(0xF8F4E8),
               paper = rgba(0xFFFDF6), sky = rgba(0x5BC8F5),
               pink = rgba(0xE85CB4), lemon = rgba(0xFFD84D),
               mint = rgba(0xA8E6A1), soft = rgba(0xE8E1D0);
  c[ImGuiCol_Text] = ink;
  c[ImGuiCol_TextDisabled] = rgba(0x746D8C);
  c[ImGuiCol_WindowBg] = cream;
  c[ImGuiCol_ChildBg] = paper;
  c[ImGuiCol_PopupBg] = paper;
  c[ImGuiCol_Border] = ink;
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 0.72f);
  c[ImGuiCol_FrameBgHovered] = rgba(0xE3F7FF);
  c[ImGuiCol_FrameBgActive] = rgba(0xFFE2F3);
  c[ImGuiCol_TitleBg] = sky;
  c[ImGuiCol_TitleBgActive] = sky;
  c[ImGuiCol_TitleBgCollapsed] = sky;
  c[ImGuiCol_MenuBarBg] = cream;
  c[ImGuiCol_ScrollbarBg] = soft;
  c[ImGuiCol_ScrollbarGrab] = pink;
  c[ImGuiCol_ScrollbarGrabHovered] = rgba(0xF47AC7);
  c[ImGuiCol_ScrollbarGrabActive] = ink;
  c[ImGuiCol_CheckMark] = pink;
  c[ImGuiCol_SliderGrab] = pink;
  c[ImGuiCol_SliderGrabActive] = ink;
  c[ImGuiCol_Button] = lemon;
  c[ImGuiCol_ButtonHovered] = rgba(0xFFEA8F);
  c[ImGuiCol_ButtonActive] = pink;
  c[ImGuiCol_Header] = rgba(0xBFEFFF);
  c[ImGuiCol_HeaderHovered] = rgba(0xFFD9F0);
  c[ImGuiCol_HeaderActive] = pink;
  c[ImGuiCol_Separator] = rgba(0x25204A, 0.45f);
  c[ImGuiCol_SeparatorHovered] = pink;
  c[ImGuiCol_SeparatorActive] = pink;
  c[ImGuiCol_ResizeGrip] = rgba(0xE85CB4, 0.5f);
  c[ImGuiCol_ResizeGripHovered] = pink;
  c[ImGuiCol_ResizeGripActive] = ink;
  c[ImGuiCol_Tab] = rgba(0xD9F4FD);
  c[ImGuiCol_TabHovered] = rgba(0xFFE3F4);
  c[ImGuiCol_TabSelected] = sky;
  c[ImGuiCol_TabDimmed] = soft;
  c[ImGuiCol_TabDimmedSelected] = rgba(0x9FE0FA);
  c[ImGuiCol_PlotLines] = pink;
  c[ImGuiCol_PlotLinesHovered] = ink;
  c[ImGuiCol_PlotHistogram] = mint;
  c[ImGuiCol_PlotHistogramHovered] = pink;
  c[ImGuiCol_TextSelectedBg] = rgba(0xFFD84D, 0.7f);
  c[ImGuiCol_NavHighlight] = pink;
}
static bool stock_atlas(const unsigned char **bytes, size_t *size) {
  // Keep menu icon coordinates pinned to the shipped atlas. A mod may replace
  // default.png for gameplay, but that must not scramble the workbench icons.
  for (size_t i = 0; i < native_asset_count; i++)
    if (!std::strcmp(native_asset_index[i].path, "/assets/atlas/default.png") ||
        !std::strcmp(native_asset_index[i].path, "assets/atlas/default.png")) {
      *bytes = native_assets + native_asset_index[i].offset;
      *size = native_asset_index[i].size;
      return true;
    }
  return false;
}
static bool load_menu_atlas(Menu &m) {
  const unsigned char *bytes = nullptr;
  size_t size = 0;
  if (!stock_atlas(&bytes, &size) || !bytes || size < 8)
    return false;
  png_image image = {};
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&image, bytes, size))
    return false;
  image.format = PNG_FORMAT_RGBA;
  std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
  int width = (int)image.width, height = (int)image.height;
  bool decoded =
      png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr) != 0;
  png_image_free(&image);
  if (!decoded)
    return false;
  GLint previous_texture = 0, alignment = 4;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
  glGenTextures(1, &m.atlas_texture);
  glBindTexture(GL_TEXTURE_2D, m.atlas_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
  glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture);
  m.atlas_width = width;
  m.atlas_height = height;
  return m.atlas_texture != 0;
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
  ImGuiIO &io = ImGui::GetIO();
  ImFontConfig font_config;
  font_config.FontDataOwnedByAtlas = false;
  font_config.OversampleH = 2;
  font_config.OversampleV = 2;
  // Sleek geometric sans, generated from the SIL OFL Space Grotesk family.
  m->font = io.Fonts->AddFontFromMemoryTTF(
      (void *)dm_ui_font, (int)dm_ui_font_size, 18.0f, &font_config,
      io.Fonts->GetGlyphRangesDefault());
  m->heading_font = io.Fonts->AddFontFromMemoryTTF(
      (void *)dm_ui_font, (int)dm_ui_font_size, 30.0f, &font_config,
      io.Fonts->GetGlyphRangesDefault());
  if (!m->font || !m->heading_font)
    fail("Native UI font initialization failed");
  io.FontDefault = m->font;
  game_style();
  if (!imgui_backend_init(h))
    fail("Native ImGui initialization failed");
  if (!load_menu_atlas(*m))
    append(m->logs, "Atlas icons unavailable; text-only menu.");
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
  const char *names[] = {"gameplay", "fun",      "api",  "assets",     "memory",
                         "storage",  "messages", "mods", "diagnostics"};
  for (int i = 0; i < 9; i++)
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
                  m->open ? "captured by mod menu"
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
    if (t >= 60 && t <= 140 && t % 10 == 0)
      m->requested_tab = (int)((t - 60) / 10 + 1);
    if (t == 150) {
      if (!m->atlas_texture)
        fail("Workbench game-atlas icon test failed");
      if (m->test_tabs != 511)
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
                      "pause/step/resume buttons, 9 tabs.\n");
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
  if (ImGui::Begin("Drive Mad | Mod Menu (F2)", &open)) {
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
    if (m->atlas_texture) {
      AtlasIcon mark = atlas_uv(*m, 21, 526, 101, 101);
      ImGui::Image((ImTextureID)(intptr_t)m->atlas_texture, ImVec2(32, 32),
                   ImVec2(mark.u0, mark.v0), ImVec2(mark.u1, mark.v1));
      ImGui::SameLine();
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
    }
    ImGui::PushFont(m->heading_font);
    ImGui::PushStyleColor(ImGuiCol_Text, rgba(0x25204A));
    ImGui::TextUnformatted("DRIVE MAD");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, rgba(0xE85CB4));
    ImGui::TextUnformatted("MOD MENU");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::TextDisabled(
        "F2 closes · game input pauses while this panel is open");
    icon_bar(*m, state);
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
              "Fun", nullptr,
              m->requested_tab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 2;
        fun_page(*m, state);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "API", nullptr,
              m->requested_tab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 4;
        api_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Assets", nullptr,
              m->requested_tab == 3 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 8;
        assets_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Memory", nullptr,
              m->requested_tab == 4 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 16;
        memory_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Storage", nullptr,
              m->requested_tab == 5 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 32;
        storage_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Messages", nullptr,
              m->requested_tab == 6 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 64;
        messages_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Mods / Console", nullptr,
              m->requested_tab == 7 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 128;
        console_page(*m);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(
              "Diagnostics", nullptr,
              m->requested_tab == 8 ? ImGuiTabItemFlags_SetSelected : 0)) {
        m->test_tabs |= 256;
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
  if (m->atlas_texture) {
    GLuint texture = m->atlas_texture;
    glDeleteTextures(1, &texture);
  }
  imgui_backend_shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  delete m;
  h->menu = nullptr;
}
