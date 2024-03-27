// Copyright 2023-2024 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

// DLAF
#include "dlaf/DLAF.h"
// std
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <string>

std::string g_filename = "particles.dlaf";
int g_numParticles = 10000;

static void printUsage()
{
  printf("usage: compute_dlaf [-np num_particles] [filename.dlaf]\n");
  std::exit(0);
}

static void parseCommandLine(int argc, char *argv[])
{
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-np" || arg == "--num-particles")
      g_numParticles = std::atoi(argv[++i]);
    else if (arg == "-h" || arg == "--help")
      printUsage();
    else
      g_filename = arg;
  }
}

int main(int argc, char *argv[])
{
  parseCommandLine(argc, argv);

  dlaf::DLAFParams params;
  dlaf::DLAFScene scene;
  params.numParticles = g_numParticles;

  std::atomic_bool done = false;
  float progress = 0.f;

  auto f = std::async([&]() {
    scene = dlaf::compute(params, &progress);
    done = true;
  });

  while (!done)
    printf("\rcomputing: %.2f%%", progress * 100);
  printf("\rcomputing: %.2f%%", 100.f);

  f.get();

  dlaf::exportDLAFFile(g_filename.c_str(), scene);

  printf("\nsaved %i points in '%s'\n", g_numParticles, g_filename.c_str());

  return 0;
}
