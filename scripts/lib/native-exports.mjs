const types = { void: "VOID", u32: "I32", u64: "I64", f32: "F32", f64: "F64" };
const protectedNames = new Set([
  "main",
  "app_init",
  "user_accepted_and_clicked",
  "app_terminate_if_necessary",
  "webaudio_fill",
  "storage_write",
  "opengl_resume",
]);
export function nativeExports(header, symbols) {
  const exports = [];
  for (const match of header.matchAll(
    /\/\* export: '([^']+)' \*\/\s+(void|u32|u64|f32|f64) (w2c_game_\w+)\(([^)]*)\);/g,
  )) {
    const name = symbols.exports[match[1]];
    if (!name) continue;
    const parameters = match[4]
      .split(",")
      .slice(1)
      .map((arg) => arg.trim().split(/\s+/)[0]);
    if (parameters.length > 16 || parameters.some((type) => !types[type]))
      throw new Error(`Unsupported native SDK signature: ${name}`);
    exports.push({
      name,
      cName: match[3],
      parameters,
      result: match[2],
      flags: protectedNames.has(name)
        ? 2
        : ["get_app_version", "malloc", "free"].includes(name)
          ? 1
          : 0,
    });
  }
  exports.sort((a, b) => a.name.localeCompare(b.name, "en"));
  if (exports.length !== Object.keys(symbols.exports).length)
    throw new Error("Native SDK export discovery is incomplete");
  const table = exports.map(
    (item) =>
      `{${JSON.stringify(item.name)}, ${item.flags}, ${item.parameters.length}, {${item.parameters.map((type) => `DM_${types[type]}`).join(",") || "DM_VOID"}}, DM_${types[item.result]}}`,
  );
  const cases = exports.map((item, index) => {
    if (item.name === "level_select_menu_start_level")
      return `case ${index}: return gameplay_call(h, "native.start_level", args, count, result);`;
    const call =
      item.name === "app_pause"
        ? "gameplay_pause(h, true)"
        : item.name === "app_resume"
          ? "gameplay_pause(h, false)"
          : `${item.cName}(&h->game${item.parameters.map((type, i) => `, args[${i}].as.${types[type].toLowerCase()}`).join("")})`;
    return `case ${index}: ${item.result === "void" ? call : `result->as.${types[item.result].toLowerCase()} = ${call}`}; break;`;
  });
  return {
    exports,
    source: `/* Generated typed SDK dispatch; do not edit. */
#include "host.h"
#include <string.h>
const DMExport dm_exports[] = {${table.join(",\n")}};
const uint32_t dm_export_count = ${exports.length};
DMStatus mods_game_call(struct w2c_a *h, const char *name, const DMValue *args,
                       uint32_t count, DMValue *result) {
  if (!name || (count && !args)) return DM_EINVAL;
  for (uint32_t i=0; i<dm_export_count; i++) {
    const DMExport *entry=&dm_exports[i];
    if (strcmp(name, entry->name)) continue;
    if (entry->flags & DM_EXPORT_HOST_ONLY) return DM_EPROTECTED;
    if (!mods_ready(h) && !(entry->flags & DM_EXPORT_EARLY)) return DM_ENOTREADY;
    if (count != entry->argument_count) return DM_EABI;
    for (uint32_t a=0; a<count; a++)
      if (args[a].type != entry->arguments[a]) return DM_EABI;
    DMValue ignored={0};
    if (!result) result=&ignored;
    memset(result, 0, sizeof(*result));
    result->type=entry->result;
    switch(i) {${cases.join("\n")}}
    return DM_OK;
  }
  return DM_ENOTFOUND;
}
`,
  };
}
