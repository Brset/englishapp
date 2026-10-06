package app.englishpron.ui

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.padding
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import app.englishpron.EnglishApp
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.LibraryBooks
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavType
import androidx.navigation.compose.*
import androidx.navigation.navArgument

private data class Tab(val route: String, val title: String, val icon: ImageVector)

private val tabs = listOf(
    Tab("home", "Главная", Icons.Filled.Home),
    Tab("library", "Библиотека", Icons.AutoMirrored.Filled.LibraryBooks),
    Tab("training", "Тренировка", Icons.Filled.Mic),
    Tab("dictionary", "Словарь", Icons.Filled.Book),
    Tab("progress", "Прогресс", Icons.Filled.BarChart),
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppRoot() {
    val nav = rememberNavController()
    val ctx = LocalContext.current
    val queue = remember { (ctx.applicationContext as EnglishApp).queue }
    val jobs by queue.jobs.collectAsStateWithLifecycle()
    var showQueue by rememberSaveable { mutableStateOf(false) }
    var askedNotif by rememberSaveable { mutableStateOf(false) }
    val notifLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {}
    LaunchedEffect(jobs.isNotEmpty()) {
        // Android 13+: the progress notification needs POST_NOTIFICATIONS (the service runs without it, silently).
        if (jobs.isNotEmpty() && !askedNotif && Build.VERSION.SDK_INT >= 33 &&
            ContextCompat.checkSelfPermission(ctx, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            askedNotif = true
            notifLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }
    if (showQueue) ProcessingSheet(jobs, onCancel = { queue.cancel(it) }, onDismiss = { showQueue = false })
    val practice: PracticeViewModel = viewModel() // shared by reading / record / result
    val entry by nav.currentBackStackEntryAsState()
    val route = entry?.destination?.route
    val topLevel = tabs.any { it.route == route }
    val title = when {
        route == null -> ""
        route == "settings" -> "Настройки"
        route.startsWith("reading") -> "Чтение"
        route == "record" -> "Запись"
        route == "result" -> "Результат"
        route.startsWith("sound") -> "Звук"
        else -> tabs.first { it.route == route }.title
    }

    Scaffold(
        topBar = {
            CenterAlignedTopAppBar(
                title = { Text(title) },
                navigationIcon = {
                    if (!topLevel) IconButton(onClick = { nav.popBackStack() }) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Назад")
                    }
                },
                actions = {
                    QueueIndicator(jobs) { showQueue = true }
                    if (topLevel) IconButton(onClick = { nav.navigate("settings") }) {
                        Icon(Icons.Filled.Settings, contentDescription = "Настройки")
                    }
                },
            )
        },
        bottomBar = {
            if (topLevel) NavigationBar {
                tabs.forEach { t ->
                    NavigationBarItem(
                        selected = route == t.route,
                        onClick = {
                            nav.navigate(t.route) {
                                popUpTo(nav.graph.findStartDestination().id) { saveState = true }
                                launchSingleTop = true
                                restoreState = true
                            }
                        },
                        icon = { Icon(t.icon, contentDescription = null) },
                        label = { Text(t.title) },
                    )
                }
            }
        },
    ) { pad ->
        NavHost(nav, startDestination = "home", modifier = Modifier.padding(pad)) {
            composable("home") {
                HomeScreen(onOpenText = { nav.navigate("reading/$it") }, onDictionary = { nav.navigate("dictionary") })
            }
            composable("library") { LibraryScreen(onOpen = { nav.navigate("reading/$it") }) }
            composable("training") { SoundsScreen(onOpen = { nav.navigate("sound/$it") }) }
            composable("sound/{id}", listOf(navArgument("id") { type = NavType.StringType })) {
                SoundDetailScreen(it.arguments?.getString("id").orEmpty())
            }
            composable("dictionary") { DictionaryScreen() }
            composable("progress") { ProgressScreen() }
            composable("settings") { SettingsScreen() }
            composable("reading/{id}", listOf(navArgument("id") { type = NavType.StringType })) {
                ReadingScreen(practice, it.arguments?.getString("id").orEmpty(), onRecord = { nav.navigate("record") })
            }
            composable("record") {
                // Reading saved: back to the text (it is already marked as read; the assessment runs in the queue).
                RecordScreen(practice, onResult = { if (nav.currentDestination?.route == "record") nav.popBackStack() })
            }
        }
    }
}
