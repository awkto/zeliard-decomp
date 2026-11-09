/* map_widths.c -- step 0 of the widescreen plan (docs/WIDESCREEN.md section 7): the width of every
 * system map next to the 36-column ring, so a map narrower than the ring is known before the wide
 * view tries to draw it.  `map_widths GAMEDIR` prints one line per map. */
#include <stdio.h>
#include "map.h"

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../zeliard";
    int narrow = 0;
    printf("idx name  width kind  ring36\n");
    for (int i = 0; i < 0x1F; i++) {
        static Map m;
        if (map_load_system(&m, dir, i) != 0) { printf("%3d  (load failed)\n", i); continue; }
        int boss = (m.lvl_flags & 0x80) != 0;
        printf("%3d  %-5s %5d %-6s %s\n", i, m.name, m.width, boss ? "boss" : "cavern",
               m.width < 36 ? (boss ? "NARROW (boss, stays 28 view)" : "NARROW") : "ok");
        if (!boss && m.width < 36) narrow++;
        map_free(&m);
    }
    printf("cavern maps narrower than the ring: %d\n", narrow);
    return 0;
}
