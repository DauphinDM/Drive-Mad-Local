import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile, rm } from "node:fs/promises";
import path from "node:path";
import { tmpdir } from "node:os";
import wabtFactory from "wabt";
import {
  analyzeWat,
  moduleForms,
  pseudocodeFunctions,
  externalFunctionABI,
  assertFunctionABI,
} from "../scripts/lib/wat.mjs";
import { splitWorkspace, mergeWorkspace } from "../scripts/lib/workspace.mjs";
import { stripLocalDebugNames } from "../scripts/lib/wasm.mjs";
import { sha256, loadInputs, recoverSymbols } from "../scripts/lib/modding.mjs";

const wat = `(module
  (type $unary (func (param i32) (result i32)))
  (import "env" "echo" (func $import_echo (type $unary)))
  ;; (func $fake) call $fake
  (; outer (; nested (func $also_fake) ;) ;)
  (func $increment (type $unary) (param $value i32) (result i32)
    local.get $value
    i32.const 1
    i32.add
    call $import_echo)
  (func $answer (result i32)
    i32.const 41
    call $increment)
  (memory 1)
  (data (i32.const 0) "(func $not_real) ;; call $not_real")
  (export "answer" (func $answer))
  (export "increm\\65nt" (func $increment))
)`;

async function compile(text) {
  const wabt = await wabtFactory();
  const module = wabt.parseWat("fixture.wat", text);
  try {
    module.resolveNames();
    module.validate();
    return Buffer.from(module.toBinary({ write_debug_names: true }).buffer);
  } finally {
    module.destroy();
  }
}

test("WAT analysis handles strings/comments and resolves signatures and direct callers", () => {
  const functions = analyzeWat(wat);
  assert.equal(functions.length, 3);
  assert.equal(functions[0].imported, true);
  assert.deepEqual(functions[0].signature, {
    params: ["i32"],
    results: ["i32"],
  });
  assert.deepEqual(functions[1].exports, ["increment"]);
  assert.deepEqual(functions[1].calls, ["$import_echo"]);
  assert.deepEqual(functions[1].callers, ["$answer"]);
  assert.deepEqual(functions[2].signature, { params: [], results: ["i32"] });
  assert.deepEqual(functions[2].calls, ["$increment"]);
  assert.equal(
    wat.slice(functions[2].start, functions[2].end).startsWith("(func $answer"),
    true,
  );
  assert.throws(() => moduleForms("(module (; incomplete)"));
  assert.throws(() => moduleForms("(module) (module)"));
  assert.throws(() => moduleForms('(module (data "unterminated))'));
});

test("indirect calls are counted but targets aren't invented", () => {
  const functions = analyzeWat(`(module
    (type $signature (func))
    (func $a)
    (func $b (i32.const 0) (call_indirect (type $signature)))
    (func $c (call $a))
  )`);
  assert.deepEqual(functions[1].calls, []);
  assert.equal(functions[1].indirectCalls, 1);
  assert.deepEqual(functions[0].callers, ["$c"]);
});

test("ABI checks catch type changes that native WebAssembly reflection misses", async () => {
  const original =
    '(module (func $echo (param $value i32) (result i32) local.get $value) (export "e" (func $echo)))';
  const modified = original.replaceAll("i32", "f32");
  const [first, second] = await Promise.all([
    compile(original),
    compile(modified),
  ]);
  assert.deepEqual(
    WebAssembly.Module.exports(new WebAssembly.Module(first)),
    WebAssembly.Module.exports(new WebAssembly.Module(second)),
  );
  assert.throws(
    () =>
      assertFunctionABI(
        externalFunctionABI(original),
        externalFunctionABI(modified),
      ),
    /exports function signatures changed/,
  );
  const renamed = original.replaceAll("$echo", "$some_other_name");
  assert.doesNotThrow(() =>
    assertFunctionABI(
      externalFunctionABI(original),
      externalFunctionABI(renamed),
    ),
  );
  assert.deepEqual(externalFunctionABI(wat).imports[0], {
    module: "env",
    name: "echo",
    signature: { params: ["i32"], results: ["i32"] },
  });
  assert.throws(
    () =>
      assertFunctionABI(
        externalFunctionABI('(module (import "a" "b" (func $f (param i32))))'),
        externalFunctionABI('(module (import "a" "b" (func $f (param f32))))'),
      ),
    /imports function signatures changed/,
  );
});

