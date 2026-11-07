package io.github.backspace135.musicunlock.ui.nav

import java.io.Serializable

/** Every page of the app. Routes are saved across process death, so their fields must be Serializable. */
sealed interface Route : Serializable {
    data object Root : Route
}
