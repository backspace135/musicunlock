package io.github.backspace135.musicunlock.ui.accounts

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** Notices and one-shot navigation shared by the accounts page and the login page pushed over it. */
internal object AccountFeedback {
    var notice by mutableStateOf<String?>(null)
    var openQr by mutableStateOf(false)

    fun show(message: String) {
        notice = message.ifBlank { null }
    }
}
