package app.englishpron.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import org.json.JSONArray
import org.json.JSONObject

@Composable
fun SectionTitle(text: String, modifier: Modifier = Modifier) =
    Text(text, style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary,
        modifier = modifier.padding(top = 16.dp, bottom = 4.dp))

@Composable
fun Empty(text: String) = Box(Modifier.fillMaxSize().padding(24.dp), Alignment.Center) {
    Text(text, style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

fun JSONArray?.objects(): List<JSONObject> = if (this == null) emptyList() else (0 until length()).mapNotNull { optJSONObject(it) }

/** Strings of an array whose items are either plain strings or objects with a text-like field. */
fun JSONArray?.texts(): List<String> = if (this == null) emptyList() else (0 until length()).mapNotNull { i ->
    when (val v = opt(i)) {
        is String -> v
        is JSONObject -> listOf("text", "phrase", "en", "sentence").firstNotNullOfOrNull { k -> v.optString(k).takeIf { it.isNotEmpty() } }
        else -> null
    }
}

fun levelLabel(score: Double) = when {
    score >= 85 -> "Отлично"
    score >= 70 -> "Хорошо"
    score >= 50 -> "Неплохо"
    else -> "Нужно поработать"
}
