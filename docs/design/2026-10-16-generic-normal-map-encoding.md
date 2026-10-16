# Generic normal-map decoding

## Intent

Onyx must render normal maps according to metadata supplied by a consumer. A
renderer cannot infer the source convention after a loader has expanded every
texture to RGBA8, and assuming that every normal stores only X/Y destroys an
authored Z channel.

The SDK therefore owns a small, game-independent normal-map contract. A game
module translates its asset format into that contract; the renderer only
decodes the declared representation.

## Contract

`MaterialDesc` gains a `NormalMapDesc` with:

- `encoding`: `XYReconstructZ` or `XYZ`;
- `decodeScale` and `decodeBias`: affine conversion from sampled UNORM to the
  signed tangent-normal domain;
- `strength`: scales tangent X/Y before normalization;
- `flipY`: preserves or flips the decoded green/Y component.

Defaults reproduce the existing Onyx path exactly: reconstruct Z from RG,
`sample * 2 - 1`, strength 1, and no Y flip. Consumers that do nothing retain
their current image.

The Vulkan material UBO carries the four scalar decode controls in one `vec4`.
One new flag selects authored XYZ. No asset names, hashes, compression enums or
game-specific constants enter Onyx.

## Consumer mapping

GoWToolkit keeps source texture storage while decoding, but does not infer a
normal convention from compression alone. Unknown shader permutations retain
`XYReconstructZ`. A permutation whose DXIL is known to sample authored RGB may
select `XYZ` when its storage carries a third channel, and supply the observed
endpoint-compensated scale `2.00787401199` and bias `-1.00787401199`.

Unknown storage and unknown shader behavior keep the Onyx default. This is
deliberately conservative.

## Scope

This increment fixes normal decoding only. It does not claim full Ragnarök
lighting parity: packed G-buffer properties, YCoCg-like base color, environment
lighting and the deferred resolve remain separate increments.

## Verification

- Pure unit tests pin default/backward-compatible packing and explicit XYZ
  packing into the GPU contract.
- Shader compilation proves both GLSL branches remain valid SPIR-V.
- GoWToolkit unit tests pin AGC storage mapping and material propagation.
- Both projects build against the same local Onyx revision before release.
