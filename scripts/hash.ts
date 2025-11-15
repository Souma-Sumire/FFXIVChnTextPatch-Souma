import fs from "fs";
import path from "path";

const directoryPath = "resource\\rawexd";
const filePath = "hashPaths.txt";

function getAllFilePaths(dirPath: string) {
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
  v
    .replaceAll(directoryPath, "exd")
    .replaceAll(/\\/g, "/")
    .replaceAll(/\.csv$/g, "_0_en.exd")
);

const langHashPaths = [
  ...hashPaths,
  ...hashPaths.map((v) => v.replace("_0_en.exd", "_0_ja.exd")),
  ...hashPaths.map((v) => v.replace("_0_en.exd", "_0_de.exd")),
  ...hashPaths.map((v) => v.replace("_0_en.exd", "_0_fr.exd")),
];

fs.writeFileSync(
  filePath,
  [...langHashPaths, ...langHashPaths.map((v) => v.replace("_0_", "_1_")), ...langHashPaths.map((v) => v.replace("_0_en.exd", ".exh"))].join("\n"),
  "utf8"
);
console.log(`Hash paths saved to ${filePath}`);
