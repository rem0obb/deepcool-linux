# Deepcool Linux

This GTK application is meant to replicate the functionality of the original `DeepCool Digital`
Windows program on Linux. It detects supported DeepCool USB/HID devices, shows live CPU/GPU
monitoring data, and sends the display data stream directly from the graphical interface.

If you have a device that has not been added or tested yet, please read the notes below the
supported devices.
If you think you can collaborate, please write an issue so we can get in touch.

> [!WARNING]
> This software is not official. Use it at your own risk and at the risk of your account.

For installation, you can download it here 

## Install

Build and install into your user profile:

```sh
make
./deepcool-digital-linux --install
```

This installs:

- `~/.local/bin/deepcool-digital-linux`
- `~/.local/share/applications/unnoficial.deepcool.digital.linux.desktop`
- `~/.local/share/icons/hicolor/256x256/apps/deepcool-digital-linux.png`

After installing, launch **DeepCool Digital Linux** from your application menu. The app asks for
administrator permission when needed because DeepCool HID devices normally require root access.

## Dependencies

Runtime libraries needed by the binary:

- GTK 4
- GLib / GIO
- hidapi with hidraw backend
- Cairo, Pango, HarfBuzz, GDK Pixbuf, Graphene
- `pkexec` / polkit, used to request administrator permission
- systemd, used by the optional background service
- Desktop StatusNotifier/AppIndicator support if you want the background tray icon

Build tools needed from source:

- C compiler, `make`, `pkg-config`
- GTK 4 development package
- hidapi development package
- GLib resource compiler, usually `glib-compile-resources`

Example package names:

```sh
# Debian/Ubuntu
sudo apt install build-essential pkg-config libgtk-4-dev libhidapi-dev libglib2.0-dev-bin policykit-1

# Fedora
sudo dnf install gcc make pkgconf-pkg-config gtk4-devel hidapi-devel glib2-devel polkit

# Arch
sudo pacman -S base-devel pkgconf gtk4 hidapi glib2 polkit
```

# Deepcool 

Initial interface how execute application 

![Deepcool](screenshoots/deepcool-interface.png)

You can leave it running in the background whenever your PC starts up, so it will always display the settings you added on the screen.
![Deepcool](screenshoots/deepcool-configs.png)
