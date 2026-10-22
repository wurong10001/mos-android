// 在没有 Android SDK 的环境下验证 sample.wasm:
//   1. WebAssembly.validate 校验模块格式
//   2. 用 mock 的 host 函数实例化, 调用 init()/render()
//   3. 检查显存像素值是否符合预期
// 用法: node tools/test_wasm.mjs
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const bytes = readFileSync(join(here, "..", "app", "src", "main", "assets", "sample.wasm"));

console.log("validate:", WebAssembly.validate(bytes));
if (!WebAssembly.validate(bytes)) process.exit(1);

let present = null;
const env = {
  host_present: (offset, w, h) => { present = { offset, w, h }; },
  host_log: (offset, len) => {},
};

const inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), { env });
const W = 256, H = 256, FB = 0x1000;

const init = inst.exports.init();
console.log("init() =", init, init === FB ? "OK" : "FAIL");

const ret = inst.exports.render(0);
console.log("render(0) =", ret, ret === FB ? "OK" : "FAIL");
console.log("present =", JSON.stringify(present));

const mem = new Uint32Array(inst.exports.memory.buffer);
const px = (x, y) => mem[FB / 4 + y * W + x];

let ok = true;
const check = (name, got, want) => {
  const pass = got === want;
  if (!pass) ok = false;
  console.log(name + ": 0x" + got.toString(16) + " " + (pass ? "OK" : "FAIL want 0x" + want.toString(16)));
};

check("px(0,0) frame0", px(0, 0), 0xff000000);
check("px(10,5) frame0", px(10, 5), 0xff0f0f1e);
inst.exports.render(7);
check("px(10,5) frame7", px(10, 5), 0xff084041);

console.log(ok ? "ALL PASS" : "FAILED");
process.exit(ok ? 0 : 1);
