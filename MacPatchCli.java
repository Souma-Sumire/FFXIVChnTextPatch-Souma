import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import name.yumao.ffxiv.chn.replace.ReplaceEXDF;
import name.yumao.ffxiv.chn.replace.ReplaceFont;
import name.yumao.ffxiv.chn.swing.PercentPanel;
import name.yumao.ffxiv.chn.thread.ReplaceThread;
import name.yumao.ffxiv.chn.util.res.Config;

public class MacPatchCli {
  private static final String[] BACKUP_FILES = {
    "000000.win32.dat0",
    "000000.win32.index",
    "000000.win32.index2",
    "0a0000.win32.dat0",
    "0a0000.win32.index",
    "0a0000.win32.index2"
  };

  public static void main(String[] args) throws Exception {
    System.setProperty("java.awt.headless", "false");
    Config.setConfigResource("conf" + File.separator + "global.properties");

    String gamePath = Config.getProperty("GamePath");
    if (gamePath == null || gamePath.isBlank()) {
      throw new IllegalStateException("GamePath is empty in conf/global.properties");
    }

    Path gameDir = Path.of(gamePath);
    Path dx11 = gameDir.resolve("game").resolve("ffxiv_dx11.exe");
    Path resourceFolder = gameDir.resolve("game").resolve("sqpack").resolve("ffxiv");
    if (!Files.isRegularFile(dx11)) {
      throw new IllegalStateException("Not a valid FFXIV game root: " + gamePath);
    }
    if (!Files.isRegularFile(resourceFolder.resolve("0a0000.win32.index"))) {
      throw new IllegalStateException("Missing sqpack/ffxiv/0a0000.win32.index under: " + gamePath);
    }

    backup(resourceFolder);

    PercentPanel percentPanel = new PercentPanel("Patching");
    try {
      if ("1".equals(Config.getProperty("ReplaFont"))) {
        new ReplaceFont(
          resourceFolder.resolve("000000.win32.index").toString(),
          "resource" + File.separator + "font",
          percentPanel
        ).replace();
      }

      if ("1".equals(Config.getProperty("ReplaText"))) {
        String flang = Config.getProperty("FLanguage");
        if ("CSV".equals(flang) && ReplaceThread.hasCsvFiles("resource" + File.separator + "rawexd")) {
          new ReplaceEXDF(
            resourceFolder.resolve("0a0000.win32.index").toString(),
            "resource" + File.separator + "rawexd" + File.separator + "Achievement.csv",
            percentPanel
          ).replace();
        } else {
          Path textIndex = Path.of("resource", "text", "0a0000.win32.index");
          if (!Files.isRegularFile(textIndex)) {
            throw new IllegalStateException("Missing " + textIndex + "; set FLanguage=CSV or provide resource/text files.");
          }
          new ReplaceEXDF(
            resourceFolder.resolve("0a0000.win32.index").toString(),
            textIndex.toString(),
            percentPanel
          ).replace();
        }
      }
    } finally {
      percentPanel.dispose();
    }

    System.out.println("Patch finished.");
  }

  private static void backup(Path resourceFolder) throws Exception {
    Path backupDir = Path.of("backup");
    Files.createDirectories(backupDir);
    for (String fileName : BACKUP_FILES) {
      Path source = resourceFolder.resolve(fileName);
      Path target = backupDir.resolve(fileName);
      if (Files.isRegularFile(source) && !Files.exists(target)) {
        Files.copy(source, target, StandardCopyOption.COPY_ATTRIBUTES);
        System.out.println("Backed up " + fileName);
      }
    }
  }
}
