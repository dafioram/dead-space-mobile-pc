# Credits in the RIG

The Tab/controller RIG screen gains a read-only credits tile immediately left of
the power-node counter. It uses the existing RIG node-panel background, font,
colour, baseline and fade/visibility, with the game's existing credits item icon.
The original node counter and weapon/menu controls are unchanged. Survival's
separate credits display is not duplicated.

The displayed decimal balance comes from the same inventory wallet used by
`Inventory::useCredits`, not an estimated count of pickup items. It is refreshed
while the RIG/pause menu is open and supports the whole unsigned 32-bit balance.
There is no button, inventory item, sell action, currency grant or persistence
change. Original assets are not rewritten: the layout is augmented in memory.

## Boundaries

- `rig_credits_layout.cpp` copies three existing node-layout entities after
  validating the schema/templates. It keeps the originals and their vertex
  references unchanged. Repeated patching does not append more counters.
- `rig_credits.cpp` reads the wallet during the native `Hud::onUpdate` callback
  and updates a named native mutable-text entity. The guest-layout boundary is
  guarded by symbol/instruction checks; unsupported libraries disable the feature.
- The live layout is fetched from that Hud on each update, rather than retaining
  widget/entity pointers across level loads. One persistent native string and
  UTF-16 buffer avoid per-frame/menu-visit leaks. No inventory fields are written.
- No new fonts/images or game data are included in the port or its packages.

## Validation

Synthetic `rig_credits` tests cover schema/template rejection without mutation,
preservation of the original entities, matching font/colour/anchors, the separate
currency icon, noninteractive entity types, left-of-nodes placement, idempotency
and SBIN round-tripping. An offline private-copy check also applied the patch to
the supplied layouts without launching the game or changing the original asset.

Visual and live-wallet confirmation remain user-led. With `--no-save`, check:

1. Open Tab: the credits tile and nodes are readable, aligned and do not overlap.
2. Close/reopen the RIG and enter/exit pause/options; no stale tile remains in
   gameplay and normal navigation still works with mouse/keyboard/controller.
3. Compare the number to the store balance; after collecting/spending credits,
   reopen the RIG and confirm the value updates.
4. Check Native/Legacy and your usual window sizes. No economy/save change is
   implied by the automated tests or the offline layout check.
