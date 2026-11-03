package com.awkto.zeliard.port;

import org.libsdl.app.SDLActivity;

/** SDLActivity owns the window, input, touch, audio and controllers; the C port
 *  (port/*.c) is built as libmain.so and entered through SDL_main. */
public class ZeliardActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
