package org.motorstormpr.rift;

import org.libsdl.app.SDLActivity;

/** SDL's Java launcher. libSDL2.so carries the HID glue, so only SDL2 and the port (libmain.so) are loaded. */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
