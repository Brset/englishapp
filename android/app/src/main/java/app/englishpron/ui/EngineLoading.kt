package app.englishpron.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import app.englishpron.engine.EnginePhase
import app.englishpron.engine.EngineState

@Composable
fun EngineLoadingScreen(st: EngineState) {
    Surface(Modifier.fillMaxSize()) {
        Column(
            Modifier.fillMaxSize().padding(32.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            when (st.phase) {
                EnginePhase.FAILED -> {
                    Text("Не удалось запустить движок", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(8.dp))
                    Text(st.error.orEmpty(), style = MaterialTheme.typography.bodySmall)
                }
                EnginePhase.COPYING -> {
                    Text("Первый запуск: распаковка моделей…", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(16.dp))
                    LinearProgressIndicator(progress = { st.progress }, modifier = Modifier.fillMaxWidth())
                    Spacer(Modifier.height(8.dp))
                    Text("${(st.progress * 100).toInt()}%", style = MaterialTheme.typography.bodySmall)
                }
                else -> {
                    Text("Загрузка движка…", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(16.dp))
                    LinearProgressIndicator(Modifier.fillMaxWidth())
                }
            }
        }
    }
}
