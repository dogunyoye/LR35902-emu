#include "Controls.h"

#include <cassert>

static void test_defaults()
{
    const KeyBindings bindings = default_key_bindings();
    assert(action_for_key(bindings, SDLK_z) == Action::A);
    assert(action_for_key(bindings, SDLK_RETURN) == Action::Start);
    assert(action_for_key(bindings, SDLK_TAB) == Action::FastForward);
    assert(action_for_key(bindings, SDLK_p) == Action::Pause);
    assert(!action_for_key(bindings, SDLK_q));
    assert(std::string(action_name(Action::FastForward)) == "fast_forward");
}

static void test_parsing_replaces_listed_actions()
{
    KeyBindings bindings = default_key_bindings();
    std::string error;
    assert(parse_key_bindings("# Comment\n"
                              "a = Space   # trailing comment\n"
                              "\n"
                              "start = Return, Keypad Enter\n"
                              "select = Left Shift\n",
                              bindings, error));
    assert(action_for_key(bindings, SDLK_SPACE) == Action::A);
    assert(!action_for_key(bindings, SDLK_z)); // The default is replaced
    assert(action_for_key(bindings, SDLK_KP_ENTER) == Action::Start);
    assert(action_for_key(bindings, SDLK_RETURN) == Action::Start);
    assert(action_for_key(bindings, SDLK_LSHIFT) == Action::Select);
    assert(action_for_key(bindings, SDLK_x) == Action::B); // Unlisted actions keep their defaults
}

static void test_bad_files_change_nothing()
{
    const char *bad_files[] = {
        "a Z\n",                // No '='
        "jump = Space\n",       // Unknown action
        "a = NotAKey\n",        // Unknown key
        "a =\n",                // No keys
        "a = Space\nb = Space\n" // One key bound twice
    };
    for (const char *text : bad_files)
    {
        KeyBindings bindings = default_key_bindings();
        std::string error;
        assert(!parse_key_bindings(text, bindings, error));
        assert(!error.empty());
        assert(action_for_key(bindings, SDLK_z) == Action::A);
        assert(!action_for_key(bindings, SDLK_SPACE));
    }

    KeyBindings bindings = default_key_bindings();
    std::string error;
    assert(!parse_key_bindings("a = X\n", bindings, error)); // Clashes with b's default
    assert(error == "X is bound to both a and b.");
}

static void test_controller_layout()
{
    assert(action_for_controller_button(SDL_CONTROLLER_BUTTON_A) == Action::A);
    assert(action_for_controller_button(SDL_CONTROLLER_BUTTON_BACK) == Action::Select);
    assert(action_for_controller_button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) == Action::Left);
    assert(!action_for_controller_button(SDL_CONTROLLER_BUTTON_GUIDE));
}

int main()
{
    test_defaults();
    test_parsing_replaces_listed_actions();
    test_bad_files_change_nothing();
    test_controller_layout();
    return 0;
}
