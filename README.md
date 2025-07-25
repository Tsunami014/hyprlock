# hyprlock
Hyprland's simple, yet multi-threaded and GPU-accelerated screen locking utility.

# What I changed
I modified it from the original to let you type your password while it's checking it, like Linux Mint's functionality.

- When authenticating, the password input box is cleared, and the placeholder text changes to "Checking..." (configurable).
- You can configure the text shown during authentication with the `checking_text` option in your input-field config, e.g.:
  ```
  checking_text = Checking...
  ```

## Features
 - Uses the ext-session-lock protocol
 - Support for fractional-scale
 - Fully GPU accelerated
 - Multi-threaded resource acquisition
 - Blurred screenshot as the background
 - Native fingerprint support (using libfprint's dbus interface)
 - Some of Hyprland's eyecandy: gradient borders, blur, animations, shadows, etc.
 - and more...

## How it looks

![](https://i.ibb.co/8Bd98BP/20240220-00h12m46s.png)

## Docs / Configuration
[See the wiki](https://wiki.hyprland.org/Hypr-Ecosystem/hyprlock/)

## Building

### Deps
You need the following dependencies

- cairo
- hyprgraphics
- hyprland-protocols
- hyprlang
- hyprutils
- hyprwayland-scanner
- mesa (required is libgbm, libdrm and the opengl runtime)
- pam
- pango
- sdbus-cpp (>= 2.0.0)
- wayland-client
- wayland-protocols
- xkbcommon

Sometimes distro packages are missing required development files.
Such distros usually offer development versions of library package - commonly suffixed with `-devel` or `-dev`.

### Building

Building:
```sh
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -S . -B ./build
cmake --build ./build --config Release --target hyprlock -j`nproc 2>/dev/null || getconf _NPROCESSORS_CONF`
```

Installation:
```sh
sudo cmake --install build
```
