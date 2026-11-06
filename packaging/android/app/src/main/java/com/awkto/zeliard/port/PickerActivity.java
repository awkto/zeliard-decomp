package com.awkto.zeliard.port;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.InputStream;

/**
 * The launcher activity.  If the app's game dir already holds the three
 * ZELRES archives it hands straight over to ZeliardActivity (SDL + the native
 * port).  Otherwise, or when started from the "Choose game files" launcher
 * shortcut (EXTRA_REPICK), it asks the user to pick the original game folder
 * or a .zip through the Storage Access Framework (no storage permission
 * needed), copies the files into files/game, and then starts the game.  The
 * native side is unchanged: it still reads <external files dir>/game.
 */
public class PickerActivity extends Activity {
    static final String EXTRA_REPICK = "repick";
    private static final int REQ_FOLDER = 1, REQ_ZIP = 2;

    private TextView msg;
    private Button bFolder, bZip;
    private boolean busy;

    private File gameDir() {
        File ext = getExternalFilesDir(null);
        if (ext == null) ext = getFilesDir();
        return new File(ext, "game");
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        boolean repick = getIntent().getBooleanExtra(EXTRA_REPICK, false);
        if (!repick && GameFiles.isGameDir(gameDir())) { startGame(); return; }

        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setGravity(Gravity.CENTER);
        l.setPadding(48, 24, 48, 24);
        msg = new TextView(this);
        msg.setTextSize(16);
        msg.setGravity(Gravity.CENTER);
        msg.setText(getString(repick && GameFiles.isGameDir(gameDir()) ? R.string.picker_repick : R.string.picker_first));
        bFolder = new Button(this);
        bFolder.setText(R.string.picker_folder);
        bFolder.setOnClickListener(v -> pickFolder());
        bZip = new Button(this);
        bZip.setText(R.string.picker_zip);
        bZip.setOnClickListener(v -> pickZip());
        l.addView(msg); l.addView(bFolder); l.addView(bZip);
        if (repick && GameFiles.isGameDir(gameDir())) {
            Button play = new Button(this);
            play.setText(R.string.picker_play);
            play.setOnClickListener(v -> startGame());
            l.addView(play);
        }
        setContentView(l);
    }

    private void startGame() {
        startActivity(new Intent(this, ZeliardActivity.class));
        finish();
    }

    private void pickFolder() {
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_FOLDER);
    }

    private void pickZip() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
            "application/zip", "application/x-zip-compressed", "application/octet-stream" });
        startActivityForResult(i, REQ_ZIP);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null || busy) return;
        final Uri uri = data.getData();
        busy = true;
        bFolder.setEnabled(false); bZip.setEnabled(false);
        msg.setText(R.string.picker_copying);
        new Thread(() -> {
            String err = null;
            File staging = new File(getCacheDir(), "import");
            try {
                GameFiles.deleteTree(staging);
                staging.mkdirs();
                if (req == REQ_ZIP) {
                    try (InputStream in = getContentResolver().openInputStream(uri)) {
                        GameFiles.extractZip(in, staging);
                    }
                } else {
                    String doc = findGameDoc(uri, DocumentsContract.getTreeDocumentId(uri), 0);
                    if (doc != null) copyFolder(uri, doc, staging);
                }
                if (GameFiles.install(staging, gameDir()) < 0) err = getString(R.string.picker_invalid);
                else if (!GameFiles.isGameDir(gameDir())) err = getString(R.string.picker_invalid);
            } catch (Exception e) {
                err = getString(R.string.picker_failed) + "\n" + e.getMessage();
            } finally {
                GameFiles.deleteTree(staging);
            }
            final String fe = err;
            runOnUiThread(() -> {
                busy = false;
                if (fe == null) { startGame(); return; }
                msg.setText(fe + "\n\n" + getString(R.string.picker_first));
                bFolder.setEnabled(true); bZip.setEnabled(true);
            });
        }, "zeliard-import").start();
    }

    private static final String[] KID_COLS = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE };

    /** Document id of the folder (the pick itself or up to MAX_DEPTH levels down)
     *  that directly holds ZELRES1.SAR, or null.  Listing only: nothing is copied
     *  until the right folder is known, so picking e.g. Downloads stays cheap. */
    private String findGameDoc(Uri tree, String docId, int depth) {
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        java.util.ArrayList<String> dirs = new java.util.ArrayList<>();
        try (Cursor c = getContentResolver().query(kids, KID_COLS, null, null, null)) {
            if (c == null) return null;
            while (c.moveToNext()) {
                String name = c.getString(1);
                if (name == null || GameFiles.skipName(name)) continue;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))) dirs.add(c.getString(0));
                else if (name.equalsIgnoreCase(GameFiles.REQUIRED[0])) return docId;
            }
        }
        if (depth >= GameFiles.MAX_DEPTH) return null;
        for (String d : dirs) { String r = findGameDoc(tree, d, depth + 1); if (r != null) return r; }
        return null;
    }

    /** Copy the plain files of one SAF folder into dest (flat). */
    private void copyFolder(Uri tree, String docId, File dest) throws Exception {
        ContentResolver cr = getContentResolver();
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        long total = 0;
        try (Cursor c = cr.query(kids, KID_COLS, null, null, null)) {
            if (c == null) return;
            while (c.moveToNext()) {
                String name = c.getString(1);
                if (name == null || GameFiles.skipName(name) || name.contains("/")
                        || DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))) continue;
                Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0));
                try (InputStream in = cr.openInputStream(doc)) {
                    total += GameFiles.copy(in, new File(dest, GameFiles.foldName(name)),
                            Math.min(GameFiles.MAX_FILE, GameFiles.MAX_TOTAL - total));
                }
            }
        }
    }
}
