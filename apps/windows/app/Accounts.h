#pragma once
// Saved music platform logins (macOS MusicAccounts.swift). The lyrics workspace reads the Cookie header
// for each request from here.

#include <string>

#include "musicunlock/lyrics.h"

namespace accounts {

/// "name=value; ..." for the provider's saved session, or an empty string when not logged in.
std::string cookie_header(mu::MusicProvider provider);

}  // namespace accounts
