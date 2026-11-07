package io.github.backspace135.musicunlock.ui.nav

import androidx.compose.animation.core.AnimationSpec
import androidx.compose.animation.core.animate
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import io.github.backspace135.musicunlock.ui.theme.MuMotion
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * The page stack. Get it with `LocalNavigator.current` and call [push] / [pop]; every route is declared
 * in Routes.kt. Pages below the top stay composed (their state and running work survive), but only the
 * top page is "active" (see [LocalPageActive]). Each page has its own ViewModelStore, so `viewModel()`
 * inside a page lives exactly as long as the page is on the stack.
 */
@Stable
class Navigator internal constructor(initial: List<PageEntry>, private var nextId: Long) {
    internal val entries = mutableStateListOf<PageEntry>().apply { addAll(initial) }

    /** 1 when the top page fully covers the one below it, 0 when it has slid away. */
    internal var progress by mutableFloatStateOf(1f)

    /** Width of the stack in px, set by [PageStackHost]; swipe distances are relative to it. */
    internal var width = 1f
    internal var scope: CoroutineScope? = null
    /** Serialises push, pop and swipe-back so they never fight over [progress]. */
    internal val mutex = Mutex()

    val current: Route get() = entries.last().route
    val canGoBack: Boolean get() = entries.size > 1
    val routes: List<Route> get() = entries.map { it.route }

    /** Slides [route] in from the right over the current page. */
    fun push(route: Route) {
        val entry = PageEntry(nextId++, route)
        transition(immediately = { entries += entry }) {
            entries += entry
            progress = 0f
            animateProgress(1f, 0f, MuMotion.page())
        }
    }

    /** Slides the top page away to the right. Does nothing on the root page. */
    fun pop() {
        transition(immediately = { if (canGoBack) removeTop() }) {
            if (canGoBack) {
                animateProgress(0f, 0f, MuMotion.page())
                removeTop()
            }
        }
    }

    /** Pops every page above the root in one transition. */
    fun popToRoot() {
        transition(immediately = { while (canGoBack) removeTop() }) {
            if (canGoBack) {
                while (entries.size > 2) entries.removeAt(entries.lastIndex - 1).viewModelStore.clear()
                animateProgress(0f, 0f, MuMotion.page())
                removeTop()
            }
        }
    }

    /** Runs [animated] after any running transition, or [immediately] when the stack is not shown yet. */
    private fun transition(immediately: () -> Unit, animated: suspend () -> Unit) {
        val scope = scope ?: return immediately()
        scope.launch { mutex.withLock { animated() } }
    }

    internal suspend fun animateProgress(target: Float, velocity: Float, spec: AnimationSpec<Float>) {
        animate(progress, target, velocity, spec) { value, _ -> progress = value }
    }

    internal fun removeTop() {
        entries.removeAt(entries.lastIndex).viewModelStore.clear()
        progress = 1f
    }

    internal companion object {
        fun saver() = listSaver<Navigator, Any>(
            save = { navigator -> listOf<Any>(navigator.nextId) + navigator.entries.flatMap { listOf(it.id, it.route) } },
            restore = { saved ->
                val entries = saved.drop(1).chunked(2).map { (id, route) -> PageEntry(id as Long, route as Route) }
                Navigator(entries, saved[0] as Long)
            },
        )
    }
}

/** One page on the stack: its route plus the ViewModelStore of the page. */
class PageEntry internal constructor(val id: Long, val route: Route) : ViewModelStoreOwner {
    override val viewModelStore = ViewModelStore()
}

@Composable
fun rememberNavigator(root: Route): Navigator =
    rememberSaveable(saver = Navigator.saver()) { Navigator(listOf(PageEntry(0, root)), nextId = 1) }

val LocalNavigator = staticCompositionLocalOf<Navigator> { error("No Navigator: wrap the content in PageStackHost") }

/**
 * True for the top page, false for pages covered by another one. Gate page-level `BackHandler`s with it:
 * `BackHandler(enabled = LocalPageActive.current && searchOpen) { … }`.
 */
val LocalPageActive = compositionLocalOf { true }
