# Desktop Runtime

Desktop combines public ZUI host/router/screen/draw APIs with product UI
lifecycle, input/render handoff, navigation, registries, text, and notifications.
Apps, widgets, and Desktop-local services own their corresponding behavior;
reusable UI components belong in public ZUI.

- Keep Desktop as runtime glue. Put app behavior in its owning app, dashboard
  behavior in its widget, and reusable components in public ZUI.
- Use the current ZUI host, router, screen, draw, and component APIs. Do not
  reintroduce removed scene/view-dispatcher abstractions.
- Draw callbacks render prepared state only. They do not query services,
  allocate, block, mutate lifecycle state, or emit repeated logs.
- Keep input and listener callbacks bounded. Defer service calls and heavier
  work, then request redraw after prepared state changes.
- Prefer fixed-size state on render and input paths. Cancel work and prevent
  callbacks from reaching freed state before app or widget teardown.
- Keep user-visible text in the central text contract and maintain every
  selectable language pack with matching symbols and format arguments.

Use local test metadata for focused integration checks and a current consuming
product build for composition. Pixel, input, display, and timing behavior remain
unverified until observed on the applicable hardware.
