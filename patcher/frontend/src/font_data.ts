// 游戏字体数据。
// - 字体类型表直接引用仓库里由 scripts/update_fontchanger.ts 同步的上游文件
//   （tools/fontchanger/FontChanger.FixedSizeFont/data/game_fonts.json），仓库内不再另存副本；
// - 字体度数是预览用的近似快照（行高/基线主要由字号推算，字形数按字族），
//   只影响编辑器里的列显示；写进客户端的字体数据由后端 fontengine 生成。
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

interface GameFontTypeDefinition {
  textures: string;
  textureCount: number;
  fonts: Array<[string, string, number]>;
}

const GAME_FONT_TYPES = gameFonts.fontTypes as unknown as Record<string, GameFontTypeDefinition>;

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
