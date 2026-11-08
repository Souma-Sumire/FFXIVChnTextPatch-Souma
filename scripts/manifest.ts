import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { deflateRawSync } from "node:zlib";

const ROOT_DIR = path.resolve(import.meta.dirname, "..");
const RESOURCE_PATH = path.join(ROOT_DIR, "resource");
const OUTPUT_PATH = path.join(RESOURCE_PATH, "rawexd");
const FONT_PATH = path.join(RESOURCE_PATH, "font");
const MANIFEST_PATH = path.join(RESOURCE_PATH, "manifest.json");
const ZIP_PATH = path.join(RESOURCE_PATH, "rawexd.zip");
const FONT_ZIP_PATH = path.join(RESOURCE_PATH, "font.zip");
const RULES_PATH = path.join(RESOURCE_PATH, "rules.json");
const TABLES_PATH = path.join(RESOURCE_PATH, "tables.json");

const CRC_TABLE = new Uint32Array(256);
for (let i = 0; i < 256; i++) {
  let c = i;
  for (let k = 0; k < 8; k++) {
    c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  }
  CRC_TABLE[i] = c >>> 0;
}

function crc32(buf: Buffer): number {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    c = CRC_TABLE[(c ^ buf[i]!) & 0xff]! ^ (c >>> 8);
  }
  return (c ^ 0xffffffff) >>> 0;
}

// 固定 ZIP 时间戳，保证相同内容生成完全相同的快照包。
// 该值参与 zip 字节与 rootHash 计算，改动会让已发布客户端重新下载整包资源。
const ZIP_DOS_TIME = 0;
const ZIP_DOS_DATE = (40 << 9) | (1 << 5) | 1;

function sha256File(filePath: string): { sha256: string; size: number } {
  if (!fs.existsSync(filePath)) {
    throw new Error(`找不到文件: ${filePath}`);
  }
  const hash = createHash("sha256");
  const fd = fs.openSync(filePath, "r");
  try {
    const buf = Buffer.allocUnsafe(1024 * 1024);
    let read = 0;
    while ((read = fs.readSync(fd, buf, 0, buf.length, null)) > 0) {
      hash.update(buf.subarray(0, read));
    }
  } finally {
    fs.closeSync(fd);
  }
  return { sha256: hash.digest("hex"), size: fs.statSync(filePath).size };
}

// 供 csv 脚本生成表名索引时复用，prefix 为空表示按目录相对路径返回。
export function listRelativeFiles(root: string, prefix: string): string[] {
  const out: string[] = [];
  const walk = (dir: string, rel: string): void => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const childRel = rel === "" ? entry.name : `${rel}/${entry.name}`;
      if (entry.isDirectory()) {
        walk(path.join(dir, entry.name), childRel);
      } else if (entry.isFile()) {
        out.push(prefix === "" ? childRel : `${prefix}/${childRel}`);
      }
    }
  };
  if (fs.existsSync(root)) {
    walk(root, "");
  }
  return out.sort();
}

interface ZipEntry {
  name: string;
  absPath: string;
}

function buildZip(entries: ZipEntry[]): Buffer {
  const localChunks: Buffer[] = [];
  const centralChunks: Buffer[] = [];
  let localOffset = 0;

  for (const entry of entries) {
    const raw = fs.readFileSync(entry.absPath);
    const compressed = deflateRawSync(raw, { level: 9 });
    const checksum = crc32(raw);
    const nameBuf = Buffer.from(entry.name, "utf8");
    const time = ZIP_DOS_TIME;
    const date = ZIP_DOS_DATE;

    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(0x0800, 6);
    local.writeUInt16LE(8, 8);
    local.writeUInt16LE(time, 10);
    local.writeUInt16LE(date, 12);
    local.writeUInt32LE(checksum, 14);
    local.writeUInt32LE(compressed.length, 18);
    local.writeUInt32LE(raw.length, 22);
    local.writeUInt16LE(nameBuf.length, 26);
    local.writeUInt16LE(0, 28);

    const central = Buffer.alloc(46);
    central.writeUInt32LE(0x02014b50, 0);
    central.writeUInt16LE(20, 4);
    central.writeUInt16LE(20, 6);
    central.writeUInt16LE(0x0800, 8);
    central.writeUInt16LE(8, 10);
    central.writeUInt16LE(time, 12);
    central.writeUInt16LE(date, 14);
    central.writeUInt32LE(checksum, 16);
    central.writeUInt32LE(compressed.length, 20);
    central.writeUInt32LE(raw.length, 24);
    central.writeUInt16LE(nameBuf.length, 28);
    central.writeUInt16LE(0, 30);
    central.writeUInt16LE(0, 32);
    central.writeUInt16LE(0, 34);
    central.writeUInt16LE(0, 36);
    central.writeUInt32LE(0, 38);
    central.writeUInt32LE(localOffset, 42);

    localChunks.push(local, nameBuf, compressed);
    centralChunks.push(central, nameBuf);
    localOffset += local.length + nameBuf.length + compressed.length;
  }

  const centralBuf = Buffer.concat(centralChunks);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(0, 4);
  eocd.writeUInt16LE(0, 6);
  eocd.writeUInt16LE(entries.length, 8);
  eocd.writeUInt16LE(entries.length, 10);
  eocd.writeUInt32LE(centralBuf.length, 12);
  eocd.writeUInt32LE(localOffset, 16);
  eocd.writeUInt16LE(0, 20);

  return Buffer.concat([...localChunks, centralBuf, eocd]);
}

