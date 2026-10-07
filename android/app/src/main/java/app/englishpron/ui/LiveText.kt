package app.englishpron.ui

import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.englishpron.engine.LiveState
import app.englishpron.engine.WordState
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import androidx.compose.runtime.snapshotFlow

/**
 * Reading text with live word highlighting (teleprompter). [live] == null renders plain text.
 * Auto-scrolls so the word at `scroll_to` sits about 1/3 from the top; manual scrolling pauses it for 3 s.
 */
@Composable
fun LiveReadingText(
    text: String, live: LiveState?, onTapWord: (Int) -> Unit, modifier: Modifier = Modifier,
    style: TextStyle = TextStyle(fontSize = 20.sp, lineHeight = 30.sp),
) {
    val scroll = rememberScrollState()
    val density = LocalDensity.current
    var layout by remember { mutableStateOf<TextLayoutResult?>(null) }
    var viewportH by remember { mutableIntStateOf(0) }
    val holder = remember { object { var autoScrolls = 0; var pausedUntil = 0L } }

    val cs = MaterialTheme.colorScheme
    val annotated = remember(text, live, cs) { buildLiveAnnotated(text, live, cs) }

    // Manual scroll pauses auto-scroll for 3 s.
    LaunchedEffect(scroll) {
        snapshotFlow { scroll.isScrollInProgress }.collectLatest { moving ->
            if (moving && holder.autoScrolls == 0) holder.pausedUntil = System.currentTimeMillis() + 3000
        }
    }

    val target = live?.scrollTo ?: -1
    LaunchedEffect(target, layout, viewportH) {
        val l = layout ?: return@LaunchedEffect
        val st = live ?: return@LaunchedEffect
        if (viewportH <= 0 || System.currentTimeMillis() < holder.pausedUntil) return@LaunchedEffect
        val w = st.words.firstOrNull { it.index == target } ?: return@LaunchedEffect
        if (w.u16Begin < 0 || w.u16Begin >= l.layoutInput.text.length) return@LaunchedEffect
        val box = l.getBoundingBox(w.u16Begin)
        val pad = with(density) { 16.dp.toPx() }
        val y = (box.top + pad - viewportH / 3f).toInt().coerceIn(0, scroll.maxValue)
        holder.autoScrolls++
        try { scroll.animateScrollTo(y) } finally { holder.autoScrolls-- }
    }

    Box(modifier.onSizeChanged { viewportH = it.height }) {
        Column(Modifier.fillMaxSize().verticalScroll(scroll).padding(16.dp)) {
            Text(
                annotated, style = style,
                onTextLayout = { layout = it },
                modifier = Modifier.pointerInput(live != null) {
                    if (live == null) return@pointerInput
                    detectTapGestures { pos ->
                        val l = layout ?: return@detectTapGestures
                        val off = l.getOffsetForPosition(pos)
                        val st = live
                        val hit = st.words.firstOrNull { off >= it.u16Begin && off <= it.u16End && it.u16Begin >= 0 }
                        if (hit != null) onTapWord(hit.index)
                    }
                },
            )
        }
    }
}

/** Live marks over [text]: read = soft green, skipped = orange underline, current = accent, pending = muted. */
fun buildLiveAnnotated(text: String, live: LiveState?, cs: ColorScheme): AnnotatedString = buildAnnotatedString {
    if (live == null) { append(text); return@buildAnnotatedString }
    val primary = cs.primary
    val pending = cs.onSurface.copy(alpha = 0.45f)
    val green = Color(0xFF2E7D32)
    val orange = Color(0xFFEF6C00)
    var last = 0
    for (w in live.words.sortedBy { it.u16Begin }) {
        if (w.u16Begin < last || w.u16End < w.u16Begin || w.u16End > text.length) continue
        append(text.substring(last, w.u16Begin))
        val style = when (w.state) {
            WordState.READ -> SpanStyle(color = primary, background = green.copy(alpha = 0.14f))
            WordState.SKIPPED -> SpanStyle(color = orange, textDecoration = TextDecoration.Underline)
            WordState.CURRENT -> SpanStyle(color = cs.onPrimaryContainer, background = cs.primaryContainer, fontWeight = FontWeight.Bold)
            WordState.PENDING -> SpanStyle(color = pending)
        }
        withStyle(style) { append(text.substring(w.u16Begin, w.u16End)) }
        last = w.u16End
    }
    append(text.substring(last))
}
