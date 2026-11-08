#include "Controls.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{
    struct ActionInfo
    {
        const char *name;
        std::vector<SDL_Keycode> default_keys;
    };

    // In Action order.
    const std::array<ActionInfo, ACTION_COUNT> &action_info()
    {
        static const std::array<ActionInfo, ACTION_COUNT> info = {{
            {"up", {SDLK_UP}},
            {"down", {SDLK_DOWN}},
            {"left", {SDLK_LEFT}},
            {"right", {SDLK_RIGHT}},
            {"a", {SDLK_z}},
            {"b", {SDLK_x}},
            {"select", {SDLK_RSHIFT}},
            {"start", {SDLK_RETURN}},
            {"tilt_up", {SDLK_i}},
            {"tilt_down", {SDLK_k}},
            {"tilt_left", {SDLK_j}},
            {"tilt_right", {SDLK_l}},
            {"fast_forward", {SDLK_TAB}},
            {"pause", {SDLK_p}},
            {"save_state", {SDLK_F5}},
            {"load_state", {SDLK_F8}},
            {"fullscreen", {SDLK_F11}},
            {"color_correction", {SDLK_F9}},
            {"quit", {SDLK_ESCAPE}},
        }};
        return info;
    }

    std::string trim(const std::string &text)
    {
        const auto not_space = [](unsigned char c) { return !std::isspace(c); };
        const auto begin = std::find_if(text.begin(), text.end(), not_space);
        const auto end = std::find_if(text.rbegin(), text.rend(), not_space).base();
        return begin < end ? std::string(begin, end) : std::string();
    }

    std::optional<Action> action_from_name(const std::string &name)
    {
        for (std::size_t index = 0; index < ACTION_COUNT; ++index)
        {
            if (name == action_info()[index].name)
            {
                return static_cast<Action>(index);
            }
        }
        return std::nullopt;
    }
}

const char *action_name(Action action)
{
    return action_info()[static_cast<std::size_t>(action)].name;
}

KeyBindings default_key_bindings()
{
    KeyBindings bindings;
    for (std::size_t index = 0; index < ACTION_COUNT; ++index)
    {
        bindings.keys[index] = action_info()[index].default_keys;
    }
    return bindings;
}

bool parse_key_bindings(const std::string &text, KeyBindings &bindings, std::string &error)
{
    KeyBindings parsed = bindings;
    std::istringstream lines(text);
    std::string line;
    int line_number = 0;
    while (std::getline(lines, line))
    {
        ++line_number;
        line = trim(line.substr(0, line.find('#')));
        if (line.empty())
        {
            continue;
        }
        const std::string where = "line " + std::to_string(line_number) + ": ";
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
        {
            error = where + "expected \"action = key\".";
            return false;
        }
        const std::string name = trim(line.substr(0, equals));
        const std::optional<Action> action = action_from_name(name);
        if (!action)
        {
            error = where + "unknown action \"" + name + "\".";
            return false;
        }

        std::vector<SDL_Keycode> keys;
        std::istringstream key_names(line.substr(equals + 1));
        std::string key_name;
        while (std::getline(key_names, key_name, ','))
        {
            key_name = trim(key_name);
            const SDL_Keycode key = SDL_GetKeyFromName(key_name.c_str());
            if (key == SDLK_UNKNOWN)
            {
                error = where + "unknown key \"" + key_name + "\".";
                return false;
            }
            keys.push_back(key);
        }
        if (keys.empty())
        {
            error = where + "no keys given for " + name + ".";
            return false;
        }
        parsed.keys[static_cast<std::size_t>(*action)] = keys;
    }

    for (std::size_t first = 0; first < ACTION_COUNT; ++first)
    {
        for (const SDL_Keycode key : parsed.keys[first])
        {
            for (std::size_t second = first + 1; second < ACTION_COUNT; ++second)
            {
                const std::vector<SDL_Keycode> &other = parsed.keys[second];
                if (std::find(other.begin(), other.end(), key) != other.end())
                {
                    error = std::string(SDL_GetKeyName(key)) + " is bound to both " + action_info()[first].name +
                            " and " + action_info()[second].name + ".";
                    return false;
                }
            }
        }
    }
    bindings = parsed;
    return true;
}

std::optional<Action> action_for_key(const KeyBindings &bindings, SDL_Keycode key)
{
    for (std::size_t index = 0; index < ACTION_COUNT; ++index)
    {
        const std::vector<SDL_Keycode> &keys = bindings.keys[index];
        if (std::find(keys.begin(), keys.end(), key) != keys.end())
        {
            return static_cast<Action>(index);
        }
    }
    return std::nullopt;
}

std::optional<Action> action_for_controller_button(SDL_GameControllerButton button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return Action::Up;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return Action::Down;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return Action::Left;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return Action::Right;
    case SDL_CONTROLLER_BUTTON_A:
        return Action::A;
    case SDL_CONTROLLER_BUTTON_B:
        return Action::B;
    case SDL_CONTROLLER_BUTTON_BACK:
        return Action::Select;
    case SDL_CONTROLLER_BUTTON_START:
        return Action::Start;
    default:
        return std::nullopt;
    }
}
