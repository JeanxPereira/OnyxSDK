The PNG files in this directory are the render oracle, frozen since v1.
Each scene's PNG is rendered by the v2 renderer and compared against it within a tolerance profile.
The JSON files describe the v2 draw list for each scene.
Re-bless the JSON files with `OnyxOracle bless`; never re-bless the PNG files.
Tolerances live in `../tolerances.toml`, one profile per device type.
Scene definitions are built in code by `Onyx/TestKit/SceneFixtures.h`.
`OnyxOracle bless --golden Tests/Golden/corpus` rewrites the JSON reports from the current renderer; it never writes a PNG.
