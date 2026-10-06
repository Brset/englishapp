package app.englishpron

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.getValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import app.englishpron.engine.EnginePhase
import app.englishpron.ui.AppRoot
import app.englishpron.ui.EngineLoadingScreen
import app.englishpron.ui.AppTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            AppTheme {
                val st by (application as EnglishApp).engine.state.collectAsStateWithLifecycle()
                if (st.phase == EnginePhase.READY) AppRoot() else EngineLoadingScreen(st)
            }
        }
    }
}
