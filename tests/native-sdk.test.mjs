import test from "node:test";
import assert from "node:assert/strict";
import {
  access,
  copyFile,
  mkdtemp,
  mkdir,
  readFile,
  writeFile,
  rm,
  symlink,
} from "node:fs/promises";
import path from "node:path";
import os from "node:os";
import { spawnSync } from "node:child_process";
import { inflateSync } from "node:zlib";
import { root } from "../scripts/lib/modding.mjs";
import { nativeExports } from "../scripts/lib/native-exports.mjs";

const binary = path.join(root, "native/build/drive-mad");
const include = path.join(root, "native/sdk/include");
const cli = path.join(root, "scripts/native-mod.mjs");
const built =
  process.platform === "linux" &&
  (await access(binary).then(
    () => true,
    () => false,
  ));
const skip = !built && "Run npm run native:build first";
function run(command, args, options = {}) {
  const p = spawnSync(command, args, {
    encoding: "utf8",
    timeout: 60000,
    ...options,
  });
  assert.equal(p.status, 0, p.error?.message || p.stdout + p.stderr);
  return p;
}
async function temporary(work) {
  const dir = await mkdtemp(
    path.join(process.env.OPENCODE_TMP_DIR || os.tmpdir(), "drive-mad-sdk-"),
  );
  try {
    return await work(dir);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
}
async function manifest(dir, id, extra = "", library = false) {
  await mkdir(dir, { recursive: true });
  await writeFile(
    path.join(dir, "mod.ini"),
    `abi=1\nid=${id}\nversion=1.0.0\n${library ? "library=mod.so\n" : ""}${extra}`,
  );
}
function game(dir, args = []) {
  return spawnSync(binary, ["--self-test", ...args], {
    cwd: dir,
    env: { ...process.env, DRIVE_MAD_SAVE_DIR: path.join(dir, "save") },
    encoding: "utf8",
    timeout: 30000,
  });
}
function ok(result) {
  assert.equal(
    result.status,
    0,
    result.error?.message || result.stdout + result.stderr,
  );
}
const probe = `
#include <drive_mad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"SDK assertion: %s\\n",#x);exit(9);}} while(0)
static const DMApi *a; static DMContext *c;
static unsigned messages,frames,renders,inputs,ready_count,ready_messages;
static DMStatus load(const DMApi *api,DMContext *ctx,void **userdata) {
  a=api;c=ctx;
  CHECK(a->abi_version==1 && a->struct_size==sizeof(DMApi));
  unsigned count=a->export_count(c), core=0;
  CHECK(count>76 && a->export_at(c,count)==NULL);
  for(unsigned i=0;i<count;i++) if(strncmp(a->export_at(c,i)->name,"native.",7)) core++;
  CHECK(core==76);
  CHECK(a->asset_count(c)==261 && a->asset_path(c,261)==NULL);
  DMValue result;
  CHECK(a->call(c,"get_app_version",NULL,0,&result)==DM_OK && result.type==DM_I32 && result.as.i32==166);
  CHECK(a->call(c,"not-an-export",NULL,0,NULL)==DM_ENOTFOUND);
  CHECK(a->call(c,"app_init",NULL,0,NULL)==DM_EPROTECTED);
  CHECK(a->call(c,"app_pause",NULL,0,NULL)==DM_ENOTREADY);
  DMValue wrong=dm_f32(1);
  CHECK(a->call(c,"malloc",&wrong,1,NULL)==DM_EABI);
  DMState state={.struct_size=sizeof(state)};
  CHECK(a->state(c,&state)==DM_OK && !state.ready && state.game_version==166);
  uint32_t p;
  CHECK(a->allocate(c,8,&p)==DM_OK);
  CHECK(a->memory_write(c,p,"testing",8)==DM_OK);
  char bytes[32]={0}; size_t n=0;
  CHECK(a->memory_read(c,p,bytes,8)==DM_OK && !strcmp(bytes,"testing"));
  CHECK(a->memory_read(c,UINT32_MAX,bytes,8)==DM_EBOUNDS);
  CHECK(a->memory_write(c,0,NULL,1)==DM_EINVAL);
  CHECK(a->release(c,p)==DM_OK);
  CHECK(a->setting(c,"answer",NULL,0,&n)==DM_OK && n==3);
  CHECK(a->setting(c,"answer",bytes,1,&n)==DM_EBOUNDS && n==3);
  CHECK(a->setting(c,"answer",bytes,sizeof(bytes),&n)==DM_OK && !strcmp(bytes,"42"));
  CHECK(a->setting(c,"absent",bytes,sizeof(bytes),&n)==DM_ENOTFOUND);
  CHECK(a->storage_write(c,"../escape","x",1)==DM_EINVAL);
  unsigned launches=0;
  DMStatus read=a->storage_read(c,"launches",&launches,sizeof(launches),&n);
  CHECK(read==DM_OK || read==DM_ENOTFOUND);
  launches++;
  CHECK(a->storage_write(c,"launches",&launches,sizeof(launches))==DM_OK);
  fprintf(stderr,"probe launches=%u\\n",launches);
  CHECK(a->storage_write(c,"delete-me","xyz",3)==DM_OK);
  CHECK(a->storage_read(c,"delete-me",NULL,0,&n)==DM_OK && n==3);
  CHECK(a->storage_remove(c,"delete-me")==DM_OK);
  CHECK(a->storage_read(c,"delete-me",bytes,sizeof(bytes),&n)==DM_ENOTFOUND);
  CHECK(a->asset_read(c,"/assets/atlas/default.png",NULL,0,&n)==DM_OK && n>8);
  CHECK(a->asset_read(c,"/assets/atlas/default.png",bytes,sizeof(bytes),&n)==DM_EBOUNDS);
  CHECK(a->publish(c,"hello","test",4)==DM_OK);
  CHECK(!strcmp(a->status_string(DM_EABI),"signature/ABI mismatch"));
  a->log(c,"probe load complete");
  return DM_OK;
}
static void ready(void *u) { ready_count++; CHECK(dm_call0(a,c,"app_resume")==DM_OK); CHECK(a->publish(c,"ready",NULL,0)==DM_OK); }
static void frame(void *u,const DMFrame *f) {frames++;CHECK(f->struct_size==sizeof(*f));}
static void render(void *u,const DMFrame *f) {renders++;}
static int input(void *u,const DMInput *e) {inputs++;return 0;}
static void message(void *u,const char *sender,const char *topic,const void *data,size_t n) {
  if(!strcmp(topic,"ready")) { CHECK(frames>0 && n==0 && !strcmp(sender,"probe"));ready_messages++;return; }
  CHECK(!strcmp(sender,"probe") && !strcmp(topic,"hello") && n==4 && !memcmp(data,"test",4));messages++;
  if(messages==1) CHECK(a->publish(c,"hello","test",4)==DM_OK);
}
static DMStatus command(void *u,const char *name,const char *args) {
  if(!strcmp(name,"ping") && !strcmp(args,"some plain text")) {a->log(c,"command ok");return DM_OK;}
  return DM_ENOTFOUND;
}
static void unload(void *u) {
  CHECK(messages==(ready_count?2u:1u)); /* Publishing in a callback isn't recursive. */
  CHECK(ready_messages==(ready_count?1u:0u)); /* on_ready publications also wait a tick. */
  if(ready_count) CHECK(ready_count==1 && frames>0 && renders>0 && inputs>0);
  fprintf(stderr,"probe unload ready=%u frames=%u renders=%u inputs=%u\\n",ready_count,frames,renders,inputs);
}
DM_PUBLIC const DMMod *dm_mod_query(uint32_t abi) {
  static const DMMod d={.abi_version=1,.struct_size=sizeof(DMMod),.id="probe",.name="Probe",.version="1.0.0",
  .on_load=load,.on_ready=ready,.on_frame=frame,.on_render=render,.on_input=input,
  .on_command=command,.on_message=message,.on_unload=unload};
  return abi==1?&d:NULL;
}`;
async function buildProbe(dir, source = probe) {
  await manifest(dir, "probe", "", true);
  await writeFile(path.join(dir, "settings.ini"), "answer=42\n");
  await writeFile(path.join(dir, "mod.c"), source);
  run(process.execPath, [cli, "build", dir]);
}

test("SDK dispatcher derives machine signatures and lifecycle protections from native header", () => {
  const header =
    "/* export: 'x' */\nu32 w2c_game_x(w2c_game*, f32);\n/* export: 'y' */\nvoid w2c_game_y(w2c_game*);";
  const result = nativeExports(header, {
    exports: { x: "get_app_version", y: "main" },
  });
  assert.equal(result.exports.length, 2);
  assert.deepEqual(result.exports[0].parameters, ["f32"]);
  assert.equal(result.exports[1].flags, 2);
  assert.match(result.source, /args\[0\]\.as\.f32/);
  assert.throws(
    () => nativeExports("", { exports: { x: "foo" } }),
    /incomplete/,
  );
});
test(
  "native SDK loads real plugins, validates API operations, dispatches messages and persists per-mod data",
  { skip },
  async () => {
    await temporary(async (dir) => {
      const mods = path.join(dir, "mods");
      await buildProbe(path.join(mods, "probe"));
      const args = [
        "--mods",
        mods,
        "--command",
        "mod probe ping some plain text",
      ];
      const first = game(dir, args);
      ok(first);
      assert.match(first.stderr, /probe launches=1/);
      assert.match(first.stderr, /command ok/);
      assert.match(first.stderr, /probe unload ready=0/);
      const second = game(dir, args);
      ok(second);
      assert.match(second.stderr, /probe launches=2/);
      assert.equal(
        (
          await readFile(path.join(dir, "save/mods/probe/launches"))
        ).readUInt32LE(),
        2,
      );
    });
  },
);
test(
  "native manifest loader orders dependencies and rejects missing, cyclic, duplicate or incompatible mods",
  { skip },
  async () => {
    await temporary(async (dir) => {
      const mods = path.join(dir, "mods");
      const dependent = path.join(mods, "a-dependent"),
        base = path.join(mods, "z-base");
      await manifest(dependent, "dependent", "requires=base\n");
      await manifest(base, "base");
      await manifest(path.join(mods, "zz-independent"), "independent");
      const result = game(dir, ["--mods", mods]);
      ok(result);
      assert.ok(
        result.stderr.indexOf("[mod:base] loaded") <
          result.stderr.indexOf("[mod:dependent] loaded"),
      );
      assert.ok(
        result.stderr.indexOf("[mod:dependent] loaded") <
          result.stderr.indexOf("[mod:independent] loaded"),
      );
      await manifest(base, "base", "enabled=false\n");
      assert.notEqual(game(dir, ["--mods", mods]).status, 0);
      await manifest(base, "base", "requires=dependent\n");
      assert.match(game(dir, ["--mods", mods]).stderr, /Cyclic/);
      await manifest(base, "dependent");
      assert.match(game(dir, ["--mods", mods]).stderr, /Duplicate/);
      await manifest(base, "base", "game_version=999\n");
      assert.match(game(dir, ["--mods", mods]).stderr, /game_version/);
      await writeFile(
        path.join(base, "mod.ini"),
        "abi=99\nid=base\nversion=1.0.0\n",
      );
      assert.match(game(dir, ["--mods", mods]).stderr, /ABI must be 1/);
      await manifest(base, "base", "unknown=value\n");
      assert.match(
        game(dir, ["--mods", mods]).stderr,
        /Unknown mod manifest field/,
      );
    });
  },
);
test(
  "native asset packs override reads and reject symlink escapes",
  { skip },
  async () => {
    await temporary(async (dir) => {
      const mods = path.join(dir, "mods"),
        pack = path.join(mods, "pack");
      await manifest(pack, "pack");
      const target = path.join(pack, "assets/leaderboards");
      await mkdir(target, { recursive: true });
      const asset = "/assets/leaderboards/drive_mad_2_leaderboards.json";
      const filename = path.join(pack, asset.slice(1));
      await writeFile(filename, "[]\n");
      // Valid PNG magic only: sufficient for the headless adapter test, not for
      // actual gameplay. Different length proves FD seek/stat use override size.
      await mkdir(path.join(pack, "assets/atlas"));
      await writeFile(
        path.join(pack, "assets/atlas/default.png"),
        Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
      );
      const source = `
#include <drive_mad.h>
#include <string.h>
static DMStatus load(const DMApi *a,DMContext *c,void **u) {
 char b[32];size_t n;
 if(a->asset_read(c,"${asset}",b,sizeof(b),&n)!=DM_OK || n!=3 || memcmp(b,"[]\\n",3)) return DM_EABI;
 a->log(c,"asset override verified");return DM_OK;
}
DM_PUBLIC const DMMod *dm_mod_query(uint32_t v) {
 static const DMMod m={.abi_version=1,.struct_size=sizeof(DMMod),.id="probe",.name="Probe",.version="1.0.0",.on_load=load};
 return &m;
}`;
      await buildProbe(path.join(mods, "probe"), source);
      let result = game(dir, ["--mods", mods]);
      ok(result);
      assert.match(result.stderr, /asset override verified/);
      const late = path.join(mods, "z-late");
      await manifest(late, "late");
      await mkdir(path.join(late, "assets/leaderboards"), { recursive: true });
      await writeFile(path.join(late, asset.slice(1)), "[]\n");
      result = game(dir, ["--mods", mods]);
      ok(result);
      assert.match(
        result.stderr,
        /\[mod:late\] overrides \/assets\/leaderboards/,
      );
      await manifest(late, "late", "enabled=false\n");
      await rm(filename);
      await symlink("/etc/passwd", filename);
      result = game(dir, ["--mods", mods]);
      assert.notEqual(result.status, 0);
      assert.match(result.stderr, /Unsafe or unreadable asset override/);
    });
  },
);
test(
  "native plugin loader rejects missing entrypoint and incompatible descriptor",
  { skip },
  async () => {
    await temporary(async (dir) => {
      const mods = path.join(dir, "mods"),
        mod = path.join(mods, "probe");
      await buildProbe(mod, "int not_a_plugin(void) {return 0;}");
      assert.match(game(dir, ["--mods", mods]).stderr, /dm_mod_query/);
      await buildProbe(
        mod,
        probe.replace(
          ".abi_version=1,.struct_size=sizeof(DMMod)",
          ".abi_version=9,.struct_size=sizeof(DMMod)",
        ),
      );
      assert.match(game(dir, ["--mods", mods]).stderr, /descriptor ABI/);
      // Plugins present on disk do not load unless explicitly opted in.
      const result = game(dir);
      ok(result);
      assert.doesNotMatch(result.stderr, /\[mod:/);
    });
  },
);
test(
  "native SDK scaffold, asset encoder, archive, standalone CMake and C++ consumers work",
  { skip },
  async () => {
    await temporary(async (dir) => {
      const mod = path.join(dir, "mods/example");
      run(process.execPath, [cli, "init", mod, "example"]);
      const refused = spawnSync(process.execPath, [cli, "init", mod], {
        encoding: "utf8",
      });
      assert.notEqual(refused.status, 0);
      run(process.execPath, [cli, "build", mod]);
      let result = game(dir, [
        "--mods",
        path.dirname(mod),
        "--command",
        "mod example info",
      ]);
      ok(result);
      assert.match(result.stderr, /ready=0.*launches=1.*exports=\d+/);
      const decoded = path.join(dir, "db.json");
      await writeFile(decoded, '{"sdk":true}');
      run(process.execPath, [cli, "asset", mod, "/assets/db", decoded]);
      assert.equal(
        inflateSync(await readFile(path.join(mod, "assets/db"))).toString(),
        '{"sdk":true}',
      );
      run(process.execPath, [
        cli,
        "pack",
        mod,
        path.join(dir, "example.tar.gz"),
      ]);
      const sdk = path.join(dir, "sdk");
      run(process.execPath, [
        path.join(root, "scripts/export-native-sdk.mjs"),
        sdk,
      ]);
      const cmake = path.join(dir, "cmake");
      run("cmake", [
        "-S",
        path.join(sdk, "examples/native-tools"),
        "-B",
        cmake,
        `-DCMAKE_PREFIX_PATH=${sdk}`,
      ]);
      run("cmake", ["--build", cmake]);
      assert.ok((await readFile(path.join(cmake, "mod.so"))).length > 0);
      for (const example of ["fun-pack", "couch-chaos", "time-warp"]) {
        const out = path.join(dir, "cmake-" + example);
        run("cmake", [
          "-S",
          path.join(sdk, "examples", example),
          "-B",
          out,
          `-DCMAKE_PREFIX_PATH=${sdk}`,
        ]);
        run("cmake", ["--build", out]);
        assert.ok((await readFile(path.join(out, "mod.so"))).length > 0);
      }
      const couch = path.join(dir, "couch-mods/couch-chaos");
      await mkdir(couch, { recursive: true });
      await copyFile(
        path.join(sdk, "examples/couch-chaos/mod.ini"),
        path.join(couch, "mod.ini"),
      );
      await copyFile(
        path.join(dir, "cmake-couch-chaos/mod.so"),
        path.join(couch, "mod.so"),
      );
      result = game(dir, [
        "--mods",
        path.dirname(couch),
        "--command",
        "mod couch-chaos info",
      ]);
      ok(result);
      assert.match(result.stderr, /player=1.*wins=0-0.*supported=1/);
      run("pkg-config", ["--cflags", "drive-mad-sdk"], {
        env: {
          ...process.env,
          PKG_CONFIG_PATH: path.join(sdk, "lib/pkgconfig"),
        },
      });
      const cpp = path.join(dir, "cpp");
      await manifest(cpp, "cpp", "", true);
      await writeFile(
        path.join(cpp, "mod.cpp"),
        `
#include <drive_mad_gameplay.h>
extern "C" DM_PUBLIC const DMMod *dm_mod_query(uint32_t abi) {
 static DMMod m{};m.abi_version=1;m.struct_size=sizeof(m);m.id="cpp";m.name="C++";m.version="1.0.0";return abi==1?&m:nullptr;
}`,
      );
      run(process.env.CXX || "c++", [
        "-std=c++17",
        "-shared",
        "-fPIC",
        "-fvisibility=hidden",
        "-I",
        path.join(sdk, "include"),
        path.join(cpp, "mod.cpp"),
        "-o",
        path.join(cpp, "mod.so"),
      ]);
      const cppRoot = path.join(dir, "cppmods");
      await mkdir(cppRoot);
      await import("node:fs/promises").then((fs) =>
        fs.rename(cpp, path.join(cppRoot, "cpp")),
      );
      result = game(dir, ["--mods", cppRoot]);
      ok(result);
      assert.match(result.stderr, /\[mod:cpp\] loaded/);
    });
  },
);
test(
  "native SDK real GUI executes ready, frame, render and input callbacks",
  {
    skip:
      skip ||
      (process.env.DRIVE_MAD_TEST_GUI !== "1" &&
        "Set DRIVE_MAD_TEST_GUI=1 in a graphical session"),
  },
  async () => {
    await temporary(async (dir) => {
      const mods = path.join(dir, "mods");
      await buildProbe(path.join(mods, "probe"));
      const result = run(
        binary,
        [
          "--mods",
          mods,
          "--smoke-test",
          "--console",
          "--command",
          "mod probe ping some plain text",
        ],
        {
          cwd: dir,
          env: { ...process.env, DRIVE_MAD_SAVE_DIR: path.join(dir, "save") },
          input:
            "x".repeat(4096) +
            "\nversion\nexports get_app_version\nnot-a-command\nmod probe ping some plain text\n",
        },
      );
      assert.match(
        result.stderr,
        /probe unload ready=1 frames=[1-9]\d* renders=[1-9]\d* inputs=[1-9]\d*/,
      );
      assert.match(result.stderr, /GL_error=0x0/);
      assert.match(result.stdout, /Game 166 \/ Native SDK ABI 1/);
      assert.match(result.stdout, /get_app_version\(\) -> i32/);
      assert.match(result.stderr, /console: line too long/);
      assert.match(result.stderr, /console: not found/);
    });
  },
);
