// 游戏字体数据。
// - 字形合并形状、字体类型表直接引用仓库里由 scripts/update_fontchanger.ts 同步的上游文件
//   （tools/fontchanger/FontChanger.FixedSizeFont/data/*.json），仓库内不再另存副本；
// - 字体度数是预览用的近似快照（行高/基线主要由字号推算，字形数按字族），
//   只影响编辑器里的列显示与预览排版；写进客户端的字体数据由后端 fontengine 生成。
import glyphMergeShapes from "../../../tools/fontchanger/FontChanger.FixedSizeFont/data/glyph_merge_shapes.json";
import gameFonts from "../../../tools/fontchanger/FontChanger.FixedSizeFont/data/game_fonts.json";
import gameFontMetrics from "./game_font_metrics.json";

export interface GameFontMetric {
  family: string;
  subfamily: string;
  size: number;
  lineHeight: number;
  ascent: number;
  glyphs: number;
}

export const GAME_FONT_METRICS: Record<string, GameFontMetric> = gameFontMetrics;

// 形状定义（advance / rect / radius / polygon / preset 等）随上游版本演进，这里不做强类型约束。
export const GLYPH_MERGE_SHAPES: any = glyphMergeShapes;

// 上游 game_fonts.json 的 fontTypes：每种字体类型（贴图文件名格式、贴图数量）与其字体面。
export interface GameFontTypeDefinition {
  textures: string;
  textureCount: number;
  fonts: Array<[string, string, number]>;
}

export const GAME_FONT_TYPES = gameFonts.fontTypes as unknown as Record<string, GameFontTypeDefinition>;

// 字体面的权威信息（家族、子家族、像素字号、所属字体类型），逐项取自上游 game_fonts.json：
//   fontTypes[<类型>].fonts = [字体面名, 家族, 字号]
//   families[<家族>].subfamily
// 不要再用字体面名字里的数字去猜字号（例如 TrumpGothic_184 是 18.4px，不是 184px）。
export interface GameFontFaceInfo {
  name: string;
  fontType: string;
  family: string;
  subfamily: string;
  size: number;
}

const families = gameFonts.families as unknown as Record<string, { subfamily?: string }>;

export const GAME_FONT_FACES: Record<string, GameFontFaceInfo> = (() => {
  const faces: Record<string, GameFontFaceInfo> = {};
  for (const [fontType, definition] of Object.entries(GAME_FONT_TYPES)) {
    for (const [name, family, size] of definition.fonts) {
      faces[name] = {
        name,
        fontType,
        family,
        subfamily: families[family]?.subfamily ?? "",
        size,
      };
    }
  }
  return faces;
})();


// 与 Structs::GetDefaultPreviewText() 逐字节一致（后面几行是私有区字符，用于展示字形合并结果）。
export const DEFAULT_PREVIEW_TEXT = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\r\n0英en-US: _The_ (89) quick brown foxes jump over the [01] lazy dogs.\r\n1日ja-JP: _パングラム_(pangram)で[23]つの字体（フォント）をテストします。\r\n2中zh-CN: _(天)地玄黃_，宇[宙]洪荒。蓋此身髮，4大5常。\r\n3韓ko-KR: 45 _다(람)쥐_ 67 헌 쳇바퀴에 타[고]파.\r\n4露ru-RU: Съешь (ж)е ещё этих мягких 23 [ф]ранцузских булок да 45 выпей чаю.\r\n5泰th-TH: เป็นมนุษย์สุดประเสริฐเลิศคุณค่า\r\n6\r\n7\r\n8\r\n9\r\n\r\n\r\n";
