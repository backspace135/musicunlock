#pragma once
// Saved music platform logins (macOS MusicAccounts.swift). The lyrics workspace reads the Cookie header
// for each request from here. Sessions are sealed with DPAPI in <data dir>\sessions\<provider>.bin
// (settings::data_directory, so MUSICUNLOCK_DATA_DIR moves them for tests and renders). Thread-safe.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "musicunlock/lyrics.h"
#include "musicunlock/session.h"

namespace accounts {

/// "name=value; ..." for the provider's saved session, or an empty string when not logged in.
std::string cookie_header(mu::MusicProvider provider);

/// True while the provider has a session whose cookies have not expired. The platform can still end it
/// earlier; requests then report that a new login is needed.
bool signed_in(mu::MusicProvider provider);

/// Keeps `received` when it forms a music session and saves it encrypted. Returns the status line for the
/// user (UTF-8), or nullopt when the cookies are no session and nothing changed.
std::optional<std::string> accept(mu::MusicProvider provider, const std::vector<mu::Cookie>& received);

/// Forgets the session, deletes its file and the provider's WebView2 data (cookies and browsing data).
/// Returns the status line for the user (UTF-8).
std::string logout(mu::MusicProvider provider);

/// WebView2 user data for the provider's official login page: <data dir>\WebView2\<provider>. One folder per
/// platform, so logging out of one clears only its browsing data.
std::filesystem::path webview_folder(mu::MusicProvider provider);

/// Deletes WebView2 data that a logout could not remove while the browser still had it open.
/// False when it is still in use; call before opening a login page.
bool clear_pending_webview_data(mu::MusicProvider provider);

}  // namespace accounts