test("pseudocode is separated using absolute function-index headers", () => {
  const snippets = pseudocodeFunctions(`data d0 = "function not_a_header()";

function answer():int { // func2
  return 42
}

export function f3(a:int) { // func3
}
`);
  assert.equal(snippets.size, 2);
  assert.match(snippets.get(2), /return 42/);
  assert.doesNotMatch(snippets.get(2), /function f3/);
});

test("function workspace preserves untouched source, builds edits, and protects against stale/missing/malformed inputs", async () => {
  const directory = await mkdtemp(path.join(tmpdir(), "drive-mad-functions-"));
  const workspace = path.join(directory, "workspace");
  try {
    const binary = await compile(wat);
    const hash = sha256(binary);
    const manifest = await splitWorkspace(workspace, wat, { wasmSha256: hash });
    assert.equal((await mergeWorkspace(workspace, wat, hash)).wat, wat);
    await assert.rejects(splitWorkspace(workspace, wat), /already exists/);
    const file = path.join(workspace, manifest.functions[2].file);
    const original = await readFile(file, "utf8");
    await writeFile(file, original.replace("i32.const 41", "i32.const 99"));
    const merged = await mergeWorkspace(workspace, wat, hash);
    assert.deepEqual(merged.changed, ["$answer"]);
    const edited = await compile(merged.wat);
    const instance = await WebAssembly.instantiate(edited, {
      env: { echo: (value) => value },
    });
    assert.equal(instance.instance.exports.answer(), 100);
    await assert.rejects(mergeWorkspace(workspace, wat + "\n", hash), /stale/);
    await assert.rejects(
      mergeWorkspace(workspace, wat, "different-wasm"),
      /stale/,
    );
    await writeFile(file, original.replace("$answer", "$renamed"));
    await assert.rejects(
      mergeWorkspace(workspace, wat, hash),
      /unchanged function identifier/,
    );
    await writeFile(file, original + "(func $injected)");
    await assert.rejects(mergeWorkspace(workspace, wat, hash), /exactly one/);
    await rm(file);
    await assert.rejects(mergeWorkspace(workspace, wat, hash), /ENOENT/);
    await assert.rejects(
      mergeWorkspace(path.join(workspace, "functions"), wat, hash),
      /no index.json/,
    );
    assert.equal(
      (await mergeWorkspace(path.join(directory, "absent"), wat, hash))
        .workspace,
      false,
    );
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
});

test("stripping local names preserves function debug names and executable behavior", async () => {
  const original = await compile(wat);
  const stripped = stripLocalDebugNames(original);
  const wabt = await wabtFactory();
  // WABT's JS reader doesn't respect pooled Node Buffer byte offsets.
  const parsed = wabt.readWasm(Uint8Array.from(stripped), {
    readDebugNames: true,
  });
  try {
    const text = parsed.toText({});
    assert.match(text, /\$increment/);
    assert.doesNotMatch(text, /\$value/);
  } finally {
    parsed.destroy();
  }
  const instance = await WebAssembly.instantiate(stripped, {
    env: { echo: (value) => value },
  });
  assert.equal(instance.instance.exports.answer(), 42);
  assert.deepEqual(stripLocalDebugNames(stripped), stripped);
  assert.throws(() => stripLocalDebugNames(Buffer.from("not wasm")), /WASM v1/);
  assert.throws(
    () => stripLocalDebugNames(original.subarray(0, -1)),
    /Truncated/,
  );
});

test("the actual game exposes a complete function/signature index", async () => {
  const { wasm, loader } = await loadInputs();
  const wabt = await wabtFactory();
  const module = wabt.readWasm(wasm, {});
  let text;
  try {
    module.generateNames();
    module.applyNames();
    text = module.toText({ foldExprs: false, inlineExport: false });
  } finally {
    module.destroy();
  }
  const functions = analyzeWat(text);
  assert.equal(functions.filter((item) => item.imported).length, 116);
  const versionName = Object.entries(recoverSymbols(loader).exports).find(
    ([, name]) => name === "get_app_version",
  )[0];
  const version = functions.find((item) => item.exports.includes(versionName));
  assert.deepEqual(version.signature, { params: [], results: ["i32"] });
  assert.deepEqual(version.calls, []);
  const pauseName = Object.entries(recoverSymbols(loader).exports).find(
    ([, name]) => name === "app_pause",
  )[0];
  assert.ok(functions.find((item) => item.exports.includes(pauseName)));
});
