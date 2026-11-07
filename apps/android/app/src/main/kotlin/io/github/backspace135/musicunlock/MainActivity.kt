package io.github.backspace135.musicunlock

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import io.github.backspace135.musicunlock.ui.theme.MuTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        setContent { MusicUnlockApp() }
    }
}

@Composable
fun MusicUnlockApp() {
    MuTheme(dark = isSystemInDarkTheme()) {
        Box(Modifier.fillMaxSize().background(MuTheme.colors.background), contentAlignment = Alignment.Center) {
            Text(stringResource(R.string.app_name), style = MuTheme.type.largeTitle)
        }
    }
}

@Preview
@Composable
private fun MusicUnlockAppPreview() {
    MusicUnlockApp()
}
