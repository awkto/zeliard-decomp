import com.awkto.zeliard.port.GameFiles;
import java.io.*;
import java.nio.file.*;
import java.util.zip.*;

/** Plain-JVM test of GameFiles: ./packaging/android/test/run.sh */
public class GameFilesTest {
    static int fails;
    static void check(boolean ok, String what) { System.out.println((ok ? "ok   " : "FAIL ") + what); if (!ok) fails++; }

    static byte[] zip(String... nameContent) throws IOException {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        try (ZipOutputStream z = new ZipOutputStream(bo)) {
            for (int i = 0; i < nameContent.length; i += 2) {
                z.putNextEntry(new ZipEntry(nameContent[i]));
                z.write(nameContent[i + 1].getBytes());
                z.closeEntry();
            }
        }
        return bo.toByteArray();
    }

    static File tmp() throws IOException { return Files.createTempDirectory("gft").toFile(); }
    static String read(File f) throws IOException { return new String(Files.readAllBytes(f.toPath())); }

    public static void main(String[] a) throws Exception {
        check(GameFiles.foldName("zelres1.sar").equals("ZELRES1.SAR"), "fold lower");
        check(GameFiles.foldName("ZelRes1.Sar").equals("ZELRES1.SAR"), "fold mixed");

        // zip with a top folder, mixed case, junk entries
        File st = tmp(), game = tmp();
        byte[] z = zip("Zeliard/zelres1.sar", "A", "Zeliard/ZelRes2.SAR", "B", "Zeliard/zelres3.sar", "C",
                       "Zeliard/game.bin", "G", "Zeliard/.DS_Store", "x", "__MACOSX/Zeliard/._zelres1.sar", "x",
                       "readme.txt", "hi");
        long n = GameFiles.extractZip(new ByteArrayInputStream(z), st);
        check(n > 0, "extract returns bytes");
        check(GameFiles.locateGameDir(st) != null, "locate in subfolder");
        check(GameFiles.install(st, game) == 4, "install 4 files (no junk)");
        check(GameFiles.isGameDir(game), "isGameDir after install");
        check(new File(game, "ZELRES1.SAR").isFile() && new File(game, "GAME.BIN").isFile(), "names folded");
        check(!new File(game, "readme.txt").exists() && !new File(game, "README.TXT").exists(), "stray top-level not installed");

        // re-import over an adb-pushed lower-case copy; saves survive
        File g2 = tmp();
        Files.write(new File(g2, "zelres1.sar").toPath(), "OLD".getBytes());
        Files.write(new File(g2, "SAVE.USR").toPath(), "SAVE".getBytes());
        File st2 = tmp();
        GameFiles.extractZip(new ByteArrayInputStream(z), st2);
        GameFiles.install(st2, g2);
        check(read(new File(g2, "ZELRES1.SAR")).equals("A"), "replaced");
        check(!new File(g2, "zelres1.sar").exists() || new File(g2, "ZELRES1.SAR").exists() && g2.list().length == 5, "no case duplicate");
        check(read(new File(g2, "SAVE.USR")).equals("SAVE"), "save kept");

        // zip-slip
        boolean threw = false;
        try { GameFiles.extractZip(new ByteArrayInputStream(zip("../evil.txt", "x")), tmp()); } catch (IOException e) { threw = true; }
        check(threw, "zip-slip rejected");
        threw = false;
        try { GameFiles.extractZip(new ByteArrayInputStream(zip("a/../../evil.txt", "x")), tmp()); } catch (IOException e) { threw = true; }
        check(threw, "nested zip-slip rejected");
        File st3 = tmp();
        GameFiles.extractZip(new ByteArrayInputStream(zip("/abs/zelres1.sar", "x")), st3);
        check(new File(st3, "ABS/ZELRES1.SAR").isFile(), "absolute path made relative");

        // invalid: missing archive
        File st4 = tmp();
        GameFiles.extractZip(new ByteArrayInputStream(zip("zelres1.sar", "A", "zelres2.sar", "B")), st4);
        check(GameFiles.install(st4, tmp()) == -1, "two of three archives is invalid");
        check(GameFiles.install(tmp(), tmp()) == -1, "empty is invalid");

        // flat zip (files at top), backslash separators
        File st5 = tmp(), g5 = tmp();
        GameFiles.extractZip(new ByteArrayInputStream(zip("ZEL\\zelres1.sar", "A", "ZEL\\zelres2.sar", "B", "ZEL\\zelres3.sar", "C")), st5);
        check(GameFiles.install(st5, g5) == 3 && GameFiles.isGameDir(g5), "backslash paths");

        // size cap
        threw = false;
        try { GameFiles.copy(new ByteArrayInputStream(new byte[100]), new File(tmp(), "X"), 50); } catch (IOException e) { threw = true; }
        check(threw, "per-file cap");

        // too-deep game dir is not found
        File st6 = tmp();
        GameFiles.extractZip(new ByteArrayInputStream(zip("a/b/c/d/e/zelres1.sar", "A")), st6);
        check(GameFiles.locateGameDir(st6) == null, "too deep ignored");

        System.out.println(fails == 0 ? "ALL PASS" : fails + " FAILED");
        System.exit(fails == 0 ? 0 : 1);
    }
}
