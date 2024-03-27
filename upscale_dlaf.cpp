// Copyright 2023-2024 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

// DLAF
#include "dlaf/DLAF.h"
// std
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
// linalg
#include "linalg.h"

using namespace linalg;
using namespace linalg::aliases;

std::string g_filename_in;
std::string g_filename_out;

#define FACTOR 2

static void printUsage()
{
  printf("usage: upscale_dlaf [filename_in.dlaf] [filename_out.dlaf]\n");
  std::exit(0);
}

static void parseCommandLine(int argc, char *argv[])
{
  if (argc != 3)
    printUsage();

  g_filename_in = argv[1];
  g_filename_out = argv[2];
}

static dlaf::DLAFScene upscaleScene(const dlaf::DLAFScene &scene_in)
{
  const size_t size_in = scene_in.distances.size();
  const size_t size_out = size_in * FACTOR;

  const float radius_in = scene_in.radius;
  const float displacement = radius_in / float(FACTOR);
  const float radius_out = radius_in;//displacement * 1.75f;

  dlaf::DLAFScene scene_out;
  scene_out.points.resize(size_out * 3);
  scene_out.distances.resize(size_out);

  std::mt19937 rng;
  rng.seed(0);
  std::uniform_real_distribution<float> dist(0.f, displacement);

  auto randomDir = [&]() { return float3(dist(rng), dist(rng), dist(rng)); };

  for (size_t i = 0; i < size_in; i++) {
    const float3 &p_in = *(const float3 *)&scene_in.points[3 * i];
    const float3 dir = normalize(p_in);

    float3 *p_out = (float3 *)&scene_out.points[3 * (FACTOR * i + 0)];
    float *d_out = &scene_out.distances[FACTOR * i + 0];
    *p_out = p_in + displacement * randomDir();
    *d_out = length(*p_out);

    p_out = (float3 *)&scene_out.points[3 * (FACTOR * i + 1)];
    d_out = &scene_out.distances[FACTOR * i + 1];
    *p_out = p_in - displacement * randomDir();
    *d_out = length(*p_out);
  }

  scene_out.maxDistance = scene_in.maxDistance;
  scene_out.radius = radius_out;
  std::memcpy(&scene_out.bounds, &scene_in.bounds, sizeof(scene_out.bounds));

  return scene_out;
}

int main(int argc, char *argv[])
{
  parseCommandLine(argc, argv);

  printf("=== importing from '%s' and exporting to '%s' ===\n",
      g_filename_in.c_str(),
      g_filename_out.c_str());
  fflush(stdout);

  dlaf::DLAFScene scene_in;
  dlaf::importDLAFFile(g_filename_in.c_str(), scene_in);

  printf("imported %zu points from '%s'\n",
      scene_in.distances.size(),
      g_filename_in.c_str());
  fflush(stdout);

  auto scene_out = upscaleScene(scene_in);
  dlaf::exportDLAFFile(g_filename_out.c_str(), scene_out);

  printf("upscaled %zu points to '%s'\n",
      scene_out.distances.size(),
      g_filename_out.c_str());
  fflush(stdout);

  return 0;
}
