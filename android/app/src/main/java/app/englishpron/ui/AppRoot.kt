package app.englishpron.ui

import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.LibraryBooks
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
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
                RecordScreen(practice, onResult = { nav.navigate("result") })
            }
            composable("result") {
                ResultScreen(practice, onRetry = { nav.popBackStack() }, onLibrary = {
                    if (!nav.popBackStack("library", inclusive = false)) nav.navigate("library")
                })
            }
        }
    }
}
