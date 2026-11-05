import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { deflateRawSync } from "node:zlib";
import { csvName, folderName } from "./names.ts";
import {
  cnColShift,
  materiaMap,
  resolveRsv,
  shoutingReplace,
  specialFile,
  specialRow,
  specialWord,
} from "./special.ts";

const DATAMINING_ROOT = "D:/Github/ffxiv-datamining-hexcode-mixed";
const CSV_PATHS = {
  cn: path.join(DATAMINING_ROOT, "chs"),
  ja: path.join(DATAMINING_ROOT, "ja"),
  en: path.join(DATAMINING_ROOT, "en"),
} as const;
const OUTPUT_PATH = path.resolve(import.meta.dirname, "../resource/rawexd");
const RESOURCE_PATH = path.resolve(import.meta.dirname, "../resource");
const MANIFEST_PATH = path.join(RESOURCE_PATH, "manifest.json");
const ZIP_PATH = path.join(RESOURCE_PATH, "rawexd.zip");
const RULES_PATH = path.join(RESOURCE_PATH, "rules.json");
const TABLES_PATH = path.join(RESOURCE_PATH, "tables.json");

const TOP_KEYS = new Set(["key", "#", "offset", "Int32"]);
const IGNORED_ROOT_CSVS = new Set([
  "CharaMakeName.csv",
  "CreditListText.csv",
  "RacingChocoboName.csv",
  "World.csv",
]);
const NO_TRANSLATION_REG = /^●|翻訳不要|^[<hex:A-Z0-9>\\!@#$%^&*()_+-=/ 《》]+$/;
const SHOUTING_REG = /(?<=(?:“说话”频道.*输入).*?“)(?<str>(?!<hex).+?)(?=”)/;
const SKIP_REG = /（★未使用／削除予定★）/;
const KANA_REG = /[\u3040-\u309f\u30a0-\u30ff]/;
const HAN_REG = /[\u4e00-\u9fff]/;
const HEX_TAG_REG = /<hex:[0-9A-Fa-f]+>/g;

function parseCsv(content: string): string[][] {
  const text = content.replace(/^\ufeff/, "");
  const rows: string[][] = [];
  let row: string[] = [];
  let field = "";
  let inQuotes = false;

  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (inQuotes) {
      if (ch === '"') {
        if (text[i + 1] === '"') {
          field += '"';
          i++;
        } else {
          inQuotes = false;
        }
      } else {
        field += ch;
      }
    } else if (ch === '"') {
      inQuotes = true;
    } else if (ch === ",") {
      row.push(field);
      field = "";
    } else if (ch === "\r") {
      if (text[i + 1] === "\n") i++;
      row.push(field);
      rows.push(row);
      row = [];
      field = "";
    } else if (ch === "\n") {
      row.push(field);
      rows.push(row);
      row = [];
      field = "";
    } else {
      field += ch;
    }
  }

  if (field.length > 0 || row.length > 0) {
    row.push(field);
    rows.push(row);
  }

  return rows;
}

function formatCsvField(val: string | undefined): string {
  const s = val ?? "";
  if (s.includes(",") || s.includes('"') || s.includes("\r") || s.includes("\n")) {
    return `"${s.replaceAll('"', '""')}"`;
  }
  return s;
}

function serializeCsv(rows: string[][]): string {
  return `\ufeff${rows.map((r) => r.map(formatCsvField).join(",")).join("\r\n")}\r\n`;
}

function stripHexTags(value: string): string {
  return value.replace(HEX_TAG_REG, "").trim();
}

