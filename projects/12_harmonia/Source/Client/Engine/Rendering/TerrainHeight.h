#pragma once

namespace Harmonia {

// The terrain's actual shape - a pure, deterministic function of (x, z),
// with no rendering or GL dependency, so both the chunked renderer AND
// PhysicsWorld's collision heightfield sample the exact same surface.
// Extracted from the old single-mesh GroundPlane during the chunking
// rewrite; the height formula itself is unchanged.
namespace TerrainHeight {
float heightAt(float worldX, float worldZ);
}

}
