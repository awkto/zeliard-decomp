Zeliard (C port) @VERSION@ for Windows, 64-bit
===============================================

This is a from-scratch C port of the 1987 Game Arts game Zeliard.  It does NOT
include the game.  You need your own copy of the original DOS game files
(ZELRES1.SAR, GAME.BIN, STDPLY.BIN, the *.USR and *.BIN files, and so on).

Running
-------
1. Copy the original game files into a folder, for example C:\Games\Zeliard.
   File-name case does not matter.
2. Open a Command Prompt (or PowerShell) in the folder holding zeliard.exe:

       zeliard.exe --dir C:\Games\Zeliard

   Run  zeliard.exe --help  for every option: display filters, video modes,
   key and controller remapping, windowed/fullscreen.
3. Settings are kept in %APPDATA%\awkto\zeliard (or next to the program, if
   SDL cannot find that folder).  Command-line flags override them.

Files in this zip
-----------------
zeliard.exe        the game (console subsystem: messages appear in the terminal)
SDL2.dll           SDL 2.30.9 (zlib licence, see SDL2-LICENSE.txt)
LICENSE            MIT licence of the port
README.txt         this file

Not in this build: save sync (--sync) says "built without network support".

Source and issues: https://github.com/awkto/zeliard-decomp
