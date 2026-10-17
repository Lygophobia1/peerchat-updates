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
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.unit.dp
import androidx.compose.ui.tooling.preview.Preview
import com.peerchat.android.platform.ServerEndpoint
import com.peerchat.android.platform.ServerTrustState
import com.peerchat.android.platform.serverTrustState
import com.peerchat.android.platform.DiagnosticsSnapshot
import com.peerchat.android.platform.DiagnosticsExport
import com.peerchat.android.platform.DiagnosticsCreateDocument
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.CancellationException
import java.time.Instant
import android.content.Intent

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
    val conversation=state.selected?.takeIf{state.route==Route.Conversation}
    val ids=conversation?.messages?.map{it.id}?.toSet()?:emptySet()
    LaunchedEffect(state.account?.accountId,state.conversations.size,conversation?.id,ids){
        withFrameNanos{ }
        model.rendered(conversation?.id,ids)
    }
    val dark = when (state.theme) { ThemeChoice.System -> isSystemInDarkTheme(); ThemeChoice.Dark -> true; ThemeChoice.Light -> false }
    PeerChatTheme(dark) { PeerChatShell(state, model::dispatch, model::login) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable fun PeerChatShell(state: PeerChatUiState, dispatch: (UiAction) -> Unit,
    login: (String, String) -> Unit = { _, _ -> }) {
    BackHandler(enabled = state.route != Route.Welcome) { dispatch(UiAction.Back) }
    Surface(Modifier.fillMaxSize()) {
        state.attachmentCompatibility?.let { choice ->
            AlertDialog(onDismissRequest={if(!state.attachmentCompatibilityBusy)dispatch(UiAction.ResolveAttachmentCompatibility(choice.token,false))},
                title={Text("Offline attachment compatibility")},
                text={Column(Modifier.heightIn(max=360.dp).verticalScroll(rememberScrollState()),verticalArrangement=Arrangement.spacedBy(10.dp)){
                    Text("Some recipient devices need an update for cached delivery. The server did not provide the affected device IDs.")
                    Text((if(choice.directReady)"Selected verified device: " else "Selected device (verified online connection required): ")+choice.device,style=MaterialTheme.typography.labelSmall)
                    Text("${choice.name}\nDirect sending reaches only the currently connected, verified device; other devices will not receive this file. No automatic fallback has occurred.")
                    if(!choice.directReady)Text("A verified online file connection is required. Connect to the recipient and select the file again. The private source is retained.")
                }},
                confirmButton={TextButton(onClick={dispatch(UiAction.ResolveAttachmentCompatibility(choice.token,true))},enabled=choice.directReady&&!state.attachmentCompatibilityBusy){Text("Send Directly to Online Device")}},
                dismissButton={TextButton(onClick={dispatch(UiAction.ResolveAttachmentCompatibility(choice.token,false))},enabled=!state.attachmentCompatibilityBusy){Text("Cancel")}})
        }
        if(state.serverTrust==null)state.deviceVerification?.let { value ->
            fun fingerprint(id:String)=id.chunked(4).joinToString(" ").uppercase()
            AlertDialog(onDismissRequest={dispatch(UiAction.VerifyDevice(value,false))},
                title={Text("Verify new device identity")},
                text={Column(Modifier.heightIn(max=360.dp).verticalScroll(rememberScrollState()),verticalArrangement=Arrangement.spacedBy(12.dp)){
                    Text("${value.contact} · account ${value.account}")
                    Text("A different device was presented for an account with previously verified identities. This may be an additional/replacement device or an impersonation attempt.")
                    Text("Previously trusted fingerprints:\n"+value.previous.joinToString("\n"){fingerprint(it)})
                    Text("Presented device fingerprint:\n"+fingerprint(value.presented))
                    if(!value.live)Text("This presentation expired. Waiting for a fresh authenticated connection; approval is disabled.")
                    Text("Compare the complete presented fingerprint with your contact through an independent trusted channel before accepting. Previous identities and queued recipients will not be replaced.")
                }},
                confirmButton={TextButton({dispatch(UiAction.VerifyDevice(value,true))},enabled=value.live){Text("Verify and Accept")}},
                dismissButton={TextButton({dispatch(UiAction.VerifyDevice(value,false))}){Text("Keep Untrusted")}})
        }
        state.serverTrust?.let { trust ->
            fun fingerprint(pin:String)=pin.chunked(2).joinToString(":").uppercase()
            AlertDialog(onDismissRequest={dispatch(UiAction.CancelServerTrust)},
                title={Text(if(trust.expected.isEmpty())"Trust this PeerChat server?" else "Server identity changed")},
                text={Column(verticalArrangement=Arrangement.spacedBy(12.dp)){
                    Text("Server: "+trust.server.host+":"+trust.server.port)
                    if(trust.expected.isNotEmpty())Text("Expected SHA-256:\n"+fingerprint(trust.expected))
                    Text("Certificate SHA-256:\n"+fingerprint(trust.observed))
                    Text(if(trust.expected.isEmpty())"Verify this fingerprint with the server operator before trusting."
                        else "Connection rejected. The stored pin has not been changed; explicit reconfiguration is required.")
                }},
                confirmButton={if(trust.expected.isEmpty())TextButton({dispatch(UiAction.TrustServer)}){Text("Trust Server")}},
                dismissButton={TextButton({dispatch(UiAction.CancelServerTrust)}){Text(if(trust.expected.isEmpty())"Cancel" else "Close")}})
        }
        if(state.serverTrust==null&&state.deviceVerification==null){
            state.mailboxReenrollment?.let { value ->
                fun decide(accept:Boolean){if(!state.mailboxRecoveryBusy)dispatch(UiAction.ReenrollMailbox(value,accept))}
                AlertDialog(onDismissRequest={decide(false)},title={Text("Re-enroll this phone's mailbox?")},
                    text={Column(Modifier.heightIn(max=360.dp).verticalScroll(rememberScrollState()),verticalArrangement=Arrangement.spacedBy(12.dp)){
                        Text("Original account ${value.oldAccount} → current account ${value.account}")
                        Text("Register this phone's existing encryption key for future offline messages to account ${value.account}. The device identity and private key stay the same.")
                        Text("Server: ${value.server}")
                        Text("Server certificate fingerprint:\n${value.serverFingerprint.chunked(4).joinToString(" ").uppercase()}")
                        Text("Phone fingerprint:\n${value.device.chunked(4).joinToString(" ").uppercase()}")
                        Text("Source key enrollment: ${value.sourceKey} · generation ${value.sourceGeneration}. The new enrollment identifies the same existing private key.")
                        Text("Original history and the account ${value.oldAccount} binding are preserved. This grants no access to account ${value.oldAccount}, and does not redirect messages addressed to another device.")
                        Text("Offline receiving starts only after authenticated server registration succeeds.")
                        if(state.mailboxRecoveryBusy)Text("Saving the enrollment request…")
                        state.mailboxRecoveryError?.let{Text(it,color=MaterialTheme.colorScheme.error)}
                    }},
                    confirmButton={TextButton({decide(true)},enabled=!state.mailboxRecoveryBusy){Text("Re-enroll mailbox")}},
                    dismissButton={TextButton({decide(false)},enabled=!state.mailboxRecoveryBusy){Text("Keep existing binding")}})
            }
            if(state.mailboxReenrollment==null)state.mailboxRecoveryError?.let { error ->
                var dismissed by remember(error,state.account){mutableStateOf(false)}
                if(!dismissed)AlertDialog(onDismissRequest={dismissed=true},title={Text("Offline mailbox needs attention")},
                    text={Column(verticalArrangement=Arrangement.spacedBy(12.dp)){
                        Text(error)
                        Text("Reconnect to the trusted server, then review any new enrollment offer. Existing keys, bindings and history are preserved.")
                    }},confirmButton={TextButton({dismissed=true}){Text("Close")}})
            }
        }
        if(state.startupStatus!=null) {
            Column(Modifier.fillMaxSize().safeDrawingPadding().padding(24.dp),
                verticalArrangement=Arrangement.Center,horizontalAlignment=Alignment.CenterHorizontally) {
                CircularProgressIndicator()
                Text(state.startupStatus,Modifier.padding(top=16.dp).testTag("startup-status"))
                TextButton({dispatch(UiAction.ResetNetworkSession)},Modifier.testTag("startup-reset-network")) {Text("Reset Network Session / Return to sign in")}
                TextButton({dispatch(UiAction.DisconnectChangeServer)}) {Text("Disconnect and change server")}
            }
        } else when (state.route) {
            Route.Welcome -> Welcome(state, dispatch)
            Route.Login, Route.Register -> Login(state, dispatch, login)
            Route.Server -> ServerSettings(state, dispatch)
            Route.Home -> Home(state, dispatch)
            Route.Contacts -> ContactsScreen(state,dispatch)
            Route.TransferSettings -> TransferSettingsScreen(state,dispatch)
            Route.Conversation -> if(state.selected!=null)ConversationScreen(checkNotNull(state.selected),state,dispatch) else Column(Modifier.fillMaxSize().padding(24.dp)){
                Text("This conversation is currently unavailable for this profile. Existing local history has not been deleted.")
                TextButton({dispatch(UiAction.Navigate(Route.Home))}){Text("Return to chats")}
                TextButton({dispatch(UiAction.RefreshContacts)}){Text("Refresh contacts")}
            }
            Route.Details -> state.selected?.let { DetailsScreen(it, state, dispatch) }
            Route.Diagnostics -> DiagnosticsScreen(state, dispatch)
        }
    }
}

internal fun diagnosticReport(state:PeerChatUiState):String =
    "PeerChat Android diagnostics\nBuild: ${com.peerchat.android.BuildConfig.VERSION_NAME}\n"+
        if(state.demo)"Explicit demo mode: no live transport or identity." else state.diagnostics+"\nUI frame timing observations:\n"+state.presentationDiagnostics

@OptIn(ExperimentalMaterial3Api::class)
@Composable internal fun DiagnosticsScreen(state:PeerChatUiState,dispatch:(UiAction)->Unit){
    val clipboard=LocalClipboardManager.current
    val context=LocalContext.current
    val scope=rememberCoroutineScope()
    val source=if(state.demo)"Explicit demo mode: no live transport or identity." else state.diagnostics
    // Avoid a second full-size concatenation just to render/copy a preview.
    val report=remember(source){if(source.length<=65000)diagnosticReport(state) else null}
    val preview=remember(source){"PeerChat Android diagnostics\nBuild: ${com.peerchat.android.BuildConfig.VERSION_NAME}\n"+source.take(32768)}
    var copied by remember(source){mutableStateOf(false)}
    var busy by remember{mutableStateOf(false)}
    var exportStatus by remember{mutableStateOf<String?>(null)}
    // Kept only in memory across the system picker, never saved instance state.
    // Later runtime updates cannot alter the snapshot the user chose to export.
    var pending by remember{mutableStateOf<DiagnosticsSnapshot?>(null)}
    fun capture()=DiagnosticsSnapshot(com.peerchat.android.BuildConfig.VERSION_NAME,Instant.now(),
        if(state.demo)"Explicit demo mode: no live transport or identity." else state.diagnostics)
    val save=rememberLauncherForActivityResult(DiagnosticsCreateDocument()){uri->
        val frozen=pending;pending=null
        if(uri==null){busy=false;exportStatus="Export cancelled."}
        else if(frozen==null){busy=false;exportStatus="Snapshot no longer available. Tap Export Diagnostics again."}
        else scope.launch{
            try{withContext(Dispatchers.IO){DiagnosticsExport.save(context,uri,frozen)};exportStatus="Complete diagnostic snapshot saved."}
            catch(error:Exception){if(error is CancellationException)throw error;exportStatus="Diagnostics could not be saved. Choose a writable location and retry; an incomplete file may remain."}
            finally{busy=false}
        }
    }
    Scaffold(topBar={TopAppBar(title={Text("Diagnostics")},navigationIcon={
        ActionIcon(Icons.AutoMirrored.Filled.ArrowBack,"Back",{dispatch(UiAction.Back)})
    },actions={TextButton({try{clipboard.setText(AnnotatedString(checkNotNull(report)));copied=true}catch(_:Exception){exportStatus="Clipboard unavailable. Use Export Diagnostics to save the complete report."}},Modifier.testTag("diagnostics-copy"),enabled=report!=null){
        Text(if(copied)"Copied" else "Copy diagnostics")
    }})}){padding->
        Column(Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState()).padding(24.dp).testTag("diagnostics-scroll")){
            Text(if(state.demo)"UI demo — no live diagnostics" else "Native connection diagnostics",style=MaterialTheme.typography.titleLarge)
            Button({
                try{val frozen=capture();pending=frozen;busy=true;exportStatus=null;save.launch(frozen.suggestedName)}
                catch(_:Exception){pending=null;busy=false;exportStatus="Diagnostics could not be prepared. Retry with the current snapshot."}
            },enabled=!busy,modifier=Modifier.fillMaxWidth().padding(top=12.dp).testTag("diagnostics-export")){Text("Export Diagnostics")}
            OutlinedButton({
                try{val frozen=capture();busy=true;exportStatus=null;scope.launch{
                    try{
                        val intent=withContext(Dispatchers.IO){DiagnosticsExport.share(context,frozen)}
                        context.startActivity(Intent.createChooser(intent,"Share diagnostics"));exportStatus="Diagnostic file prepared for sharing. Choose a destination in the Android Share menu."
                    }catch(error:Exception){if(error is CancellationException)throw error;exportStatus="Diagnostics could not be shared. Use Export Diagnostics to save a file."}
                    finally{busy=false}
                }}catch(_:Exception){busy=false;exportStatus="Diagnostics could not be prepared. Retry with the current snapshot."}
            },enabled=!busy,modifier=Modifier.fillMaxWidth().testTag("diagnostics-share")){Text("Share diagnostic file")}
            if(busy)LinearProgressIndicator(Modifier.fillMaxWidth())
            exportStatus?.let{Text(it,Modifier.padding(top=8.dp).testTag("diagnostics-export-status"))}
            Text("Save or share the complete snapshot as a UTF-8 text file. Sharing happens only when you choose an app; PeerChat does not upload this report.",Modifier.padding(top=8.dp),style=MaterialTheme.typography.bodySmall)
            if(report==null)Text("This report is too large for Copy. Use Export Diagnostics or Share diagnostic file.",style=MaterialTheme.typography.bodySmall)
            if(source.length>32768)Text("Preview shortened for display. File export includes the complete available snapshot.",style=MaterialTheme.typography.bodySmall)
            SelectionContainer{Text(preview,Modifier.padding(top=16.dp).testTag("diagnostics-full-text"))}
            Text("End of diagnostics",Modifier.padding(top=16.dp).testTag("diagnostics-end"),style=MaterialTheme.typography.labelSmall)
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
        OutlinedButton({ dispatch(UiAction.Navigate(Route.Register)) }, Modifier.fillMaxWidth()) { Text("Create account") }
        if(state.demo) TextButton({ dispatch(UiAction.Explore) }, Modifier.testTag("explore")) { Text("Explore UI demo") }
        TextButton({ dispatch(UiAction.Navigate(Route.Server)) }) { Text("Advanced / server settings") }
        Text(if(state.demo) "Sign-in and account creation are not connected in this UI-only build." else "Sign in or create an account on your trusted server.", style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
        state.notice?.let { Text(it, Modifier.padding(top = 12.dp), color = MaterialTheme.colorScheme.primary) }
        if(state.demo) Text("No credentials or messages leave this preview.", Modifier.padding(top = 16.dp), style = MaterialTheme.typography.bodySmall)
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable private fun Login(state: PeerChatUiState, dispatch: (UiAction) -> Unit, login: (String, String) -> Unit) {
    // Intentionally not rememberSaveable: passwords must not enter saved-instance state.
    val creating=state.route==Route.Register
    var username by remember(creating) { mutableStateOf("") }
    var password by remember(creating) { mutableStateOf("") }
    var visible by remember { mutableStateOf(false) }
    var previewMenu by remember { mutableStateOf(false) }
    Scaffold(topBar = { PageBar(if(creating)"Create account" else "Sign in", dispatch) }) { padding ->
        Column(Modifier.padding(padding).imePadding().padding(24.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Text(if(creating)"Create your account" else "Welcome back", style = MaterialTheme.typography.headlineMedium)
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
                Text(if (state.loginBusy) "Connecting…" else if(creating)"Create account" else "Sign in")
            }
            Text(if(creating)"Already have an account?" else "Don't have an account?")
            TextButton({dispatch(UiAction.Navigate(if(creating)Route.Login else Route.Register))},enabled=!state.loginBusy,modifier=Modifier.testTag("account-mode-switch")){
                Text(if(creating)"Back to Sign In" else "Create Account")
            }
            if(creating)Text("This device identity can belong to only one server account. Switching screens does not replace it.",style=MaterialTheme.typography.bodySmall)
            Text(if(state.demo) "UI preview: secure sign-in is not connected. This action never authenticates an account." else "Server TLS is verified; peer messages use authenticated end-to-end TLS.", style = MaterialTheme.typography.bodySmall)
            TextButton({ dispatch(UiAction.Navigate(Route.Server)) }) { Text("Server settings") }
            if(!state.demo) {
                TextButton({dispatch(UiAction.ResetNetworkSession)},Modifier.testTag("login-reset-network")) {Text("Reset Network Session")}
                TextButton({dispatch(UiAction.DisconnectChangeServer)},Modifier.testTag("login-change-server")) {Text("Disconnect and change server")}
            }
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
    var host by rememberSaveable(state.server) { mutableStateOf(state.server.host) }
    var port by rememberSaveable(state.server) { mutableStateOf(state.server.port) }
    var pin by rememberSaveable(state.server) { mutableStateOf(state.server.certificatePin) }
    var error by remember { mutableStateOf<String?>(null) }
    var advanced by rememberSaveable { mutableStateOf(false) }
    Scaffold(topBar = { PageBar("Server & trust", dispatch) }) { padding ->
        Column(Modifier.padding(padding).imePadding().padding(24.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Text("Connect to your server", style = MaterialTheme.typography.titleLarge)
            Text("Changes apply to the next sign-in. Certificate verification cannot be disabled.")
            if(state.loginBusy||state.connection!=ConnectionState.Offline) {
                OutlinedButton({dispatch(UiAction.DisconnectChangeServer)},Modifier.testTag("disconnect-change-server")) {Text("Disconnect and change server")}
            }
            val trust=serverTrustState(ServerEndpoint(host,port.toIntOrNull() ?: 0),
                ServerEndpoint(state.server.host,state.server.port.toIntOrNull() ?: 0,state.server.certificatePin),
                state.serverIdentityChanged?.let{ServerEndpoint(it.host,it.port.toIntOrNull() ?: 0,it.certificatePin)})
            Text("Server trust: "+when(trust){
                ServerTrustState.Trusted->"Trusted"
                ServerTrustState.CertificateChanged->"Certificate changed"
                else->"Untrusted"
            },Modifier.testTag("server-trust-status"))
            OutlinedTextField(host, { host = it.take(253);pin="" }, label = { Text("Discovery host") }, placeholder = { Text("Enter your server hostname") },
                singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(port, { port = it.take(5);pin="" }, label = { Text("Port") }, singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.fillMaxWidth())
            TextButton({advanced=!advanced}){Text(if(advanced)"Hide advanced pin entry" else "Advanced: enter certificate pin manually")}
            if(advanced)OutlinedTextField(pin,{pin=it.filterNot{c->c==':'||c.isWhitespace()}.take(64)},label={Text("Explicit server certificate SHA-256 pin")},modifier=Modifier.fillMaxWidth())
            if(state.demo) Text("Server certificate: Not connected\nTrust/pin: Not established",color=MaterialTheme.colorScheme.onSurfaceVariant)
            Text("New server fingerprints require your explicit approval. Trusted identities cannot be replaced automatically.", color = MaterialTheme.colorScheme.onSurfaceVariant)
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
