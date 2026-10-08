# EmberBSD Runtime

Application runtime and shared device operations for
[EmberBSD](https://github.com/oxtech-ember/EmberBSD) — Unix for intelligent devices.

## Purpose

Runtime is the planned device-side application environment above EmberBSD.
It will implement the application contracts defined by SDK: execution,
installation, lifecycle, permissions and shared operations on device resources.
Optional application profiles should let a small device use only what it needs.

Runtime owns how an application runs on the device. SDK owns the developer-facing
interfaces and compatibility checks. The operating system owns drivers and
hardware support; Ports owns adaptations to external dependencies.

## Current state

**Design stage.** This repository currently documents its purpose and boundaries.
It does not provide an executable runtime, installer, stable Device API or
validated Wasm environment. There is no Runtime installation command yet.
Use the existing OS, Ports and Examples instructions for current workflows.

The next implementation needs a defined SDK contract and a reproducible example
that exercises the same interface on its declared targets. Until then, repository
names and architectural plans must not be treated as available APIs.

## Related EmberBSD projects

[EmberBSD](https://github.com/oxtech-ember/EmberBSD#emberbsd-ecosystem) is the
central project and the entry point for the ecosystem.

- [EmberBSD](https://github.com/oxtech-ember/EmberBSD) — OS, drivers, boards and system builds.
- [EmberBSD-SDK](https://github.com/oxtech-ember/EmberBSD-SDK) — application interfaces, package contracts and development tools; design stage.
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
