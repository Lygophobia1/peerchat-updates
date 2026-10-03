package com.peerchat.android.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.unit.dp
import androidx.compose.ui.tooling.preview.Preview

private val Dark = darkColorScheme(primary = Color(0xFF85BBFF), onPrimary = Color(0xFF002F64),
    primaryContainer = Color(0xFF1E4773), onPrimaryContainer = Color(0xFFD6E6FF),
    secondaryContainer = Color(0xFF243A4F), onSecondaryContainer = Color(0xFFD8E8FF),
    surfaceContainer = Color(0xFF101F2C), surfaceContainerHigh = Color(0xFF1C2E3E),
    secondary = Color(0xFF9CBBDD), background = Color(0xFF09131D), surface = Color(0xFF101F2C),
    surfaceVariant = Color(0xFF223446), onSurface = Color(0xFFE2EBF4), onSurfaceVariant = Color(0xFFB6C6D7))
private val Light = lightColorScheme(primary = Color(0xFF125DB4), secondary = Color(0xFF486585),
    primaryContainer = Color(0xFFD5E7FF), onPrimaryContainer = Color(0xFF003568),
    secondaryContainer = Color(0xFFE0EAF5), onSecondaryContainer = Color(0xFF243A4F),
    surfaceContainer = Color(0xFFEDF3F9), surfaceContainerHigh = Color(0xFFE6EEF6),
    background = Color(0xFFF6F9FC), surface = Color(0xFFFFFFFF), surfaceVariant = Color(0xFFE6EEF6))

