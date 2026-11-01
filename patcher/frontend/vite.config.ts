import { defineConfig } from "vite";
import fs from "node:fs";
import path from "node:path";

export default defineConfig({
  base: "./",
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2022",
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
