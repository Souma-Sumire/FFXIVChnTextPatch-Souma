// 把字体包里的 .tex 从 B8G8R8A8（每像素 4 字节）转成游戏原版使用的
// B4G4R4A4（每像素 2 字节）。像素总量不变、清晰度按原版规格，文件体积减半。
//
// 用法: node scripts/font2b4.ts [目录，默认 resource/font] [--dry]
import fs from "node:fs";
import path from "node:path";
import process from "node:process";

const TYPE_B8G8R8A8 = 0x1450;
const TYPE_B4G4R4A4 = 0x1440;

interface Result {
  file: string;
  before: number;
  after: number;
}

function convertFile(file: string, dry: boolean): Result | null {
  const buf = fs.readFileSync(file);
  if (buf.length < 80) {
    return null;
  }
  if (buf.readUInt16LE(4) !== TYPE_B8G8R8A8) {
    return null;
  }
  const headerLen = buf.readUInt32LE(28);
  const width = buf.readUInt16LE(8);
  const height = buf.readUInt16LE(10);
  const mipCount = buf.readUInt16LE(14);
  if (mipCount !== 1) {
    throw new Error(`${file}: 只支持单 mip 的纹理（当前 ${mipCount}）`);
  }
  if (headerLen < 80 || headerLen > buf.length) {
    throw new Error(`${file}: 纹理头长度异常 ${headerLen}`);
  }
  const pixels = width * height;
  if (buf.length - headerLen !== pixels * 4) {
    throw new Error(`${file}: 尺寸与数据长度不符（${width}x${height}，${buf.length - headerLen} 字节）`);
  }

  const out = Buffer.alloc(headerLen + pixels * 2);
  buf.copy(out, 0, 0, headerLen);
  out.writeUInt16LE(TYPE_B4G4R4A4, 4);
  for (let i = 0, src = headerLen; i < pixels; i++, src += 4) {
    const b = buf[src] >> 4;
    const g = buf[src + 1] >> 4;
    const r = buf[src + 2] >> 4;
    const a = buf[src + 3] >> 4;
    out.writeUInt16LE((a << 12) | (r << 8) | (g << 4) | b, headerLen + i * 2);
  }
  if (!dry) {
    fs.writeFileSync(file, out);
  }
  return { file, before: buf.length, after: out.length };
}

const args = process.argv.slice(2);
const dry = args.includes("--dry");
const dir = args.find((a) => !a.startsWith("--")) ?? "resource/font";
const files = fs.readdirSync(dir).filter((f) => f.toLowerCase().endsWith(".tex")).sort();

let converted = 0;
let before = 0;
let after = 0;
for (const name of files) {
  const result = convertFile(path.join(dir, name), dry);
  if (!result) {
    continue;
  }
  converted++;
  before += result.before;
  after += result.after;
}

if (converted === 0) {
  console.log(`没有需要转换的纹理（${dir} 下的 .tex 已经是 B4G4R4A4 或其他格式）`);
} else {
  console.log(
    `${dry ? "[预览] " : ""}已转换 ${converted} 个纹理: ${(before / 1024 / 1024).toFixed(1)} MB -> ${(after / 1024 / 1024).toFixed(1)} MB`,
  );
}
