// Print the built-in pad's SDL joystick GUID and name, one per line.
//
// DSperate opens the pad through SDL_IsGameController / SDL_GameControllerOpen
// with no joystick fallback (src/frontend/sdl/input.cpp), and MinUI/NextUI ship
// no GameController mapping, so launch.sh has to supply one through
// SDL_GAMECONTROLLERCONFIG. A mapping line starts with the joystick's GUID,
// which SDL derives from evdev bus/vendor/product/version and hashes
// differently across SDL versions -- so it is read here at runtime rather than
// hardcoded per device.
//
// Prints nothing and exits 1 when no joystick is present, so the caller can
// tell "no pad" from "pad with an unknown GUID".
#include <SDL2/SDL.h>
#include <stdio.h>

int main(void) {
    // Joystick only, and never a window: this runs before the emulator and must
    // not touch the panel.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "pad-guid: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    int rc = 1;
    if (SDL_NumJoysticks() > 0) {
        char guid[33];
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(0), guid, sizeof(guid));
        const char *name = SDL_JoystickNameForIndex(0);
        printf("%s\n%s\n", guid, name ? name : "Controller");
        rc = 0;
    } else {
        fprintf(stderr, "pad-guid: no joystick found\n");
    }

    SDL_Quit();
    return rc;
}
