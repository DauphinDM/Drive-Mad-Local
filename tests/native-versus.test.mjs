import test from "node:test";
import assert from "node:assert/strict";
import { access, mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import { deflateSync } from "node:zlib";
import path from "node:path";
import os from "node:os";
import { root } from "../scripts/lib/modding.mjs";

const built =
  process.platform === "linux" &&
  (await access(path.join(root, "native/build/gameplay-profile.h")).then(
    () => true,
    () => false,
  ));
function run(command, args) {
  const p = spawnSync(command, args, { encoding: "utf8", timeout: 30000 });
  assert.equal(p.status, 0, p.error?.message || p.stdout + p.stderr);
  return p;
}
const harness = `
#include "versus.c"
#include <assert.h>
static unsigned callbacks,entered;
static u32 last_data,last_size,last_id;
static unsigned char memory[MAX_FETCH+16];
void fail(const char *s) { fprintf(stderr,"%s\\n",s);exit(9); }
void *mem(struct w2c_a *h,u32 offset,size_t length) {
  assert(offset<=sizeof(memory) && length<=sizeof(memory)-offset);
  return memory+offset;
}
u32 game_malloc(w2c_game *g,u32 size) { assert(size<=MAX_FETCH);return 16; }
void game_web_command_fetch_send_url_done(w2c_game *g,u32 data,u32 size,u32 id) {
  callbacks++;last_data=data;last_size=size;last_id=id;
}
bool mods_ready(struct w2c_a *h) {return true;}
#if DM_GAMEPLAY_PROFILE
void game_versus_enter(w2c_game *g) {entered++;}
#endif
int main(int argc,char **argv) {
  assert(argc==2);
  assert(filename(PREFIX "manifest.data"));
  assert(filename(PREFIX "rec_arches_0.data"));
  const char *bad[]={
    "http://api.drivemad.com/vs/5/manifest.data",
    "https://evil.test/vs/5/manifest.data",
    "https://api.drivemad.com.evil/vs/5/manifest.data",
    PREFIX "../manifest.data",PREFIX "rec_../x.data",
    PREFIX "rec_x.data?x=1",PREFIX "rec_%2e.data",
    PREFIX "manifest.data/anything",
    "https://api.drivemad.com/vs/upload/5/rec_x.data"
  };
  for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!filename(bad[i]));
  Fetch f={0};
  assert(receive("abc",1,3,&f)==3 && f.size==3);
  f.size=MAX_FETCH;assert(receive("x",1,1,&f)==0);
  f.size=3;assert(receive("x",SIZE_MAX,2,&f)==0);
  assert(!compressed_valid(&f));free(f.bytes);
  struct w2c_a h={0};
  assert(versus_init(&h,NULL,false));
  versus_fetch(&h,PREFIX "manifest.data",0,false);
  assert(callbacks==0);
  versus_tick(&h);
  assert(callbacks==1 && last_data==0 && last_size==0 && last_id==0);
  versus_fetch(&h,PREFIX "rec_x.data",11,true);
  assert(callbacks==1);versus_tick(&h);
  assert(callbacks==2 && last_data==0 && last_id==11);
  versus_enter(&h);assert(!entered);
  versus_close(&h);assert(h.versus==NULL);
#if !DM_GAMEPLAY_PROFILE
  assert(!versus_init(&h,argv[1],false));
  puts("Versus transport policy and deferred callbacks passed.");
  return 0;
#endif
  assert(versus_init(&h,argv[1],false));
  versus_fetch(&h,PREFIX "manifest.data",7,false);
  assert(callbacks==2);versus_tick(&h);
  assert(callbacks==3 && last_data==16 && last_size>0 && last_id==7);
  assert(memory[16]==0x78);
  versus_fetch(&h,PREFIX "rec_bad.data",8,false);versus_tick(&h);
  assert(callbacks==4 && last_data==0 && last_id==8);
  versus_fetch(&h,PREFIX "rec_bomb.data",9,false);versus_tick(&h);
  assert(callbacks==5 && last_data==0 && last_id==9);
  versus_fetch(&h,PREFIX "rec_trailing.data",10,false);versus_tick(&h);
  assert(callbacks==6 && last_data==0 && last_id==10);
  versus_fetch(&h,PREFIX "rec_missing.data",12,false);versus_tick(&h);
  assert(callbacks==7 && last_data==0 && last_id==12);
  versus_fetch(&h,"https://evil.test/manifest.data",13,false);versus_tick(&h);
  assert(callbacks==8 && last_data==0 && last_id==13);
  versus_fetch(&h,PREFIX "rec_valid.data",14,false);versus_tick(&h);
  assert(callbacks==9 && last_data==16 && last_id==14);
  versus_fetch(&h,PREFIX "rec_counts.data",15,false);versus_tick(&h);
  assert(callbacks==10 && last_data==0 && last_id==15);
  versus_fetch(&h,PREFIX "rec_truncated.data",16,false);versus_tick(&h);
  assert(callbacks==11 && last_data==0 && last_id==16);
  // Shutdown cancels pending work without calling into a destroyed engine.
  versus_fetch(&h,PREFIX "manifest.data",17,false);
  versus_close(&h);assert(callbacks==11 && h.versus==NULL);
  puts("Versus transport policy and deferred callbacks passed.");
  return 0;
}
`;
test(
  "Versus GET policy, offline/cache responses, bounds and deferred callbacks (no network)",
  { skip: !built },
  async () => {
    const dir = await mkdtemp(
      path.join(
        process.env.OPENCODE_TMP_DIR || os.tmpdir(),
        "drive-mad-versus-",
      ),
    );
    try {
      const cache = path.join(dir, "cache");
      await mkdir(cache);
      const manifest = deflateSync('{"upload_chance":0,"vs":{}}');
      await writeFile(path.join(cache, "manifest.data"), manifest);
      await writeFile(path.join(cache, "rec_bad.data"), "bad zlib stream");
      await writeFile(
        path.join(cache, "rec_bomb.data"),
        deflateSync(Buffer.alloc(32 * 1024 * 1024 + 1)),
      );
      await writeFile(
        path.join(cache, "rec_trailing.data"),
        Buffer.concat([manifest, Buffer.from([1])]),
      );
      // Tiny synthetic v9 replay: two frames, one part, one delta frame.
      const uint = (n) => {
        const b = Buffer.alloc(4);
        b.writeUInt32BE(n);
        return b;
      };
      const string = (s) =>
        Buffer.concat([Buffer.from([0, s.length]), Buffer.from(s + "\0")]);
      const scale = Buffer.alloc(4);
      scale.writeFloatBE(0.002);
      const payload = Buffer.concat([
        Buffer.alloc(11),
        string("Arches"),
        string("dino"),
        uint(0),
        uint(0xffffffff),
        uint(2),
        uint(1),
        uint(0xffffffff),
        uint(1),
        uint(1),
        uint(120),
        scale,
        Buffer.alloc(16),
        Buffer.alloc(4),
        uint(0),
        uint(0),
      ]);
      const replay = Buffer.concat([
        Buffer.from("dma\0\0\x09"),
        uint(payload.length),
        payload,
      ]);
      await writeFile(path.join(cache, "rec_valid.data"), deflateSync(replay));
      const counts = Buffer.from(replay);
      counts.writeUInt32BE(0x7fffffff, 45); // frames field
      await writeFile(path.join(cache, "rec_counts.data"), deflateSync(counts));
      await writeFile(
        path.join(cache, "rec_truncated.data"),
        deflateSync(replay.subarray(0, replay.length - 1)),
      );
      const source = path.join(dir, "check.c");
      await writeFile(source, harness);
      const cflags = run("pkg-config", [
        "--cflags",
        "sdl2",
        "glesv2",
        "libcurl",
        "zlib",
      ])
        .stdout.trim()
        .split(/\s+/);
      const libs = run("pkg-config", ["--libs", "libcurl", "zlib"])
        .stdout.trim()
        .split(/\s+/);
      const binary = path.join(dir, "check");
      run(process.env.CC || "cc", [
        "-std=c11",
        "-D_GNU_SOURCE",
        "-DWASM_RT_USE_SEGUE=0",
        "-I",
        path.join(root, "native"),
        "-I",
        path.join(root, "native/build"),
        "-I",
        path.join(root, "modding/generated/tools/wabt-1.0.39/include"),
        ...cflags,
        source,
        "-o",
        binary,
        ...libs,
      ]);
      assert.match(run(binary, [cache]).stdout, /deferred callbacks passed/);
    } finally {
      await rm(dir, { recursive: true, force: true });
    }
  },
);
