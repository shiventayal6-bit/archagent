// ArchAgent — Target profile
// Fingerprints the host environment (OS, compiler, architecture)
// to seed the model prompt with platform-specific context.

#ifndef ARCHAGENT_TARGET_PROFILE_H
#define ARCHAGENT_TARGET_PROFILE_H

#include <stdbool.h>

// holds the generated profile as a text string
typedef struct {
    char *text;
} TargetProfile;

// generate the target profile for the current host
// returns true on success, false on failure
bool target_profile_build(TargetProfile *profile);

// free memory allocated by target_profile_build
void target_profile_free(TargetProfile *profile);

#endif // ARCHAGENT_TARGET_PROFILE_H
