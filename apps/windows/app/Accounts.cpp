#include "pch.h"

#include "Accounts.h"

namespace accounts {

// Login and saved sessions arrive with the account dialog (#6); until then every request is anonymous.
std::string cookie_header(mu::MusicProvider) { return {}; }

}  // namespace accounts
