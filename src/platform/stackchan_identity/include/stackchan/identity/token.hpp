#pragma once

#include "stackchan/domain/access_token.hpp"

// Generates the API token on the device and stores it.
//
// Entropy requirements
// --------------------
// Initial token generation runs before Wi-Fi starts. It temporarily enables
// the hardware entropy source around esp_random(), then disables it before
// the radio is initialized.

namespace stackchan::identity {

// Read the stored token, generating and storing one if there is none.
//
// Call this before initialising Wi-Fi, because the entropy source and
// the radio cannot be used together.
[[nodiscard]] const domain::AccessToken& access_token();

// Whether this boot generated a new token; false when a stored token was loaded.
//
// A new token is logged once at creation for initial setup.
[[nodiscard]] bool token_is_new();

// Replace the token with a freshly generated one, and store it.
//
// The token is otherwise generated once and kept, so a value that has leaked
// -- out of a pasted log, a screenshot, or a person who no longer needs
// access -- would stay valid for the life of the device. This is how it is
// withdrawn without clearing the whole configuration.
//
// Returns false, leaving the old token in place, if the new one could not be
// stored. That direction matters: a token changed in memory but not in NVS
// would work until the next restart and then silently revert.
//
// Unlike the boot path, this does not toggle the entropy source. Rotation is
// called only while Wi-Fi is active, which supplies entropy to esp_random().
//
// The caller is responsible for that precondition, because checking it here
// would mean this component knowing about the radio, which it otherwise does
// not. token.rotate, the only caller, refuses unless an interface is up.
[[nodiscard]] bool rotate_access_token();

}  // namespace stackchan::identity
