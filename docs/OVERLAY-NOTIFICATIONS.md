# Overlay notifications

Added23 October2026 at user request.

- Freecam reports its live state through the existing camera bridge. While
  active, the overlay displays a persistent top-left `Photo Mode` indicator.
  This label does not imply freeze time or other planned photographic features.
- Successful model changes produce a temporary model-name notification.
- Overlay settings are compared on opening/closing. Each final changed setting
  produces its own notification; slider intermediates and changes reverted
  before closing produce none. Restart-dependent settings retain that wording.
  Reset buttons are covered by the same comparison. Tab selection is excluded.
- Camera changes are reported through engine state publication. Requests alone
  do not produce a success notification.
- Up to five temporary messages stack at the top left, each shown for five
  seconds. Further queued messages receive their full time when displayed.
  Menu, loading and native dialogs pause delivery and timers. The pending queue
  is bounded to128 entries. Top-left FPS readouts shift below this stack.
- Notifications render even when the FPS overlay is disabled.

Implementation: kzsf_overlay.cpp, kzsf_notifications.h and
kzsf_overlay_settings_snapshot.h in gpu/shim. The independent notification
mutex allows engine-thread publications without taking the ImGui mutex.
tests/test_overlay_notifications.cpp verifies queue ordering, stacking,
expiry, hidden-state pausing and final settings comparisons. Build/tests pass;
in-game appearance remains to be checked.
