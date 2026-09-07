# Product Application

This directory owns the thin application entry point, device service composition,
board profiles, and sysbuild policy. CMake/Kconfig and current board metadata
define supported combinations. The west manifest lives at repository root;
shared host tools and west extensions live under root `scripts/`.
Reusable behavior belongs in the module's services, public APIs, and drivers.

Follow the root's task-based documentation routing. When shared contracts are
affected, also read applicable local rules along those paths. For tracked work,
record affected targets and actual evidence in the current local ticket.
Device profiles and overlays live in `boards/`.
