package app.englishpron.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import app.englishpron.engine.LiveState

/**
 * Paragraph mode: the current paragraph carries the live marks, the others are dimmed (already read ones a bit less).
 * The list scrolls to the current paragraph; while idle a dimmed paragraph can be tapped to become the current one.
 */
@Composable
fun ParagraphReadingText(
    paragraphs: List<String>, current: Int, live: LiveState?, readParagraphs: Set<Int>, canSelect: Boolean,
    style: TextStyle, onTapWord: (Int) -> Unit, onSelect: (Int) -> Unit, modifier: Modifier = Modifier,
) {
    val listState = rememberLazyListState()
    val cs = MaterialTheme.colorScheme
    LaunchedEffect(current) { if (paragraphs.isNotEmpty()) listState.animateScrollToItem(current.coerceIn(0, paragraphs.size - 1)) }
    LazyColumn(modifier, state = listState, contentPadding = PaddingValues(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        itemsIndexed(paragraphs, key = { i, _ -> i }) { i, p ->
            val isCur = i == current
            val a by animateFloatAsState(if (isCur) 1f else if (i in readParagraphs) 0.55f else 0.35f, tween(400), label = "paragraphAlpha")
            var layout by remember { mutableStateOf<TextLayoutResult?>(null) }
            val annotated = remember(p, isCur, live, cs) { buildLiveAnnotated(p, if (isCur) live else null, cs) }
            Text(
                annotated, style = style, onTextLayout = { layout = it },
                modifier = Modifier.fillMaxWidth().alpha(a).clip(RoundedCornerShape(12.dp))
                    .background(if (isCur) cs.primaryContainer.copy(alpha = 0.25f) else Color.Transparent)
                    .then(if (!isCur && canSelect) Modifier.clickable { onSelect(i) } else Modifier)
                    .pointerInput(isCur, live != null) {
                        if (!isCur || live == null) return@pointerInput
                        detectTapGestures { pos ->
                            val l = layout ?: return@detectTapGestures
                            val off = l.getOffsetForPosition(pos)
                            live.words.firstOrNull { off >= it.u16Begin && off <= it.u16End && it.u16Begin >= 0 }?.let { onTapWord(it.index) }
                        }
                    }
                    .padding(10.dp),
            )
        }
    }
}
