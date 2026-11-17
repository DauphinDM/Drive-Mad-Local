import { sha256 } from "./modding.mjs";

export const gameplayWasmSha256 =
  "47c6c608201fcfb0b33f8943773b6e61571dd2b17dea39179a8637964453ec66";

// Pinned wasm2c's split-output header declares defined functions in WASM index
// order. Bind the researched teardown routine without depending on debug names
// or altering the game's public exports. Only use under the exact stock profile.
export function gameplayPrivateBindings(header) {
  const declarations = [
    ...header.matchAll(
      /^(void|u32|u64|f32|f64) (w2c_game_\w+)\(([^;\n]+)\);$/gm,
    ),
  ];
  if (declarations.length !== 2302)
    throw new Error("Unexpected stock game's private native function table");
  const cleanup = declarations[288 - 116];
  if (cleanup[1] !== "void" || cleanup[3].replace(/\s/g, "") !== "w2c_game*")
    throw new Error("Unexpected native physics teardown signature");
  return `#include "game.h"\nvoid ${cleanup[2]}(w2c_game*);\n#define game_physics_cleanup ${cleanup[2]}\n`;
}

export function supportsGameplayProfile(wabt, original, compiled) {
  if (sha256(original) !== gameplayWasmSha256) return false;
  function executable(bytes) {
    const module = wabt.readWasm(Uint8Array.from(bytes), {
      readDebugNames: false,
    });
    try {
      module.generateNames();
      module.applyNames();
      // Deterministic module text ignores debug/custom metadata but includes
      // all executable instructions, globals, initializers, exports and data.
      return module.toText({ foldExprs: false, inlineExport: false });
    } finally {
      module.destroy();
    }
  }
  return executable(original) === executable(compiled);
}

export function gameplayExports(source) {
  const types = {
    DM_VOID: "void",
    DM_I32: "u32",
    DM_I64: "u64",
    DM_F32: "f32",
    DM_F64: "f64",
  };
  return [
    ...source.matchAll(
      /ENTRY(_EARLY)?\("([^"]+)",\s*(\d+),\s*(DM_\w+),\s*([^)]*)\)/g,
    ),
  ].map(([, early, name, count, result, args]) => {
    const parameters = Number(count)
      ? args.split(",").map((v) => types[v.trim()])
      : [];
    if (
      !types[result] ||
      parameters.length !== Number(count) ||
      parameters.some((v) => !v)
    )
      throw new Error(`Invalid native gameplay signature: ${name}`);
    return {
      name: `native.${name}`,
      cName: "gameplay_call",
      parameters,
      result: types[result],
      flags: early ? 1 : 0,
    };
  });
}
