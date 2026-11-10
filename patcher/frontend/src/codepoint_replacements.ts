export function parseReplacementKey(raw: string): string {
  const key = raw.trim();
  if (!key) return "";
  const hexMatch = key.match(/^(?:U\+|0x)([0-9A-Fa-f]+)$/i);
  if (hexMatch) {
    const cp = parseInt(hexMatch[1], 16);
    if (Number.isInteger(cp) && cp >= 0 && cp <= 0x10ffff) {
      return String.fromCodePoint(cp);
    }
    return "";
  }
  const firstCp = key.codePointAt(0);
  return firstCp !== undefined ? String.fromCodePoint(firstCp) : "";
}

export function formatReplacementKeyHex(key: string): string {
  const cp = key.codePointAt(0);
  return cp !== undefined ? `U+${cp.toString(16).toUpperCase().padStart(4, "0")}` : key;
}

export function formatReplacementsToText(rep: Record<string, string> | undefined): string {
  if (!rep) return "";
  return Object.entries(rep)
    .map(([k, v]) => `${k} = ${v}`)
    .join("\n");
}

export function parseTextToReplacements(text: string): Record<string, string> {
  const result: Record<string, string> = {};
  const lines = text.split(/\r?\n/);
  for (const line of lines) {
    const idx = line.indexOf("=");
    if (idx <= 0) continue;
    const key = parseReplacementKey(line.slice(0, idx));
    const val = line.slice(idx + 1).trim();
    if (!key || !val) continue;
    result[key] = val;
  }
  return result;
}

export function nextReplacementKey(dict: Record<string, string> | undefined, baseCp = 0xe0b0): string {
  let cp = baseCp;
  while (dict && dict[String.fromCodePoint(cp)] !== undefined) {
    cp++;
  }
  return String.fromCodePoint(cp);
}

export interface ReplacementEditResult {
  dict: Record<string, string> | undefined;
  nextKey: string;
  needsRerender: boolean;
}

export function applyReplacementRowEdit(
  dict: Record<string, string> | undefined,
  prevKey: string,
  rawSrcInput: string,
  targetVal: string,
): ReplacementEditResult {
  const nextKey = parseReplacementKey(rawSrcInput);
  const nextDict: Record<string, string> = { ...(dict || {}) };
  const keyChanged = prevKey !== nextKey;

  if (keyChanged && prevKey) {
    delete nextDict[prevKey];
  }

  const mergedConflict = keyChanged && nextKey !== "" && Object.prototype.hasOwnProperty.call(nextDict, nextKey);

  if (nextKey && targetVal) {
    nextDict[nextKey] = targetVal;
  } else if (nextKey && !mergedConflict) {
    delete nextDict[nextKey];
  }

  return {
    dict: Object.keys(nextDict).length > 0 ? nextDict : undefined,
    nextKey,
    needsRerender: mergedConflict,
  };
}
