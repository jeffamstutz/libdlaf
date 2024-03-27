// Copyright 2023-2024 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

// DLAF
#include "dlaf/DLAF.h"
// std
#include <algorithm>
#include <cstdio>
#include <string>

std::string g_filename;
std::string g_basename;
int g_numSplits = 1;

static void printUsage()
{
  printf("usage: split_dlaf [num_splits] [base_name] [filename.dlaf]\n");
  std::exit(0);
}

static void parseCommandLine(int argc, char *argv[])
{
  if (argc != 4)
    printUsage();

  g_numSplits = std::atoi(argv[1]);
  g_basename = argv[2];
  g_filename = argv[3];

  if (g_numSplits <= 0) {
    printf("ERROR: 'num_splits' must be > 0");
    printUsage();
  }
}

static dlaf::DLAFScene makeParition(const dlaf::DLAFScene &scene_in, int i)
{
  if (g_numSplits == 1)
    return scene_in;

  const bool lastPartition = (i == (g_numSplits - 1));

  dlaf::DLAFScene scene_out;
  const size_t totalParticles = scene_in.distances.size();
  const size_t partitionSize = totalParticles / g_numSplits;
  const size_t copySize = lastPartition
      ? partitionSize + totalParticles % g_numSplits
      : partitionSize;

  printf("[%zu -> %zu]...", i * partitionSize, i * partitionSize + copySize);

  scene_out.points.resize(3 * copySize);
  scene_out.distances.resize(copySize);

  auto *pts_begin = scene_in.points.data() + (3 * i * partitionSize);
  auto *pts_end = pts_begin + (3 * copySize);
  std::copy(pts_begin, pts_end, scene_out.points.data());

  auto *dst_begin = scene_in.distances.data() + (i * partitionSize);
  auto *dst_end = dst_begin + copySize;
  std::copy(dst_begin, dst_end, scene_out.distances.data());

  scene_out.maxDistance = scene_in.maxDistance;
  std::copy(
      scene_in.bounds.begin(), scene_in.bounds.end(), scene_out.bounds.begin());

  return scene_out;
}

int main(int argc, char *argv[])
{
  parseCommandLine(argc, argv);

  printf("=== importing from '%s' and exporting to '%s'_part_n ===\n",
      g_filename.c_str(),
      g_basename.c_str());
  fflush(stdout);

  dlaf::DLAFScene scene_in;
  dlaf::importDLAFFile(g_filename.c_str(), scene_in);

  for (int i = 0; i < g_numSplits; i++) {
    std::string filename = g_basename + "_part_" + std::to_string(i) + ".dlaf";
    printf("exporting %s...", filename.c_str());
    fflush(stdout);
    dlaf::DLAFScene partition = makeParition(scene_in, i);
    dlaf::exportDLAFFile(filename.c_str(), partition);
    printf("done\n");
    fflush(stdout);
  }

  return 0;
}
