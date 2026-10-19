# Chain Reaction user guide

Chain Reaction is Interi's visual scripting: gameplay as **data**. A chain is a graph of Events joined by links; the
engine runs it deterministically (same result at 1, 4 or 32 threads, and on Linux and Windows), so designers can build
rules without code and programmers extend the palette from modules.

## Anatomy

A `.chain.json` file holds **nodes** (an Event each) and **links** from `<node>.<output>` to another node.

```json
{
  "version": 1,
  "nodes": [
    { "id": "gold",  "event": "trigger.Overlap", "params": { "key": { "const": "gold" } } },
    { "id": "score", "event": "world.ModifyValue", "params": {
        "id": { "const": "com.interi.starter:score" },
        "delta": { "expr": "com.interi.starter:CoinValue(\"gold\")" } } },
    { "id": "pickup", "event": "world.Destroy" }
  ],
  "links": [
    { "from": "gold",  "to": "score" },
    { "from": "score", "to": "pickup" }
  ]
}
```

- **Triggers** start a chain: `Start`, `Tick`, `InputAction`, `Timer`, `ValueChanged`, `Spawned`, `Destroyed`,
  `trigger.Overlap` / `trigger.OverlapEnd`. A trigger's `key` input matches what was raised (an action name, a volume tag).
- **Actions** do something: `world.SetValue`, `ModifyValue`, `Destroy`, `Move`, `Rotate`, `Show`, `Hide`, `PlaySound`,
  `SpawnPrefab`, and flow control `Delay`, `Timer`, `Branch`, `Sequence`, `Random`. Modules add more (their ids are
  `<module id>:<name>`); animation (`anim.*`), navigation (`nav.*`) and `ai` events come from the engine.
- **Links** may carry a bool `condition`: the "if" on the link. A link runs when its condition holds.

An input binds one of `{"const": v}`, `{"payload": "field"}` (an upstream output's data), `{"value": "id"}` (a Value) or
`{"expr": "..."}`. An unbound input takes its schema default.

## Expressions

Typed once at compile time, so a mistake is an error with a span, not a run-time surprise. Operators `+ - * / %`,
comparisons, `&& || !`, members `.x .y .z` on `vec3`; built-ins `min max abs clamp sqrt floor int float vec3 length dot`;
`value("id")`, `payload.field`, and module functions (`com.interi.starter:CoinValue("gold")`).

- int and u32 wrap; mixing an int or u32 with a float promotes to float; `x / 0` and `x % 0` are 0; `int()` saturates and
  turns NaN into 0.
- There is **no trigonometry**: chain results must be bit-identical across standard libraries. Do angle math in a module
  function if you need it, and accept that it stays out of the determinism guarantee.
- Nesting is limited (64 levels) and so is the size of one expression (2048 nodes).

## Values

Values are the game's typed state: declared in `module.json` (`values`), overridden by the project's data file, changed at
run time by chains (`SetValue`, `ModifyValue`) and read by chains, the HUD and saves. A write that fails the schema
(`min`/`max`, type, enum option) is dropped and traced. Run-time writes win over project data, which wins over module defaults.

## Waves and time

The game calls the executor once per fixed step. **Wave 0** is what was raised plus the due timers; each wave's link
activations are evaluated in parallel against the *pre-step* state and merged in order, then effects apply serially at
the end of the step (a `ModifyValue` delta from two hits in one step lands both times; `SetValue` is last writer wins).
Triggers raised by effects fire on the next step. A depth limit stops runaway loops: overruns are logged and dropped.
`Random` is derived from the seed, the step and the link, so replays match.

## Functions

Repeated logic becomes a **Chain Function** (`.chainfn.json`): a body with typed inputs and outputs, called as a node
`<provider>:<Name>`, with an `.Entry` node (inputs are `payload.<input>`) and `.Return.<output>` exits. Calls are inlined
before compiling, so they cost nothing at run time and don't change the determinism hash.

## In the editor

Open a chain from the asset browser (or `level_editor --chain <file>`). Pan with the middle mouse button, zoom with the
wheel, drag Events from the **palette** (it shows each event's module and "provided by A, overrides B"), link by dragging
between pins, and click a link to give it a condition. The inspector shows type-filtered pickers for bindings. Edits are
undoable and **checked**: an edit that would introduce a compile error is refused, with the compiler's message. The
Problems tab lists diagnostics by node, link and input.

**Debugging**: Play in the editor (F5), watch nodes and links light up with heat, add breakpoints (the step finishes, then
the session pauses), read the event timeline and add watches on Values. `level_editor --chain <file> --chain-breakpoint
<node> --play 1` pauses at the step that hits it.

## Extending from a module

```cpp
interi::chain::EventDefinition complete;
complete.outputs = {{.name = "Out", .payload = {}}};
complete.implementation = std::make_shared<const interi::chain::EventImpl>(
    [](interi::chain::EventCall& call)
    {
        call.Defer([](interi::chain::ApplyContext&) { /* change shared state here, serially */ });
        call.Fire(0);
    });
registrar.Chains().Events().Provide(scope.Provider(), scope.Local("LevelComplete"), std::move(complete));
```

A native implementation runs during a wave, possibly on a worker, against the pre-step state. It may read its inputs, the
World and Values, and record outputs (`Fire`, `FireAfter(ns)`) and effects (`Defer`). It must not write shared state,
throw, or depend on anything but its inputs and `Seed()`. Violating that breaks determinism, which the cross-thread
tests in `tests/chain` exist to catch: add your event to a scenario and compare 1/4/32 workers.

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| "unknown event" | The module providing it isn't enabled, or the id is misspelt (Module Manager > Conflicts shows providers) |
| A node never runs | Nothing links to it from a trigger; unreachable nodes are dropped with a warning |
| An expression won't compile | The error span points at the token; check int/float mixing and `vec3` members |
| A Value doesn't change | The write failed validation (see the debugger trace) or another module overrides the Value |
