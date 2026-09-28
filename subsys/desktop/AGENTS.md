# Desktop Runtime

Desktop combines public ZUI host/router/screen/draw APIs with product UI
lifecycle, input/render handoff, navigation, registries, text, and notifications.
Apps, widgets, and Desktop-local services own their corresponding behavior;
reusable UI components belong in public ZUI.

- Keep Desktop as runtime glue. Put app behavior in its owning app, dashboard
  behavior in its widget, and reusable components in public ZUI.
- Use ZUI's public host, router, screen, draw, and component APIs.
- Draw callbacks render prepared state only. They do not query services,
  allocate, block, mutate lifecycle state, or emit repeated logs.
- Keep input and listener callbacks bounded. Defer service calls and heavier
  work, then request redraw after prepared state changes.
- Prefer fixed-size state on render and input paths. During app, widget, or
  service teardown, stop observations and new callbacks, wait for in-flight
  access when needed, and cancel work before freeing owned state.
- Keep user-visible text in the central English text contract and preserve
  matching symbols and format arguments at its call sites.
- Preserve accepted navigation, interaction, and visual behavior unless the task
  explicitly changes it.

Select focused checks from local test metadata. Add a consuming product build
when the change affects product composition. Pixel, input, display, and timing
claims require observation on the applicable hardware; perform those checks
when required by the task's acceptance criteria and already authorized.
For framebuffer capture or synthetic navigation over MCUmgr, use
[the CLI guide](../../scripts/meshbus/README.md#display-capture). The capture
endpoint belongs to Display, independently of Desktop or Shell availability.
Release injected keys and observe the resulting UI state before claiming
navigation succeeded.
