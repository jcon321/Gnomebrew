#pragma once

// my-client.ini: every setting, its default and what it does. A new file is written with each
// key under a comment saying what it's for; a key missing from an existing file is added the
// same way, and the rest of the file (the player's own comments too) is left as it is.

#include <string>

// False when the file can't be written (`error` says why).
bool WriteDefaultSettings(const std::string &path, std::string &error);
