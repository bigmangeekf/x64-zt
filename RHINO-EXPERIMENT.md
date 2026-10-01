# IW7 Rhino-only pack experiment

This fork adds narrowly scoped animation-class and behavior-tree serializers for the local retail Rhino (`alien_rhino_animclass` / `alien_rhino`). It also exports embedded shader bytecode before linking dependencies, loads available donor patch/techset fastfiles offline, exports only the Rhino definition row and writes the expanded pack asset inventory. Rhino pack builds reject map/world/navigation and unsupported assets.

Use `-rhino-extract -buildzone paris_rhino` with a locally prepared `zone_source/paris_rhino.csv`. Asset files and compiled scripts must be supplied from an owned local installation. The shared donor animation tree must first be pruned to the 123 animations referenced by the Rhino class. No proprietary assets or script dumps are included in this repository.

Validation: Windows VS2022 Release build succeeded locally. A roughly 4.7 MB pack has linked with one model, 123 animations, one animation class, one behavior tree, 17 dependency scripts, 11 images and required rendering/physics dependencies. Runtime loading and gameplay remain experimental and are not accepted.

The command is intentionally specialized. Upstream contributions should separate generic serializers/shader fixes from this experiment after runtime validation.

Runtime validation found that linking a live donor physics asset copied Havok data after its in-memory transformation. The extraction hook now captures `ragdoll_alien_queen` before registration; the resulting pack passed physics loading. Further validation identified missing functions in Spaceland shared GSC namespaces. These are being isolated in a Rhino support namespace by the local script preparation tools, preserving destination scripts. No spawn or gameplay acceptance yet.
