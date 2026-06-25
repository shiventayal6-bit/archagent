// Created by sm4925 on 2026/6/14

#include "target_profile.h"
#include "test_helpers.h"

int main(void) {
    TargetProfile profile;
    TEST_ASSERT(target_profile_build(&profile));
    TEST_ASSERT(profile.text != NULL);
    TEST_ASSERT_STR_CONTAINS(profile.text, "TARGET PROFILE");
    TEST_ASSERT_STR_CONTAINS(profile.text, "CPU AND ARCHITECTURE");
    TEST_ASSERT_STR_CONTAINS(profile.text, "Endianness");
    TEST_ASSERT_STR_CONTAINS(profile.text, "COMPILER");
    target_profile_free(&profile);
    printf("test_target_profile: PASS\n");
    return 0;
}