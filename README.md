# EmberBSD Runtime

Application runtime and shared device operations for
[EmberBSD](https://github.com/oxtech-ember/EmberBSD) — Unix for intelligent devices.

Read the [EmberBSD Developer Guide](https://github.com/oxtech-ember/EmberBSD-Docs/blob/main/modules/runtime.md)
for the proposed Runtime and its boundaries with the implemented OS and SDK.

## Purpose

Runtime is the planned device-side application environment above EmberBSD.
It will implement the application contracts defined by SDK: execution,
installation, lifecycle, permissions and shared operations on device resources.
Optional application profiles should let a small device use only what it needs.

Runtime owns how an application runs on the device. SDK owns the developer-facing
interfaces and compatibility checks. The operating system owns drivers and
hardware support; Ports owns adaptations to external dependencies.

## Current state

**Design stage.** Most of this repository still documents its purpose and
boundaries. It does not provide an installer, stable general Device API or
validated Wasm environment. There is no Runtime installation command yet.

The new `qml/` directory is an opt-in, experimental C renderer for the flat
Rectangle/Text scene ABI produced by the EmberBSD SDK QML compiler. It uses a
caller-owned RGB565 scanline, supports mutable integer bindings and a NetBSD
wsdisplay dumb-framebuffer backend, and does not depend on Qt or LVGL. This
small renderer is a concrete implementation under development; it does not
make the broader Runtime APIs stable or imply general QML compatibility.
See [`qml/README.md`](qml/README.md) for its ABI, supported scene subset,
build instructions and checks.

## Related EmberBSD projects

[EmberBSD](https://github.com/oxtech-ember/EmberBSD#emberbsd-ecosystem) is the
central project and the entry point for the ecosystem.

- [EmberBSD](https://github.com/oxtech-ember/EmberBSD) — OS, drivers, boards and system builds.
- [EmberBSD-SDK](https://github.com/oxtech-ember/EmberBSD-SDK) — Prebuild SDK workspace and sysroot tools; application and package contracts remain planned.
- [EmberBSD-Ports](https://github.com/oxtech-ember/EmberBSD-Ports) — third-party recipes, patches and native dependencies.
- [EmberBSD-Examples](https://github.com/oxtech-ember/EmberBSD-Examples) — standalone applications and reproducible demonstrations.
- [Ember-Agent-Skills](https://github.com/oxtech-ember/Ember-Agent-Skills) — portable developer skills for AI coding assistants and tested contributions.

## Connect developer skills

[Ember Agent Skills](https://github.com/oxtech-ember/Ember-Agent-Skills) provides
portable Agent Skills packaged with Agent Plugins. Load the package or the
complete skill directory using your development environment's supported
mechanism. Follow the [installation and validation guide](https://github.com/oxtech-ember/Ember-Agent-Skills#use-in-your-development-environment)
for the shared format and the separately tested Codex adapter.

Ask the `emberbsd-repository-guide` skill to identify the available interfaces
and checks for your task.
The package supplies assistant instructions; it does not install a device runtime
or create missing SDK interfaces.