// 英文标题：去掉 <hex:...> 标记后只由拉丁字母/数字和常见符号组成。
function isEnglishTitle(value: string): boolean {
  const text = stripHexTags(value);
  if (!text || text.length > 60) return false;
  if (/[\u3040-\u30ff\u3400-\u9fff]/.test(text)) return false;
  if (!/[A-Za-z]/.test(text)) return false;
  return /^[A-Za-z0-9][A-Za-z0-9 &'’\-:!?.,/\\+%()\[\]“”"«»™©®]*$/.test(text);
}

// 日文副标题：去掉 <hex:...> 标记后不含拉丁字母，且为短标题。
// 允许纯汉字（如「無人島開拓」「製作」）和含假名的标题。
function isJapaneseSubtitle(value: string): boolean {
  const text = stripHexTags(value);
  if (!text || text.length > 40) return false;
  if (/[A-Za-z]/.test(text)) return false;
  if (KANA_REG.test(text)) return true;
  return HAN_REG.test(text) && text.length <= 12;
}

function readCsvRowMap(filePath: string, targetCols: string[]): Map<string, Record<string, string>> {
  const map = new Map<string, Record<string, string>>();
  if (!fs.existsSync(filePath)) return map;

  const rows = parseCsv(fs.readFileSync(filePath, "utf8"));
  if (rows.length === 0) return map;

  const headerRow = rows[0] ?? [];
  const colIndices = targetCols.map((c) => headerRow.indexOf(c));

  for (let i = 0; i < rows.length; i++) {
    const r = rows[i];
    if (!r) continue;
    const key = r[0]?.trim();
    if (!key || key === "int32") continue;

    const vals: Record<string, string> = {};
    for (let c = 0; c < targetCols.length; c++) {
      const colName = targetCols[c]!;
      const idx = colIndices[c] ?? -1;
      vals[colName] = idx !== -1 ? (r[idx] ?? "") : "";
    }
    map.set(key, vals);
  }

  return map;
}

function resolveCellValue(params: {
  fileName: string;
  col: string;
  rowKey: string;
  ja: string;
  cn: string | undefined;
  en: string | undefined;
  auto?: string;
  getSoumaValue: () => string;
}): string {
  const { fileName, col, rowKey, ja, cn, en, auto, getSoumaValue } = params;

  const spRow = specialRow[fileName]?.[col];
  if (spRow !== undefined) {
    return spRow === "ja" ? ja : (cn ?? "");
  }

  const spFileVal = specialFile[fileName]?.[col]?.[rowKey];
  if (spFileVal !== undefined) {
    return spFileVal;
  }

  // 「英文标题 + 日文副标题」的自动配对结果（见 buildPairOverrides）。
  if (auto !== undefined) {
    return auto;
  }

  if (cn !== undefined) {
    const rsvVal = resolveRsv(cn);
    if (rsvVal !== undefined) {
      return rsvVal;
    }
    if (cn.startsWith("_rsv_")) {
      const souma = getSoumaValue();
      return souma.startsWith("_rsv_") ? "" : souma;
    }
  }

  if (!cn || NO_TRANSLATION_REG.test(cn)) {
    return "";
  }

  if (cn === ja && cn === en) {
    return "";
  }

  return cn;
}

const globalSummaryWarnings: string[] = [];

/**
 * 自动识别日服「英文标题 + 日文副标题」的双语排版：
 *
 *   Lobby 1804:  "3"=TANK  "4"=タンク   → 中文只翻了 "3"（防护职业），"4" 为空
 *   Addon 17300/17301: CONFIG SHARE / コンフィグシェア → 中文只翻了标题行
 *
 * 这类条目如果直接按官方中文输出，就会变成「防护职业 + タンク」这种中日混排。
 * 规则是保留英文标题，把官方中文的翻译放进日文副标题的位置。
 *
 * 支持两种形态：
 *   - 同一行的相邻两列（标题列 + 副标题列）
 *   - 相邻两行的同一列（标题行 + 副标题行）
 *
 * 结果只作为 special.ts 手写规则之外的兜底，不会覆盖手写值。
 */
function buildPairOverrides(params: {
  fileName: string;
  jaRows: string[][];
  jaColIndices: number[];
  cnMap: Map<string, Record<string, string>>;
  targetCols: string[];
}): Map<string, Record<string, string>> {
  const { fileName, jaRows, jaColIndices, cnMap, targetCols } = params;
  const overrides = new Map<string, Record<string, string>>();
  if (targetCols.length === 0) return overrides;

  const cnColOf = (col: string): string => cnColShift[fileName]?.[col] ?? col;
  const cnValue = (key: string, col: string): string => {
    const row = cnMap.get(key);
    return row ? (row[cnColOf(col)] ?? "") : "";
  };

  const set = (key: string, col: string, value: string): void => {
    let entry = overrides.get(key);
    if (!entry) {
      entry = {};
      overrides.set(key, entry);
    }
    entry[col] = value;
  };

  const rowsByKey = new Map<string, string[]>();
  for (let i = 1; i < jaRows.length; i++) {
    const row = jaRows[i];
    const key = row?.[0]?.trim();
    if (!key || TOP_KEYS.has(key)) continue;
    rowsByKey.set(key, row);
  }

  // 形态一：同一行内「英文标题列 + 日文副标题列」。
  for (const [key, jaRow] of rowsByKey) {
    if (!cnMap.has(key)) continue;
    for (let c = 0; c + 1 < targetCols.length; c++) {
      const titleCol = targetCols[c]!;
      const subtitleCol = targetCols[c + 1]!;
      const jaTitle = jaRow[jaColIndices[c] ?? -1] ?? "";
      const jaSubtitle = jaRow[jaColIndices[c + 1] ?? -1] ?? "";
      if (!isEnglishTitle(jaTitle) || !isJapaneseSubtitle(jaSubtitle)) continue;
      const cnTitle = cnValue(key, titleCol);
      if (!HAN_REG.test(cnTitle) || cnValue(key, subtitleCol).trim() !== "") continue;
      set(key, titleCol, jaTitle.trim());
      set(key, subtitleCol, cnTitle.trim());
    }
  }

  // 形态二：相邻两行「英文标题行 + 日文副标题行」。
  const numericKeys = [...rowsByKey.keys()]
    .map((key) => Number(key))
    .filter((key) => Number.isInteger(key))
    .sort((a, b) => a - b);
  for (const numericKey of numericKeys) {
    const key = String(numericKey);
    const nextKey = String(numericKey + 1);
    const jaRow = rowsByKey.get(key);
    const jaNextRow = rowsByKey.get(nextKey);
    if (!jaRow || !jaNextRow || !cnMap.has(key) || !cnMap.has(nextKey)) continue;
    for (let c = 0; c < targetCols.length; c++) {
      const col = targetCols[c]!;
      const jaTitle = jaRow[jaColIndices[c] ?? -1] ?? "";
      const jaSubtitle = jaNextRow[jaColIndices[c] ?? -1] ?? "";
      if (!isEnglishTitle(jaTitle) || !isJapaneseSubtitle(jaSubtitle)) continue;
      const cnTitle = cnValue(key, col);
      if (!HAN_REG.test(cnTitle) || cnValue(nextKey, col).trim() !== "") continue;
      set(key, col, jaTitle.trim());
      set(nextKey, col, cnTitle.trim());
    }
  }

  return overrides;
}

function checkKTGTypeWordMissing(params: {
  jaRows: string[][];
  enMap: Map<string, Record<string, string>>;
  outRows: string[][];
  targetCols: string[];
}): void {
  const warnings: string[] = [];

  const outMap = new Map<string, Record<string, string>>();
  for (let i = 4; i < params.outRows.length; i++) {
    const r = params.outRows[i];
    if (!r || !r[0]) continue;
    const vals: Record<string, string> = {};
    for (let c = 0; c < params.targetCols.length; c++) {
      vals[params.targetCols[c]!] = r[c + 1] ?? "";
    }
    outMap.set(r[0].trim(), vals);
  }

  const jaHeader = params.jaRows[0] ?? [];
  const jaIdx0 = jaHeader.indexOf("0");

  for (let i = 4; i < params.jaRows.length; i++) {
    const r = params.jaRows[i];
    if (!r) continue;
    const key = r[0]?.trim();
    if (!key || TOP_KEYS.has(key)) continue;

    const jaText = (jaIdx0 !== -1 ? r[jaIdx0] : "") ?? "";
    const enText = params.enMap.get(key)?.["0"] ?? "";
    const outEntry = outMap.get(key);
    const cnText = outEntry?.["0"] ?? "";
    const pinyinText = outEntry?.["1"] ?? "";

    if (!jaText && !enText) continue;

    if (!cnText || KANA_REG.test(cnText)) {
      warnings.push(`  - [Key ${key}] 国际服有内容但汉化缺失 (日: "${jaText}", 英: "${enText}")`);
    } else if (!pinyinText) {
      warnings.push(`  - [Key ${key}] 缺少击键拼音 (词条: "${cnText}")`);
    }
  }

  for (const [enKey, enVals] of params.enMap.entries()) {
    if (TOP_KEYS.has(enKey) || outMap.has(enKey)) continue;
    const enText = enVals["0"] ?? "";
    if (enText) {
      warnings.push(`  - [Key ${enKey}] 英文端存在新条目但数据表未收录 (英: "${enText}")`);
    }
  }

  if (warnings.length > 0) {
    const headerMsg = `[警告] KTGTypeWordTextData.csv 检测到 ${warnings.length} 个国际服词条缺少汉化或拼音：`;
    console.warn(`\n${headerMsg}`);
    for (const issue of warnings) {
      console.warn(issue);
    }
    console.warn(`请在 scripts/ktg.ts 中补充对应的中文译名与击键拼音。\n`);
    globalSummaryWarnings.push(headerMsg, ...warnings, `请在 scripts/ktg.ts 中补充对应的中文译名与击键拼音。`);
  }
}

function processRootCsv(fileName: string): void {
  const jaPath = path.join(CSV_PATHS.ja, fileName);
  if (!fs.existsSync(jaPath)) {
    console.warn(`跳过不存在的文件: ${jaPath}`);
    return;
  }

  const cnPath = path.join(CSV_PATHS.cn, fileName);
  const enPath = path.join(CSV_PATHS.en, fileName);
  const soumaPath = path.join(OUTPUT_PATH, fileName);

  const jaRows = parseCsv(fs.readFileSync(jaPath, "utf8"));
  const headerKeys = jaRows[0] ?? [];
  const typeKeys = jaRows[3] ?? [];

  const targetCols =
    fileName === "Quest.csv"
      ? ["0", "1"]
      : headerKeys.slice(1).filter((_, idx) => typeKeys[idx + 1]?.trim() === "String");

  const cnMap = readCsvRowMap(cnPath, targetCols);
  const enMap = readCsvRowMap(enPath, targetCols);
  let soumaMap: Map<string, Record<string, string>> | null = null;

  const jaColIndices = targetCols.map((c) => headerKeys.indexOf(c));
  const pairOverrides = buildPairOverrides({ fileName, jaRows, jaColIndices, cnMap, targetCols });
  if (pairOverrides.size > 0) {
    console.log(`  ${fileName}: 自动识别 ${pairOverrides.size} 条「英文标题 + 日文副标题」`);
  }
  const outRows: string[][] = [["key", ...targetCols]];

  for (let i = 1; i < jaRows.length; i++) {
    const r = jaRows[i];
    if (!r) continue;
    const rowKey = r[0]?.trim();
    if (!rowKey || rowKey === "int32") continue;

    if (TOP_KEYS.has(rowKey)) {
      outRows.push([rowKey, ...jaColIndices.map((idx) => (idx !== -1 ? (r[idx] ?? "") : ""))]);
      continue;
    }

    const cnRow = cnMap.get(rowKey);
    const enRow = enMap.get(rowKey);
    const autoRow = pairOverrides.get(rowKey);
    const outRow: string[] = [rowKey];

    for (let c = 0; c < targetCols.length; c++) {
      const col = targetCols[c]!;
      const jaIdx = jaColIndices[c] ?? -1;
      const ja = jaIdx !== -1 ? (r[jaIdx] ?? "") : "";
      const shiftedCol = cnColShift[fileName]?.[col] ?? col;
      const cn = cnRow ? (cnRow[shiftedCol] ?? "") : undefined;
      const en = enRow ? (enRow[col] ?? "") : undefined;

      const rawVal = resolveCellValue({
        fileName,
        col,
        rowKey,
        ja,
        cn,
        en,
        auto: autoRow?.[col],
        getSoumaValue: () => {
          if (!soumaMap) {
            soumaMap = readCsvRowMap(soumaPath, targetCols);
          }
          return soumaMap.get(rowKey)?.[col] ?? "";
        },
      });

      outRow.push(rawVal.replaceAll(/\n/g, "<hex:02100103>"));
    }

    outRows.push(outRow);
  }

  fs.mkdirSync(OUTPUT_PATH, { recursive: true });
  fs.writeFileSync(soumaPath, serializeCsv(outRows), "utf8");

  if (fileName === "KTGTypeWordTextData.csv") {
    checkKTGTypeWordMissing({ jaRows, enMap, outRows, targetCols });
  }
}

function readDialogueMap(filePath: string): Map<string, string> {
  const map = new Map<string, string>();
  if (!fs.existsSync(filePath)) return map;

  const rows = parseCsv(fs.readFileSync(filePath, "utf8"));
  for (let i = 1; i < rows.length; i++) {
    const r = rows[i];
    const id = r?.[1];
    if (id) {
      map.set(id, r[2] ?? "");
    }
  }
  return map;
}

const shoutingCollect = new Set<string>();

function processFolderCsv(relPath: string): void {
  const jaPath = path.join(CSV_PATHS.ja, relPath);
  const cnPath = path.join(CSV_PATHS.cn, relPath);
  const enPath = path.join(CSV_PATHS.en, relPath);
  const outPath = path.join(OUTPUT_PATH, relPath);

  const jaRows = parseCsv(fs.readFileSync(jaPath, "utf8"));
  const cnMap = readDialogueMap(cnPath);
  const enMap = readDialogueMap(enPath);

  const outRows: string[][] = [];
  for (let i = 0; i < jaRows.length; i++) {
    const r = jaRows[i];
    if (!r) continue;
    const key = r[0] ?? "";
    const textId = r[1] ?? "";
    const jaText = r[2] ?? "";

    if (TOP_KEYS.has(key)) {
      outRows.push([key, textId, jaText]);
      continue;
    }

    const cnText = cnMap.get(textId);
    const enText = enMap.get(textId);

    if (SKIP_REG.test(jaText)) {
      outRows.push([key, textId, cnText || ""]);
      continue;
    }

    const c1 = cnText ?? "";
    let c2: string;

    if (shoutingReplace[textId] !== undefined) {
      c2 = shoutingReplace[textId]!;
    } else if (/^TEXT_\w+_\w+_(SAY|SAYTODO)_/.test(textId) && enText !== undefined) {
      c2 = enText;
    } else {
      c2 = c1.replace(SHOUTING_REG, (value) => {
        shoutingCollect.add(value);
        const match = enText?.match(/“(?<str>.+?)”/);
        return match?.groups?.["str"] ?? "";
      });
      if (shoutingCollect.has(c1) && /^TEXT_.+(?:TODO|SYSTEM)/.test(textId) && enText !== undefined) {
        c2 = enText;
      }
    }

    outRows.push([key, textId, c2]);
  }

  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, serializeCsv(outRows), "utf8");
}

