package io.github.backspace135.musicunlock.ui.kit

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Share
import androidx.compose.material.icons.outlined.AccountCircle
import androidx.compose.material.icons.outlined.Delete
import androidx.compose.material.icons.outlined.Info
import androidx.compose.material.icons.outlined.Lock
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.Warning
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.addPathNodes
import androidx.compose.ui.unit.dp

/**
 * Every icon the app uses, in one place, so pages never import Material icons directly. Icons come from
 * material-icons-core; the few it lacks are Material Icons paths (Apache 2.0) added below.
 * To add one: prefer `Icons.Outlined.X` / `Icons.Filled.X` from material-icons-core, otherwise add a
 * `material("name", "<24×24 path data>")` entry with the path from fonts.google.com/icons.
 */
object MuIcons {
    val Back: ImageVector get() = Icons.AutoMirrored.Filled.ArrowBack
    val Chevron: ImageVector get() = Icons.AutoMirrored.Filled.KeyboardArrowRight
    val Add: ImageVector get() = Icons.Filled.Add
    val Check: ImageVector get() = Icons.Filled.Check
    val Close: ImageVector get() = Icons.Filled.Close
    val More: ImageVector get() = Icons.Filled.MoreVert
    val Refresh: ImageVector get() = Icons.Filled.Refresh
    val Search: ImageVector get() = Icons.Filled.Search
    val Share: ImageVector get() = Icons.Filled.Share
    val Account: ImageVector get() = Icons.Outlined.AccountCircle
    val Delete: ImageVector get() = Icons.Outlined.Delete
    val Info: ImageVector get() = Icons.Outlined.Info
    val Lock: ImageVector get() = Icons.Outlined.Lock
    val Settings: ImageVector get() = Icons.Outlined.Settings
    val Warning: ImageVector get() = Icons.Outlined.Warning

    val MusicNote: ImageVector by lazy {
        material("MusicNote", "M12,3v10.55c-0.59,-0.34 -1.27,-0.55 -2,-0.55 -2.21,0 -4,1.79 -4,4s1.79,4 4,4 4,-1.79 4,-4V7h4V3h-6z")
    }
    val Folder: ImageVector by lazy {
        material("Folder", "M10,4H4c-1.1,0 -1.99,0.9 -1.99,2L2,18c0,1.1 0.9,2 2,2h16c1.1,0 2,-0.9 2,-2V8c0,-1.1 -0.9,-2 -2,-2h-8l-2,-2z")
    }
    val AddFolder: ImageVector by lazy {
        material(
            "AddFolder",
            "M20,6h-8l-2,-2H4c-1.11,0 -1.99,0.89 -1.99,2L2,18c0,1.11 0.89,2 2,2h16c1.11,0 2,-0.89 2,-2V8c0,-1.11 -0.89,-2 -2,-2z" +
                "m-1,8h-3v3h-2v-3h-3v-2h3V9h2v3h3v2z",
        )
    }
    /** Lyrics: lines of text. */
    val Lyrics: ImageVector by lazy {
        material("Lyrics", "M14,17H4v2h10v-2zM20,9H4v2h16V9zM4,15h16v-2H4v2zM4,5v2h16V5H4z")
    }
    val Shield: ImageVector by lazy {
        material("Shield", "M12,1L3,5v6c0,5.55 3.84,10.74 9,12 5.16,-1.26 9,-6.45 9,-12V5l-9,-4z")
    }
    val Globe: ImageVector by lazy {
        material(
            "Globe",
            "M12,2C6.48,2 2,6.48 2,12s4.48,10 10,10 10,-4.48 10,-10S17.52,2 12,2z" +
                "m-1,17.93c-3.95,-0.49 -7,-3.85 -7,-7.93 0,-0.62 0.08,-1.21 0.21,-1.79L9,15v1c0,1.1 0.9,2 2,2v1.93z" +
                "m6.9,-2.54c-0.26,-0.81 -1,-1.39 -1.9,-1.39h-1v-3c0,-0.55 -0.45,-1 -1,-1H8v-2h2c0.55,0 1,-0.45 1,-1V7h2" +
                "c1.1,0 2,-0.9 2,-2v-0.41c2.93,1.19 5,4.06 5,7.41 0,2.08 -0.8,3.97 -2.1,5.39z",
        )
    }
    val Album: ImageVector by lazy {
        material(
            "Album",
            "M12,2C6.48,2 2,6.48 2,12s4.48,10 10,10 10,-4.48 10,-10S17.52,2 12,2z" +
                "m0,14.5c-2.49,0 -4.5,-2.01 -4.5,-4.5S9.51,7.5 12,7.5s4.5,2.01 4.5,4.5 -2.01,4.5 -4.5,4.5z" +
                "m0,-5.5c-0.55,0 -1,0.45 -1,1s0.45,1 1,1 1,-0.45 1,-1 -0.45,-1 -1,-1z",
        )
    }
    val Playlist: ImageVector by lazy {
        material(
            "Playlist",
            "M15,6H3v2h12V6zM15,10H3v2h12v-2zM3,16h8v-2H3v2z" +
                "M17,6v8.18c-0.31,-0.11 -0.65,-0.18 -1,-0.18 -1.66,0 -3,1.34 -3,3s1.34,3 3,3 3,-1.34 3,-3V8h3V6h-5z",
        )
    }
    /** Phone, for "scanned, confirm on the phone". */
    val Phone: ImageVector by lazy {
        material(
            "Phone",
            "M17,1H7C5.9,1 5,1.9 5,3v18c0,1.1 0.9,2 2,2h10c1.1,0 2,-0.9 2,-2V3C19,1.9 18.1,1 17,1z" +
                "M17,19H7V5h10V19z",
        )
    }

    val Copy: ImageVector by lazy {
        material(
            "Copy",
            "M16,1H4c-1.1,0 -2,0.9 -2,2v14h2V3h12V1z" +
                "m3,4H8c-1.1,0 -2,0.9 -2,2v14c0,1.1 0.9,2 2,2h11c1.1,0 2,-0.9 2,-2V7c0,-1.1 -0.9,-2 -2,-2zm0,16H8V7h11v14z",
        )
    }

    private fun material(name: String, path: String): ImageVector =
        ImageVector.Builder(name, 24.dp, 24.dp, 24f, 24f)
            .addPath(addPathNodes(path), fill = SolidColor(Color.Black))
            .build()
}
