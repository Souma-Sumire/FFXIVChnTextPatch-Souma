import fs from "node:fs";
import path from "node:path";

const rootDir = path.resolve(import.meta.dirname, "..");
const directoryPath = path.join(rootDir, "resource", "rawexd");
const filePath = path.join(rootDir, "hashPaths.txt");

function getAllFilePaths(dirPath: string): string[] {
  let filePaths: string[] = [];
  const files = fs.readdirSync(dirPath);

  files.forEach((file) => {
    const fullPath = path.join(dirPath, file);
    if (fs.statSync(fullPath).isDirectory()) {
      filePaths = filePaths.concat(getAllFilePaths(fullPath));
    } else {
      filePaths.push(fullPath);
    }
  });

  return filePaths;
}

const allFilePaths = getAllFilePaths(directoryPath);

const hashPaths = allFilePaths.map((v) =>
  path
    .relative(directoryPath, v)
    .replace(/^/, "exd/")
    .replaceAll(/\\/g, "/")
    .replaceAll(/\.csv$/g, "_0_en.exd")
);

const langHashPaths = [
  ...hashPaths,
  ...hashPaths.map((v) => v.replace("_0_en.exd", "_0_ja.exd")),
  ...hashPaths.map((v) => v.replace("_0_en.exd", "_0_de.exd")),
  ...hashPaths.map((v) => v.replace("_0_en.exd", "_0_fr.exd")),
  ...hashPaths.map((v) => v.replace("_0_en.exd", "_0_ko.exd")),
  ...hashPaths.map((v) => v.replace("_0_en.exd", "_0_tc.exd")),
];

fs.writeFileSync(
  filePath,
  [...langHashPaths, ...langHashPaths.map((v) => v.replace("_0_", "_1_")), ...langHashPaths.map((v) => v.replace("_0_en.exd", ".exh"))].join("\n"),
  "utf8"
);
console.log(`Hash paths saved to ${filePath}`);
