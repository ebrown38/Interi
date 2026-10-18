## With a project pin but no module declaring physicsEngines, is the pinned engine still selected?
- **Task:** Milestone 17, Physics Engine selection
- **Context:** `SelectPhysicsEngine` in engine/sdk/src/ModuleGraph.cpp; plan 17.3 only says "NoDeclaringModules_NoEngine" without a pin.
- **Assumption made:** A pin selects its engine even when nobody declares engines (the project asked for it); it sits at the bottom of the priority list.

## Where does CanAddModule insert the candidate in the project's module order?
- **Task:** Milestone 17, 17.4 Installing and enabling
- **Context:** `CanAddModule` in engine/sdk/include/interi/sdk/ModuleGraph.h; the plan doesn't say top or bottom, and priority affects engine candidate order.
- **Assumption made:** An `insertAt` parameter, defaulting to the top (highest priority) like a freshly enabled module.

## Which base version does an extender's chain use when several are installed?
- **Task:** Milestone 17, 17.3 selection
- **Context:** Extender declares `extends: {id, version-range}`; several versions of the base may be installed.
- **Assumption made:** Try base versions highest to lowest and take the first chain that satisfies every declaring module (and pin range).

## What are the concrete service.Physics* contract ids?
- **Task:** Milestone 17, 17.5/17.7 contracts and scaffolding
- **Context:** The plan names only service.PhysicsQueries (M19.3.1 declares it). A scaffolded PhysicsEngine needs something to provide.
- **Assumption made:** The prefix `service.Physics` is reserved (`sdk::kPhysicsContractPrefix`). `sdk::PhysicsContracts()` (just `service.PhysicsQueries`, a placeholder hook signature) is declared by every ModuleHost so a scaffolded engine has something to provide; M19 defines the real contracts.

## May a PhysicsEngine module have its own `dependencies`?
- **Task:** Milestone 17, 17.3 selection
- **Context:** Dependencies are resolved before the engine is selected, so an engine's dependencies would be skipped.
- **Assumption made:** Rejected at parse time for M17 (a PhysicsEngine declares no dependencies); revisit with M19.
