package com.awkto.zeliard.port;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Pure-Java (no android.* imports, so it runs on a plain JVM for the unit test)
 * helpers for importing the user's game files: zip extraction, name case
 * folding, locating the folder that holds the archives, and installing them
 * into the app's game dir.
 *
 * Names are folded to upper case, the DOS media's case.  The native side's
 * game_fopen() accepts any case anyway; folding just makes a re-import replace
 * the old copy instead of sitting next to it on the case-sensitive file system.
 */
public final class GameFiles {
    private GameFiles() {}

    /** The archives native shell_find_dir() requires; same list as port/shell.c. */
    public static final String[] REQUIRED = { "ZELRES1.SAR", "ZELRES2.SAR", "ZELRES3.SAR" };

    public static final long MAX_FILE = 64L << 20;    // the real files are a few MB
    public static final long MAX_TOTAL = 256L << 20;
    public static final int MAX_DEPTH = 3;            // how far below the pick the files may sit

    public static String foldName(String name) {
        return name.toUpperCase(Locale.ROOT);
    }

    /** True when dir holds all three archives (any case). */
    public static boolean isGameDir(File dir) {
        for (String r : REQUIRED) if (findCI(dir, r) == null) return false;
        return true;
    }

    /** The file in dir whose name equals name ignoring case, or null. */
    public static File findCI(File dir, String name) {
        File[] fs = dir.listFiles();
        if (fs == null) return null;
        for (File f : fs) if (f.isFile() && f.getName().equalsIgnoreCase(name)) return f;
        return null;
    }

    /** Names that are never game data: dotfiles, macOS resource forks. */
    public static boolean skipName(String base) {
        return base.isEmpty() || base.startsWith(".") || base.equals("__MACOSX");
    }

    /** Breadth-first search for the shallowest directory under root that is a game dir. */
    public static File locateGameDir(File root) {
        java.util.ArrayDeque<File> q = new java.util.ArrayDeque<>();
        java.util.ArrayDeque<Integer> depth = new java.util.ArrayDeque<>();
        q.add(root); depth.add(0);
        while (!q.isEmpty()) {
            File d = q.poll(); int n = depth.poll();
            if (isGameDir(d)) return d;
            if (n >= MAX_DEPTH) continue;
            File[] fs = d.listFiles();
            if (fs == null) continue;
            java.util.Arrays.sort(fs);
            for (File f : fs) if (f.isDirectory() && !skipName(f.getName())) { q.add(f); depth.add(n + 1); }
        }
        return null;
    }

    /** Copy in to out, failing when more than limit bytes would be written. Returns the count. */
    public static long copy(InputStream in, File out, long limit) throws IOException {
        long n = 0;
        byte[] buf = new byte[64 * 1024];
        try (OutputStream os = new FileOutputStream(out)) {
            int r;
            while ((r = in.read(buf)) > 0) {
                n += r;
                if (n > limit) throw new IOException("file too large: " + out.getName());
                os.write(buf, 0, r);
            }
        }
        return n;
    }

    /**
     * Extract a zip stream under dest, keeping its folder structure (at most
     * MAX_DEPTH levels deep), with every path component case-folded.  Entries
     * with ".." or absolute paths are rejected (zip-slip).  Returns the bytes written.
     */
    public static long extractZip(InputStream raw, File dest) throws IOException {
        long total = 0;
        File root = dest.getCanonicalFile();
        try (ZipInputStream zin = new ZipInputStream(raw)) {
            ZipEntry e;
            while ((e = zin.getNextEntry()) != null) {
                if (e.isDirectory()) continue;
                String[] parts = e.getName().replace('\\', '/').split("/");
                File target = root;
                boolean skip = false;
                int kept = 0;
                for (String p : parts) {
                    if (p.isEmpty() || p.equals(".")) continue;
                    if (p.equals("..")) throw new IOException("unsafe zip entry: " + e.getName());
                    if (skipName(p)) { skip = true; break; }
                    target = new File(target, foldName(p));
                    kept++;
                }
                if (skip || kept == 0) continue;
                if (kept > MAX_DEPTH + 1) continue;     // too deep to be the game dir
                if (!target.getCanonicalPath().startsWith(root.getPath() + File.separator))
                    throw new IOException("unsafe zip entry: " + e.getName());
                File parent = target.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("mkdir " + parent);
                total += copy(zin, target, Math.min(MAX_FILE, MAX_TOTAL - total));
            }
        }
        return total;
    }

    /**
     * Find the game dir inside the staging tree and move its files into
     * gameDir, replacing same-named files (any case) and leaving everything
     * else there alone, so saves (.USR) survive a re-import.  Returns the
     * number of files installed, or -1 when staging holds no game dir.
     */
    public static int install(File staging, File gameDir) throws IOException {
        File src = locateGameDir(staging);
        if (src == null) return -1;
        if (!gameDir.isDirectory() && !gameDir.mkdirs()) throw new IOException("mkdir " + gameDir);
        File[] fs = src.listFiles();
        int n = 0;
        if (fs != null) for (File f : fs) {
            if (!f.isFile() || skipName(f.getName())) continue;
            String folded = foldName(f.getName());
            File[] old = gameDir.listFiles();
            if (old != null) for (File o : old)
                if (o.isFile() && o.getName().equalsIgnoreCase(folded) && !o.delete())
                    throw new IOException("cannot replace " + o);
            File to = new File(gameDir, folded);
            if (!f.renameTo(to)) {
                try (InputStream in = new java.io.FileInputStream(f)) { copy(in, to, MAX_FILE); }
                f.delete();
            }
            n++;
        }
        return n;
    }

    public static void deleteTree(File f) {
        File[] fs = f.listFiles();
        if (fs != null) for (File c : fs) deleteTree(c);
        f.delete();
    }
}
