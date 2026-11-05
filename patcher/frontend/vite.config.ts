import { defineConfig } from "vite";
import fs from "node:fs";
import path from "node:path";

const repoRoot = path.resolve(__dirname, "..", "..");

export default defineConfig({
  base: "./",
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2022",
  },
  server: {
    // font_data.ts 直接引用 tools/fontchanger 下的上游数据文件，dev server 需要放行仓库根目录。
    fs: { allow: [repoRoot] },
  },
  plugins: [
    {
      name: "preserve-gitkeep",
      closeBundle() {
        fs.writeFileSync(path.resolve(__dirname, "dist/.gitkeep"), "\n");
      },
    },
  ],
});
