# Desktop Runtime

Desktop combines public ZUI host/router/screen/draw APIs with product UI
lifecycle, input/render handoff, navigation, registries, text, and notifications.
Apps, widgets, and Desktop-local services own their corresponding behavior;
reusable UI components belong in public ZUI.

The consuming Spec Kit project's SDK standards define Desktop lifecycle,
rendering, registration, text, and compatibility contracts. Use live metadata
for integration tests and the current consuming product for composition checks.
