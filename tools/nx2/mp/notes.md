# OoT multiplayer, step 1 (2026-11-10, mp1 package, Temple of Time, young Link)
- Link's position: f32 X, Y, Z in a row (Y = height). At start (77.127, 103.938, -39.081).
- Over 100 live copies in the heap follow him (actor, collision, camera, render, history...).
  e.g. 0x214f676588, 0x2160f2b210, 0x21863a2db4, 0x22840e5048. Heap 0x2107000000-0x2507000000.
- Addresses change between boots (heap allocations); next: find the owning actor through a
  pointer chain or the code that writes it, and which copy is the source (write test).

# Step 2 (2026-11-10, mp2 + GDB stub, rsp_daemon.py on 127.0.0.1:6560)
- Writes to the f32 X/Y/Z copies snap back: they are derived. Watchpoints (rsp_daemon + watch_regs.py)
  show the chain: bone/attachment copies <- actor transform (actor+0x2f8 rotation 3x3, +0x31c X/Y/Z;
  actor was 0x214aa29508, written at pc 0x80f3b7c4 from a matrix the code builds from
  [x28+0x180] (+0x10 X/Y/Z, +0x1c.. rotation) or another path).
- SOURCE found by holding X+2 for 30 frames at each exact X/Y/Z copy (xyz_hits.py):
  0x216258f200 this boot. Layout around it (base 0x216258f1d0): 3 rows of rotation (each 4 floats),
  then X, Y, Z at +0x30, 0.1 at +0x3c, small velocity-like floats at +0x40.
  Holding X+4 there for 20 frames teleported Link through a pillar (tp_before/after.png).
- Next: a stable path to that body (pointer chain from the main module or the actor), so the
  emulator finds it every boot; then read/write it every frame for the network sync.

# Step 3 attempt (2026-11-10, mp3 with pointer_scan; not run yet)
- ASLR: the main module moves between boots (code at 0x80e04000 one boot, 0x80640000 the next).
  Module-relative: physics world vtable = main + 0x48B51B0 (2 worlds, chunk list at +0x48,
  records 0xC0 bytes, X/Y/Z f32 at record +0xF0). Statics for pointer_scan must be computed
  from the mappings each boot (qRcmd "get mappings").
- This boot the body-record hold test did not move Link (records 0x1d/0x22 near him). A record's
  f32 position is written at pc main+0x11F6008 (0x81dfa008 this boot) from f64 X/Y/Z at
  [x23 + 0x10] minus a rotated offset; holding that f64 (+3) did not move Link either.
- Open question: which state drives Link while idle vs walking (last boot's success was record
  0x23 at +0xF0, Link standing). Next: repeat the hold tests while Link walks.

# Step 3 (2026-11-10): pointer chains to Link's actor
- Plan change: the prototype only needs to READ each player's own Link (derived copies are fine)
  and DRAW the other player; it never has to write Link's position, so the "source" hunt is
  parked (record/f64 hold tests failed this boot).
- Link's actor this boot: 0x214a322f68 (vtable main + 0x4B21E18, X/Y/Z at +0x31C), found as the
  exact X/Y/Z hit preceded by a rotation matrix.
- pointer_scan (mp3, 3.3 s, 11.2M heap pointers): 160 chains from the main module's data, saved
  module-relative in chains_actor_rel.json; validate_chains.py follows them in a new boot.
- Boot 4 (main module at 0x8011a000, Castle Town Market at night): 102 of the 160 chains still
  reach Link's actor (0x2149d211c0, walks with him); 29 others land on another actor-like object
  that did not move. Survivors in chains_link_2boots.json, e.g.
  [[[main + 0x4E98438] + 0xB8] + 0x40] = Link actor, X/Y/Z at actor + 0x31C.

# Puppet and actor creation (2026-11-10)
- Puppet works (commit a35bbab636): detours at main+0x136AA0 (component matrix getter) and the
  copy sites main+0x137858 / +0x1382CC; control block in the .text padding at main+0x3C46440.
  Velocity at actor+0x388 does not drive NPC animation (their AI does).
- The engine is the Tears of the Kingdom one: Phive physics, AINB nodes (Execute*/Query*),
  ActionSeq animation; Link's actor is "Player_Child" (actor+0x260 -> name).
- Actor creation: the AINB node "ExecuteGenericRequestCreateActor" (getName at main+0x26E67E0,
  vtable in .data.rel.ro) executes at main+0x26E62B0 and calls
    main+0x372A70(ActorMgr* [[main+0x4C7F3E0]], &name, &param, handle = node+0x20, 1, 0, 0, 0,
                  stack: u8 0, u64 0, u64 0)
  param (0xA0 bytes): +0x00 position X/Y/Z, +0x0C rotation 3x3, +0x30 12 bytes from
  [main+0x4C7E0B8] (scale?), +0x40 u64 from the node, +0x98 u8 3; the rest zero.
- Next: a cave that calls it once on a request from the control block, hooked into a function
  the game runs every frame on a game thread; first actor to try: Dark Link (name from romfs).
