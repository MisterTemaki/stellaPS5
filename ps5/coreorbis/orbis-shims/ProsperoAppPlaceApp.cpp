// Stella PS5: the app's build of ProsperoAppPlace.cpp -- no statfs(2) / getfsstat(2) (they are only in libkernel_sys,
// which a title can't load: Media Player Classic PS5 1.5 crashed at launch, its #3); the helper payload reads the
// mounts (Media Player Classic PS5 1.5.1's fix).
//
// SPDX-License-Identifier: MIT

#define STELLAPS5_APP_PROCESS 1
#include "ProsperoAppPlace.cpp"