function walkFolder(relDir: string): void {
  const fullJaDir = path.join(CSV_PATHS.ja, relDir);
  if (!fs.existsSync(fullJaDir)) return;

  for (const entry of fs.readdirSync(fullJaDir, { withFileTypes: true })) {
    if (entry.name === ".git") continue;
    const relPath = path.join(relDir, entry.name);
    if (entry.isDirectory()) {
      walkFolder(relPath);
    } else if (entry.name.endsWith(".csv")) {
      processFolderCsv(relPath);
    }
  }
}

function detectNewCsvTables(): void {
  const knownFiles = new Set(Object.keys(csvName).map((name) => `${name}.csv`));
  const chsFiles = fs
    .readdirSync(CSV_PATHS.cn)
    .filter((f) => f.endsWith(".csv") && !knownFiles.has(f) && !IGNORED_ROOT_CSVS.has(f));

  const newTables: string[] = [];
  for (const fileName of chsFiles) {
    const jaPath = path.join(CSV_PATHS.ja, fileName);
    const enPath = path.join(CSV_PATHS.en, fileName);
    if (!fs.existsSync(jaPath) || !fs.existsSync(enPath)) continue;

    const cnText = fs.readFileSync(path.join(CSV_PATHS.cn, fileName), "utf8");
    const headerLines = cnText.split(/\r?\n/, 5);
    if (headerLines.length < 5 || !headerLines[3]?.includes("String")) continue;

    if (!/[\u4e00-\u9fa5]/.test(cnText)) continue;
    const jaText = fs.readFileSync(jaPath, "utf8");
    const enText = fs.readFileSync(enPath, "utf8");
    if (cnText !== jaText && cnText !== enText) {
      newTables.push(fileName.replace(/\.csv$/, ""));
    }
  }

  if (newTables.length > 0) {
    console.warn(`\n[提示] 检测到解包目录中存在未收录至 scripts/names.ts 的新中文表（共 ${newTables.length} 个）：`);
    for (const table of newTables) {
      console.warn(`  - ${table}`);
    }
    console.warn("");
  }
}