// 同步清单与快照包的唯一实现。csv 处理与发布打包都必须走这里，
// 否则两处各自计算 rootHash 会得到不同指纹，客户端会误判资源需要更新。
export function generateManifestAndSnapshot(): void {
  const zipEntries: ZipEntry[] = listRelativeFiles(OUTPUT_PATH, "rawexd").map((rel) => ({
    name: rel,
    absPath: path.join(RESOURCE_PATH, rel),
  }));
  const zipData = buildZip(zipEntries);
  fs.writeFileSync(ZIP_PATH, zipData);
  const zipInfo = {
    sha256: createHash("sha256").update(zipData).digest("hex"),
    size: zipData.length,
    count: zipEntries.length,
  };

  const rulesInfo = sha256File(RULES_PATH);
  const tablesInfo = sha256File(TABLES_PATH);

  // 字体包：macOS 无法运行 Windows 版编译组件，靠这份预编译字体按需下载。
  const fontKeys = listRelativeFiles(FONT_PATH, "font");
  const fonts: Record<string, { sha256: string; size: number }> = {};
  for (const key of fontKeys) {
    fonts[key] = sha256File(path.join(RESOURCE_PATH, key));
  }

  let fontChanged = !fs.existsSync(FONT_ZIP_PATH);
  if (!fontChanged && fs.existsSync(MANIFEST_PATH)) {
    try {
      const oldManifest = JSON.parse(fs.readFileSync(MANIFEST_PATH, "utf-8"));
      if (!oldManifest.fonts || oldManifest.font?.size !== fs.statSync(FONT_ZIP_PATH).size) {
        fontChanged = true;
      } else {
        for (const key of fontKeys) {
          if (!oldManifest.fonts[key] || oldManifest.fonts[key].sha256 !== fonts[key]!.sha256) {
            fontChanged = true;
            break;
          }
        }
      }
    } catch {
      fontChanged = true;
    }
  }

  let fontZipInfo: { sha256: string; size: number; count: number };
  if (fontChanged) {
    const fontZipEntries: ZipEntry[] = fontKeys.map((rel) => ({
      name: rel,
      absPath: path.join(RESOURCE_PATH, rel),
    }));
    const fontZipData = buildZip(fontZipEntries);
    fs.writeFileSync(FONT_ZIP_PATH, fontZipData);
    fontZipInfo = {
      sha256: createHash("sha256").update(fontZipData).digest("hex"),
      size: fontZipData.length,
      count: fontZipEntries.length,
    };
    console.log(
      `已重新生成字体包: ${path.relative(RESOURCE_PATH, FONT_ZIP_PATH)}（${fontZipEntries.length} 个文件，${(fontZipInfo.size / 1024 / 1024).toFixed(2)} MB）`,
    );
  } else {
    const zipHash = sha256File(FONT_ZIP_PATH);
    fontZipInfo = { sha256: zipHash.sha256, size: zipHash.size, count: fontKeys.length };
    console.log(`字体文件未变更，复用现有: ${path.relative(RESOURCE_PATH, FONT_ZIP_PATH)}`);
  }

  const rootHash = createHash("sha256");
  rootHash.update(`rawexd.zip:${zipInfo.sha256}\n`);
  rootHash.update(`font.zip:${fontZipInfo.sha256}\n`);
  rootHash.update(`rules.json:${rulesInfo.sha256}\n`);
  rootHash.update(`tables.json:${tablesInfo.sha256}\n`);
  for (const key of fontKeys) {
    rootHash.update(`${key}:${fonts[key]!.sha256}\n`);
  }

  const manifest = {
    generatedAt: new Date().toISOString().replace(/\.\d{3}Z$/, "Z"),
    rootHash: rootHash.digest("hex"),
    rawexd: zipInfo,
    font: fontZipInfo,
    rules: { sha256: rulesInfo.sha256, size: rulesInfo.size },
    tables: { sha256: tablesInfo.sha256, size: tablesInfo.size },
    fonts,
  };
  fs.writeFileSync(MANIFEST_PATH, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
  console.log(`已生成同步清单: ${path.relative(RESOURCE_PATH, MANIFEST_PATH)}（${fontKeys.length} 个字体）`);
  console.log(
    `已生成全量快照包: ${path.relative(RESOURCE_PATH, ZIP_PATH)}（${zipEntries.length} 个文件，${(zipInfo.size / 1024 / 1024).toFixed(2)} MB）`,
  );
  console.log(
    `已处理字体包: ${path.relative(RESOURCE_PATH, FONT_ZIP_PATH)}（${fontZipInfo.count} 个文件，${(fontZipInfo.size / 1024 / 1024).toFixed(2)} MB）`,
  );
}
