// Copyright 2023-2024 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

// std
#include <array>
#include <vector>

namespace dlaf {

struct DLAFScene
{
  std::vector<float> points; // (x, y, z) * numParticles
  std::vector<float> distances;
  float maxDistance{0.f};
  float radius{1.5f};
  std::array<float, 6> bounds = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
};

struct DLAFParams
{
  int numParticles{10000};
  // The distance between particles that are joined together
  float particleSpacing{1.f};
  // How close together particles must be in order to join together
  float attractionDistance{3.f};
  // The minimum distance that a particle will move during its random walk
  float minMoveDistance{1.f};
  // How many interactions must occur before a particle will allow another
  // particle to join to it.
  int stubbornness{0};
  // The probability that a particle will allow another particle to join to it.
  float stickiness{1.f};
  // The radius of the bounding sphere that bounds all of the particles
  float boundingRadius{0.f};
};

DLAFScene compute(
    const DLAFParams &p, float *amount_done = nullptr, bool *cancel = nullptr);

void exportDLAFFile(const char *filename, const DLAFScene &s);
void importDLAFFile(const char *filename, DLAFScene &s);

} // namespace dlaf