const CRC32_TABLE: Uint32Array = (() => {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) {
      c = (c & 1) !== 0 ? (0xedb88320 ^ (c >>> 1)) >>> 0 : c >>> 1;
    }
    table[i] = c >>> 0;
  }
  return table;
})();

function crc32(buf: Buffer): number {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    c = (c >>> 8) ^ CRC32_TABLE[(c ^ buf[i]!) & 0xff]!;
  }
  return (c ^ 0xffffffff) >>> 0;
}

// 固定 ZIP 内时间戳为 1980-01-01，保证相同内容生成完全相同的快照包。
const ZIP_DOS_TIME = 0;
const ZIP_DOS_DATE = 33;

function sha256File(filePath: string): { sha256: string; size: number } {
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

function writeRulesFile(): void {
  const rules = {
    specialWord,
    materiaMap,
  };
  fs.writeFileSync(RULES_PATH, `${JSON.stringify(rules, null, 2)}\n`, "utf8");
  console.log(`已生成运行期规则数据: ${path.relative(RESOURCE_PATH, RULES_PATH)}`);
}

function writeTablesFile(): void {
  const files = listRelativeFiles(OUTPUT_PATH, "")
    .map((file) => file.replace(/\.csv$/i, ""))
    .sort();
  const doc = {
    folders: folderName,
    tables: csvName,
    files,
  };
  fs.writeFileSync(TABLES_PATH, `${JSON.stringify(doc, null, 2)}\n`, "utf8");
  console.log(`已生成表名索引: ${path.relative(RESOURCE_PATH, TABLES_PATH)}（${Object.keys(csvName).length} 个根表，${files.length} 个文件）`);
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

function generateManifestAndSnapshot(): void {
  const zipEntries: ZipEntry[] = listRelativeFiles(OUTPUT_PATH, "rawexd").map((rel) => ({
    name: rel,
    absPath: path.join(RESOURCE_PATH, rel),
  }));
  const zipData = buildZip(zipEntries);
  fs.writeFileSync(ZIP_PATH, zipData);
  const zipInfo = { sha256: createHash("sha256").update(zipData).digest("hex"), size: zipData.length, count: zipEntries.length };

  const rulesInfo = sha256File(RULES_PATH);
  const tablesInfo = sha256File(TABLES_PATH);

  const rootHash = createHash("sha256");
  rootHash.update(`rawexd.zip:${zipInfo.sha256}\n`);
  rootHash.update(`rules.json:${rulesInfo.sha256}\n`);
  rootHash.update(`tables.json:${tablesInfo.sha256}\n`);

  // 字体不再随资源包分发：汉化时由主程序内置的 FFXIV-FontChanger 按方案编译。
  const manifest = {
    generatedAt: new Date().toISOString().replace(/\.\d{3}Z$/, "Z"),
    rootHash: rootHash.digest("hex"),
    rawexd: zipInfo,
    rules: { sha256: rulesInfo.sha256, size: rulesInfo.size },
    tables: { sha256: tablesInfo.sha256, size: tablesInfo.size },
  };
  fs.writeFileSync(MANIFEST_PATH, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
  console.log(`已生成同步清单: ${path.relative(RESOURCE_PATH, MANIFEST_PATH)}`);
  console.log(`已生成全量快照包: ${path.relative(RESOURCE_PATH, ZIP_PATH)}（${zipEntries.length} 个文件，${(zipInfo.size / 1024 / 1024).toFixed(2)} MB）`);
}

function main(): void {
  const args = new Set(process.argv.slice(2));
  const singleFileArg = process.argv.slice(2).find((a) => a.endsWith(".csv"));

  if (singleFileArg) {
    const baseName = path.basename(singleFileArg);
    console.log(`仅处理指定根目录 CSV 文件: ${baseName}`);
    processRootCsv(baseName);
    if (globalSummaryWarnings.length > 0) {
      console.warn("\n" + "=".repeat(80));
      console.warn("[注意] CSV 数据处理完成，但检测到以下未汉化词条，请及时处理：");
      for (const msg of globalSummaryWarnings) {
        console.warn(msg);
      }
      console.warn("=".repeat(80) + "\n");
    }
    return;
  }

  const runRoot = args.size === 0 || args.has("--root");
  const runFolder = args.size === 0 || args.has("--folder");

  if (runRoot) {
    detectNewCsvTables();
    const fileNames = Object.keys(csvName).map((name) => `${name}.csv`);
    console.log(`开始处理根目录 CSV 文件（共 ${fileNames.length} 个）...`);
    for (const fileName of fileNames) {
      processRootCsv(fileName);
    }
    console.log("根目录 CSV 文件处理完成。");
  }

  if (runFolder) {
    const folders = Object.keys(folderName);
    console.log(`开始处理子目录 CSV 文件（共 ${folders.length} 个目录）...`);
    for (const folder of folders) {
      walkFolder(folder);
    }
    console.log("子目录 CSV 文件处理完成。");
  }

  writeRulesFile();
  writeTablesFile();
  generateManifestAndSnapshot();

  if (globalSummaryWarnings.length > 0) {
    console.warn("\n" + "=".repeat(80));
    console.warn("[注意] CSV 数据处理完成，但检测到以下未汉化词条，请及时处理：");
    for (const msg of globalSummaryWarnings) {
      console.warn(msg);
    }
    console.warn("=".repeat(80) + "\n");
  }
}

main();

