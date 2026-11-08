#pragma once

#include <SDL2/SDL.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// Things the player can do from the keyboard or a controller.
enum class Action : std::size_t
{
    Up,
    Down,
    Left,
    Right,
    A,
    B,
    Select,
    Start,
    TiltUp,
    TiltDown,
    TiltLeft,
    TiltRight,
    FastForward,
    Pause,
    SaveState,
    LoadState,
    Fullscreen,
    Quit,
    Count
};

constexpr std::size_t ACTION_COUNT = static_cast<std::size_t>(Action::Count);

// The name used for an action in the controls file, such as "fast_forward".
const char *action_name(Action action);

struct KeyBindings
{
    std::array<std::vector<SDL_Keycode>, ACTION_COUNT> keys;
};

KeyBindings default_key_bindings();

// Reads "action = Key, Key" lines, where # starts a comment and keys use SDL's
// names ("Z", "Return", "Left Shift", "Keypad 8"). Each action listed replaces
// its default keys. On a bad line or a key bound to two actions, returns false
// with a description and leaves the bindings unchanged.
bool parse_key_bindings(const std::string &text, KeyBindings &bindings, std::string &error);

std::optional<Action> action_for_key(const KeyBindings &bindings, SDL_Keycode key);

// Controllers use a fixed layout: the D-pad moves, A and B are A and B, Back
// is Select, and Start is Start.
std::optional<Action> action_for_controller_button(SDL_GameControllerButton button);
