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

const ZIP_DOS_TIME = 0;
const ZIP_DOS_DATE = (40 << 9) | (1 << 5) | 1;

function sha256File(filePath: string): { sha256: string; size: number } {
  if (!fs.existsSync(filePath)) {
    return { sha256: "", size: 0 };
  }
  const content = fs.readFileSync(filePath);
  return {
    sha256: createHash("sha256").update(content).digest("hex"),
    size: content.length,
  };
}

function listRelativeFiles(root: string, prefix: string): string[] {
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
    console.log(`已重新生成字体包: ${path.relative(RESOURCE_PATH, FONT_ZIP_PATH)}（${fontZipEntries.length} 个文件，${(fontZipInfo.size / 1024 / 1024).toFixed(2)} MB）`);
  } else {
    const zipHash = sha256File(FONT_ZIP_PATH);
    fontZipInfo = {
      sha256: zipHash.sha256,
      size: zipHash.size,
      count: fontKeys.length,
    };
    console.log(`字体文件未变更，复用现有: ${path.relative(RESOURCE_PATH, FONT_ZIP_PATH)}`);
  }

  const manifest = {
    generatedAt: new Date().toISOString().replace(/\.\d{3}Z$/, "Z"),
    rawexd: zipInfo,
    font: fontZipInfo,
    rules: { sha256: rulesInfo.sha256, size: rulesInfo.size },
    tables: { sha256: tablesInfo.sha256, size: tablesInfo.size },
    fonts,
  };
  fs.writeFileSync(MANIFEST_PATH, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
  console.log(`已生成同步清单: ${path.relative(RESOURCE_PATH, MANIFEST_PATH)}（${fontKeys.length} 个字体）`);
  console.log(`已生成全量快照包: ${path.relative(RESOURCE_PATH, ZIP_PATH)}（${zipEntries.length} 个文件，${(zipInfo.size / 1024 / 1024).toFixed(2)} MB）`);
  console.log(`已处理字体快照包: ${path.relative(RESOURCE_PATH, FONT_ZIP_PATH)}（${fontZipInfo.count} 个文件，${(fontZipInfo.size / 1024 / 1024).toFixed(2)} MB）`);
}

generateManifestAndSnapshot();