@Composable fun PeerChatTheme(dark: Boolean = isSystemInDarkTheme(), content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (dark) Dark else Light, content = content)
}
@Composable fun PeerChatApp(model: PeerChatPresenter) {
    val state by model.state.collectAsState()
    val dark = when (state.theme) { ThemeChoice.System -> isSystemInDarkTheme(); ThemeChoice.Dark -> true; ThemeChoice.Light -> false }
    PeerChatTheme(dark) { PeerChatShell(state, model::dispatch, model::login) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable fun PeerChatShell(state: PeerChatUiState, dispatch: (UiAction) -> Unit,
    login: (String, String) -> Unit = { _, _ -> }) {
    BackHandler(enabled = state.route != Route.Welcome) { dispatch(UiAction.Back) }
    Surface(Modifier.fillMaxSize()) {
        when (state.route) {
            Route.Welcome -> Welcome(state, dispatch)
            Route.Login -> Login(state, dispatch, login)
            Route.Server -> ServerSettings(state, dispatch)
            Route.Home -> Home(state, dispatch)
            Route.Conversation -> state.selected?.let { ConversationScreen(it, state, dispatch) }
            Route.Details -> state.selected?.let { DetailsScreen(it, state, dispatch) }
            Route.Diagnostics -> Scaffold(topBar = { PageBar("Diagnostics", dispatch) }) { padding ->
                Column(Modifier.padding(padding).padding(24.dp)) {
                    Text(if(state.demo) "UI demo — no live diagnostics" else "Native connection diagnostics", style = MaterialTheme.typography.titleLarge)
                    Text(if(state.demo) "Explicit demo mode: no live transport or identity." else state.diagnostics, Modifier.padding(top = 16.dp))
                }
            }
        }
    }
}

@Composable internal fun ActionIcon(icon: ImageVector, label: String, onClick: () -> Unit, enabled: Boolean = true) {
    IconButton(onClick, enabled = enabled) { Icon(icon, contentDescription = label) }
}
@OptIn(ExperimentalMaterial3Api::class)
@Composable internal fun PageBar(title: String, dispatch: (UiAction) -> Unit) {
    TopAppBar(title = { Text(title) }, navigationIcon = {
        ActionIcon(Icons.AutoMirrored.Filled.ArrowBack, "Back", { dispatch(UiAction.Back) })
    })
}
@Composable internal fun DemoNote() {
    Text("UI demo · local sample data only", Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
        style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
}
@Composable private fun Welcome(state: PeerChatUiState, dispatch: (UiAction) -> Unit) {
    Column(Modifier.fillMaxSize().safeDrawingPadding().padding(28.dp).verticalScroll(rememberScrollState()),
        horizontalAlignment = Alignment.CenterHorizontally) {
        Spacer(Modifier.height(72.dp))
        Surface(color = MaterialTheme.colorScheme.primary, shape = MaterialTheme.shapes.large) {
            Text("p", Modifier.padding(horizontal = 28.dp, vertical = 8.dp),
                style = MaterialTheme.typography.displayLarge, color = MaterialTheme.colorScheme.onPrimary)
        }
        Text("PeerChat", Modifier.padding(top = 28.dp), style = MaterialTheme.typography.headlineLarge)
        Text("Your people. Your space.", Modifier.padding(top = 8.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text("Private conversations, made simple.", Modifier.padding(top = 24.dp))
        Button({ dispatch(UiAction.Navigate(Route.Login)) }, Modifier.fillMaxWidth().padding(top = 32.dp)) { Text("Sign in") }
        OutlinedButton({ dispatch(UiAction.Notice("Account creation will be available when the secure account backend is connected.")) }, Modifier.fillMaxWidth()) { Text("Create account") }
        if(state.demo) TextButton({ dispatch(UiAction.Explore) }, Modifier.testTag("explore")) { Text("Explore UI demo") }
        TextButton({ dispatch(UiAction.Navigate(Route.Server)) }) { Text("Advanced / server settings") }
        Text(if(state.demo) "Sign-in and account creation are not connected in this UI-only build." else "Sign in to an existing account. Account creation UI is not available yet.", style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
        state.notice?.let { Text(it, Modifier.padding(top = 12.dp), color = MaterialTheme.colorScheme.primary) }
        if(state.demo) Text("No credentials or messages leave this preview.", Modifier.padding(top = 16.dp), style = MaterialTheme.typography.bodySmall)
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable private fun Login(state: PeerChatUiState, dispatch: (UiAction) -> Unit, login: (String, String) -> Unit) {
    // Intentionally not rememberSaveable: passwords must not enter saved-instance state.
    var username by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    var visible by remember { mutableStateOf(false) }
    var previewMenu by remember { mutableStateOf(false) }
    Scaffold(topBar = { PageBar("Sign in", dispatch) }) { padding ->
        Column(Modifier.padding(padding).imePadding().padding(24.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Text("Welcome back", style = MaterialTheme.typography.headlineMedium)
            Text("Connect with your PeerChat account.", color = MaterialTheme.colorScheme.onSurfaceVariant)
            OutlinedTextField(username, { username = it.take(64) }, label = { Text("Username or account") },
                singleLine = true, enabled = !state.loginBusy, modifier = Modifier.fillMaxWidth().testTag("username"))
            OutlinedTextField(password, { password = it.take(1024) }, label = { Text("Password") }, singleLine = true,
                enabled = !state.loginBusy, visualTransformation = if (visible) VisualTransformation.None else PasswordVisualTransformation(),
                trailingIcon = { TextButton({ visible = !visible }) { Text(if (visible) "Hide" else "Show") } },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password), modifier = Modifier.fillMaxWidth().testTag("password"))
            state.loginError?.let { Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.testTag("login-error")) }
            Row(verticalAlignment=Alignment.CenterVertically) {
                Checkbox(state.rememberMe,{dispatch(UiAction.RememberMe(it))},enabled=!state.loginBusy,modifier=Modifier.testTag("remember-me"))
                Text("Remember me")
            }
            Button({ login(username, password); password = "" }, enabled = !state.loginBusy, modifier = Modifier.fillMaxWidth().testTag("sign-in-submit")) {
                if (state.loginBusy) { CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp); Spacer(Modifier.width(10.dp)) }
                Text(if (state.loginBusy) "Checking configuration…" else "Sign in")
            }
            Text(if(state.demo) "UI preview: secure sign-in is not connected. This action never authenticates an account." else "Server TLS is verified; peer messages use authenticated end-to-end TLS.", style = MaterialTheme.typography.bodySmall)
            TextButton({ dispatch(UiAction.Navigate(Route.Server)) }) { Text("Server settings") }
            if(state.demo) Box {
                TextButton({ previewMenu = true }) { Text("Preview sign-in errors") }
                DropdownMenu(previewMenu, { previewMenu = false }) {
                    listOf("Authentication failed" to LoginResult.Rejected,
                        "Server unreachable" to LoginResult.ServerUnreachable,
                        "Backend unavailable" to LoginResult.Unavailable).forEach { (label, failure) ->
                        DropdownMenuItem(text = { Text(label) }, onClick = { previewMenu = false; dispatch(UiAction.PreviewLoginFailure(failure)) })
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable private fun ServerSettings(state: PeerChatUiState, dispatch: (UiAction) -> Unit) {
    var host by rememberSaveable { mutableStateOf(state.server.host) }
    var port by rememberSaveable { mutableStateOf(state.server.port) }
    var pin by rememberSaveable { mutableStateOf(state.server.certificatePin) }
    var error by remember { mutableStateOf<String?>(null) }
    Scaffold(topBar = { PageBar("Server & trust", dispatch) }) { padding ->
        Column(Modifier.padding(padding).imePadding().padding(24.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Text("Advanced connection", style = MaterialTheme.typography.titleLarge)
            Text("Changes apply to the next sign-in. Certificate verification cannot be disabled.")
            OutlinedTextField(host, { host = it.take(253) }, label = { Text("Discovery host") }, placeholder = { Text("Enter your server hostname") },
                singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(port, { port = it.take(5) }, label = { Text("Port") }, singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.fillMaxWidth())
            OutlinedTextField(pin,{pin=it.take(64)},label={Text("Explicit server certificate SHA-256 pin (optional)")},modifier=Modifier.fillMaxWidth())
            if(state.demo) Text("Server certificate: Not connected\nTrust/pin: Not established",color=MaterialTheme.colorScheme.onSurfaceVariant)
            Text("Leave blank for system certificate/hostname validation. Obtain development pins through a trusted independent channel.", color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text("The secure backend will verify the server identity. Certificate verification cannot be disabled here.", style = MaterialTheme.typography.bodySmall)
            error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            Button({
                if (!validServerDraft(ServerUi(host, port,pin))) error = "Enter a valid hostname, port and optional 64-character hexadecimal certificate pin."
                else { dispatch(UiAction.SaveServer(ServerUi(host.trim(), port,pin))); error = null }
            }, Modifier.fillMaxWidth()) { Text("Save server") }
            TextButton({ host = ""; port = "7443"; error = null }) { Text("Restore defaults") }
            state.notice?.let { Text(it) }
        }
    }
}

@Preview(name = "Welcome dark", showBackground = true, widthDp = 360, heightDp = 800)
@Composable private fun WelcomePreview() { PeerChatTheme(true) { PeerChatShell(PeerChatUiState(), {}) } }
@Preview(name = "Welcome light", showBackground = true, widthDp = 360, heightDp = 800)
@Composable private fun WelcomeLightPreview() { PeerChatTheme(false) { PeerChatShell(PeerChatUiState(), {}) } }
