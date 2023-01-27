// Copyright 2023 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "dlaf/DLAF.h"
// std
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
// linalg
#include "linalg.h"
// boost
#include <boost/geometry/geometry.hpp>
#include <boost/iterator/function_output_iterator.hpp>

namespace dlaf {

using namespace linalg;
using namespace linalg::aliases;

// number of dimensions (must be 2 or 3)
static constexpr int D = 3;

// boost is used for its spatial index
using BoostPoint =
    boost::geometry::model::point<float, D, boost::geometry::cs::cartesian>;

using IndexValue = std::pair<BoostPoint, int>;

using Index = boost::geometry::index::rtree<IndexValue,
    boost::geometry::index::linear<4>>;

static inline BoostPoint ToBoost(const float3 &v)
{
  return BoostPoint(v.x, v.y, v.z);
}

// Random returns a uniformly distributed random number between lo and hi
static inline float Random(const float lo = 0, const float hi = 1)
{
  static thread_local std::mt19937 gen(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  std::uniform_real_distribution<float> dist(lo, hi);
  return dist(gen);
}

// RandomInUnitSphere returns a random, uniformly distributed point inside the
// unit sphere (radius = 1)
static inline float3 RandomInUnitSphere()
{
  while (true) {
    const float3 p(Random(-1, 1), Random(-1, 1), D == 2 ? 0 : Random(-1, 1));
    if (length2(p) < 1) {
      return p;
    }
  }
}

// Model holds all of the particles and defines their behavior.
class Model
{
 public:
  Model(DLAFParams p) : m_params(p)
  {
    m_scene.radius = p.attractionDistance / 2.f;
  }

  void Reserve(size_t numParticles)
  {
    m_scene.points.reserve(numParticles * 3);
    m_scene.distances.reserve(numParticles);
    m_JoinAttempts.reserve(numParticles);
  }

  size_t Size()
  {
    return m_scene.points.size();
  }

  float3 GetPoint(int i) const
  {
    return float3(m_scene.points[3 * i + 0],
        m_scene.points[3 * i + 1],
        m_scene.points[3 * i + 2]);
  }

  // Add adds a new particle with the specified parent particle
  void Add(const float3 &p, const int parent = -1)
  {
    const int id = m_scene.points.size() / 3;
    m_Index.insert(std::make_pair(ToBoost(p), id));
    m_scene.points.push_back(p.x);
    m_scene.points.push_back(p.y);
    m_scene.points.push_back(p.z);
    const auto dist = length(p);
    m_scene.distances.push_back(dist);
    m_scene.maxDistance = std::max(m_scene.maxDistance, dist);
    m_scene.bounds[0] = std::min(p.x, m_scene.bounds[0]);
    m_scene.bounds[1] = std::min(p.y, m_scene.bounds[1]);
    m_scene.bounds[2] = std::min(p.z, m_scene.bounds[2]);
    m_scene.bounds[3] = std::max(p.x, m_scene.bounds[3]);
    m_scene.bounds[4] = std::max(p.y, m_scene.bounds[4]);
    m_scene.bounds[5] = std::max(p.z, m_scene.bounds[5]);
    m_JoinAttempts.push_back(0);
    m_params.boundingRadius = std::max(
        m_params.boundingRadius, length(p) + m_params.attractionDistance);
  }

  // Nearest returns the index of the particle nearest the specified point
  int Nearest(const float3 &point) const
  {
    int result = -1;
    m_Index.query(boost::geometry::index::nearest(ToBoost(point), 1),
        boost::make_function_output_iterator(
            [&result](const auto &value) { result = value.second; }));
    return result;
  }

  // RandomStartingPosition returns a random point to start a new particle
  float3 RandomStartingPosition() const
  {
    const float d = m_params.boundingRadius;
    return normalize(RandomInUnitSphere()) * d;
  }

  // ShouldReset returns true if the particle has gone too far away and
  // should be reset to a new random starting position
  bool ShouldReset(const float3 &p) const
  {
    return length(p) > m_params.boundingRadius * 2;
  }

  // ShouldJoin returns true if the point should attach to the specified
  // parent particle. This is only called when the point is already within
  // the required attraction distance.
  bool ShouldJoin(const float3 &p, const int parent)
  {
    m_JoinAttempts[parent]++;
    if (m_JoinAttempts[parent] < m_params.stubbornness) {
      return false;
    }
    return Random() <= m_params.stickiness;
  }

  // PlaceParticle computes the final placement of the particle.
  float3 PlaceParticle(const float3 &p, const int parent) const
  {
    return lerp(GetPoint(parent), p, m_params.particleSpacing);
  }

  // Motionvec3 returns a vector specifying the direction that the
  // particle should move for one iteration. The distance that it will move
  // is determined by the algorithm.
  float3 Motionvec3(const float3 &p) const
  {
    return RandomInUnitSphere();
  }

  // AddParticle diffuses one new particle and adds it to the model
  void AddParticle()
  {
    // compute particle starting location
    auto p = RandomStartingPosition();

    // do the random walk
    while (true) {
      // get distance to nearest other particle
      const int parent = Nearest(p);
      const float d = length(p - GetPoint(parent));

      // check if close enough to join
      if (d < m_params.attractionDistance) {
        if (!ShouldJoin(p, parent)) {
          // push particle away a bit
          p = lerp(GetPoint(parent),
              p,
              m_params.attractionDistance + m_params.minMoveDistance);
          continue;
        }

        // adjust particle position in relation to its parent
        p = PlaceParticle(p, parent);

        // add the point
        Add(p, parent);
        return;
      }

      // move randomly
      const float m =
          std::max(m_params.minMoveDistance, d - m_params.attractionDistance);
      p += normalize(Motionvec3(p)) * m;

      // check if particle is too far away, reset if so
      if (ShouldReset(p)) {
        p = RandomStartingPosition();
      }
    }
  }

  DLAFScene consumeFinalScene()
  {
    return std::move(m_scene);
  }

 private:
  DLAFParams m_params;

  // final particle positions
  DLAFScene m_scene;

  // m_JoinAttempts tracks how many times other particles have attempted to
  // join with each finalized particle
  std::vector<int> m_JoinAttempts;

  // m_Index is the spatial index used to accelerate nearest neighbor queries
  Index m_Index;
};

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

DLAFScene compute(const DLAFParams &p, float *amount_done, bool *cancel)
{
  DLAFScene retval;

  // create the model
  Model model(p);
  model.Reserve(p.numParticles);

  // add seed point(s)
  model.Add(float3(0, 0, 0));

  // run diffusion-limited aggregation
  for (int i = 1; i < p.numParticles; i++) {
    if (cancel && *cancel)
      break;
    model.AddParticle();
    if (amount_done)
      *amount_done = i / static_cast<float>(p.numParticles);
  }

  return model.consumeFinalScene();
}

void exportDLAFFile(const char *filename, const DLAFScene &s)
{
  const uint64_t numParticles = s.distances.size();

  auto *fp = std::fopen(filename, "wb");

  std::fwrite(&numParticles, sizeof(numParticles), 1, fp);
  std::fwrite(&s.radius, sizeof(s.radius), 1, fp);
  std::fwrite(&s.maxDistance, sizeof(s.maxDistance), 1, fp);
  std::fwrite(s.bounds.data(), sizeof(s.bounds[0]), s.bounds.size(), fp);
  std::fwrite(s.points.data(), sizeof(s.points[0]), numParticles * 3, fp);
  std::fwrite(s.distances.data(), sizeof(s.distances[0]), numParticles, fp);

  std::fclose(fp);
}

void importDLAFFile(const char *filename, DLAFScene &s)
{
  uint64_t numParticles = 0;

  auto *fp = std::fopen(filename, "rb");

  auto r = std::fread(&numParticles, sizeof(numParticles), 1, fp);
  r = std::fread(&s.radius, sizeof(s.radius), 1, fp);
  r = std::fread(&s.maxDistance, sizeof(s.maxDistance), 1, fp);
  r = std::fread(s.bounds.data(), sizeof(s.bounds[0]), s.bounds.size(), fp);

  s.points.resize(numParticles);
  r = std::fread(s.points.data(), sizeof(s.points[0]), numParticles * 3, fp);

  s.distances.resize(numParticles);
  r = std::fread(s.distances.data(), sizeof(s.distances[0]), numParticles, fp);

  std::fclose(fp);
}

} // namespace dlaf
