package com.peerchat.android.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp

internal fun validPeopleQuery(query:String):Boolean = query.length in 1..32 &&
    query.all{it.isLetterOrDigit()||it=='-'||it=='_'} &&
    (query.length>=2 || (query.toLongOrNull()?:0)>0)

@OptIn(ExperimentalMaterial3Api::class)
@Composable internal fun ContactsScreen(state:PeerChatUiState,dispatch:(UiAction)->Unit){
    val online=state.connection==ConnectionState.Online && !state.demo
    val enabled=online&&!state.contactBusy
    val title=when(state.contactPage){ContactPage.Menu->"People and chats";ContactPage.FindPeople->"Find People / Add Friend"
        ContactPage.Incoming->"Incoming Friend Requests";ContactPage.Outgoing->"Outgoing Friend Requests"
        ContactPage.Invitations->"Group Invitations";ContactPage.NewChat->"Start New Chat"}
    Scaffold(topBar={PageBar(title,dispatch)}){padding->
        LazyColumn(Modifier.fillMaxSize().padding(padding).imePadding().testTag("contacts-screen"),
            contentPadding=PaddingValues(16.dp),verticalArrangement=Arrangement.spacedBy(12.dp)){
            if(!online)item{Text("Connect to your trusted server to search or manage requests.",color=MaterialTheme.colorScheme.secondary)}
            state.notice?.let{item{Text(it,Modifier.testTag("contact-notice"))}}
            if(state.contactPage!=ContactPage.Menu)item{TextButton({dispatch(UiAction.RefreshContacts)},enabled=enabled){Text(if(state.contactBusy)"Updating…" else "Refresh")}}
            when(state.contactPage){
                ContactPage.Menu->{
                    item{ContactEntry("Find People / Add Friend"){dispatch(UiAction.Contacts(ContactPage.FindPeople))}}
                    item{ContactEntry("Incoming Friend Requests (${state.incomingRequests.size})"){dispatch(UiAction.Contacts(ContactPage.Incoming))}}
                    item{ContactEntry("Outgoing Friend Requests (${state.outgoingRequests.size})"){dispatch(UiAction.Contacts(ContactPage.Outgoing))}}
                    item{ContactEntry("Group Invitations (${state.groupInvitations.size})"){dispatch(UiAction.Contacts(ContactPage.Invitations))}}
                    item{ContactEntry("Start New Chat"){dispatch(UiAction.Contacts(ContactPage.NewChat))}}
                }
                ContactPage.FindPeople->{
                    item{OutlinedTextField(state.peopleQuery,{dispatch(UiAction.PeopleQuery(it))},singleLine=true,
                        label={Text("Username prefix or account ID")},supportingText={Text("Enter at least 2 characters, or an account ID. Up to 15 public matches.")},
                        modifier=Modifier.fillMaxWidth().testTag("people-query"))}
                    if(state.peopleLoading)item{LinearProgressIndicator(Modifier.fillMaxWidth());Text("Searching…")}
                    state.peopleError?.let{item{Text(it,color=MaterialTheme.colorScheme.error)}}
                    if(online&&validPeopleQuery(state.peopleQuery)&&!state.peopleLoading&&state.peopleError==null&&state.peopleResults.isEmpty())item{Text("No matching discoverable accounts.")}
                    items(state.peopleResults,key={it.accountId}){person->
                        val conversation=state.conversations.find{it.id=="account:${person.accountId}"}
                        val incoming=state.incomingRequests.any{it.accountId=="account:${person.accountId}"}
                        val outgoing=state.outgoingRequests.any{it.accountId=="account:${person.accountId}"}
                        ContactCard(person.username,"Account ${person.accountId} · ${if(person.online)"Online" else "Offline"}"){
                            when{conversation!=null->TextButton({dispatch(UiAction.Select(conversation.id))}){Text("Open chat")}
                                incoming->TextButton({dispatch(UiAction.Contacts(ContactPage.Incoming))}){Text("Incoming request")}
                                outgoing->Text("Request pending")
                                else->TextButton({dispatch(UiAction.RequestFriend(person.username))},enabled=enabled){Text("Add friend")}}
                        }
                    }
                }
                ContactPage.Incoming->{
                    item{Text("Accepting a request establishes the friendship. Any unexpected device still requires the existing fingerprint verification.",style=MaterialTheme.typography.bodySmall)}
                    if(state.incomingRequests.isEmpty())item{Text("No incoming requests.")}
                    items(state.incomingRequests,key={it.accountId}){request->ContactCard(request.name,"Account ${request.accountId.removePrefix("account:")}"){
                        TextButton({dispatch(UiAction.RespondFriend(request.accountId.removePrefix("account:").toLong(),true))},enabled=enabled){Text("Accept")}
                        TextButton({dispatch(UiAction.RespondFriend(request.accountId.removePrefix("account:").toLong(),false))},enabled=enabled){Text("Decline")}
                    }}
                }
                ContactPage.Outgoing->{
                    if(state.outgoingRequests.isEmpty())item{Text("No outgoing requests.")}
                    items(state.outgoingRequests,key={it.accountId}){request->ContactCard(request.name,"Account ${request.accountId.removePrefix("account:")} · Pending"){
                        TextButton({dispatch(UiAction.CancelFriend(request.accountId.removePrefix("account:").toLong()))},enabled=enabled){Text("Cancel request")}
                    }}
                }
                ContactPage.Invitations->{
                    if(state.groupInvitations.isEmpty())item{Text("No pending invitations.")}
                    items(state.groupInvitations,key={it.id}){invite->ContactCard("Group ${invite.group}","Invited by ${invite.inviter}"){
                        TextButton({dispatch(UiAction.RespondGroup(invite.id,true))},enabled=enabled){Text("Accept")}
                        TextButton({dispatch(UiAction.RespondGroup(invite.id,false))},enabled=enabled){Text("Decline")}
                    }}
                    items(state.conversations.filter{it.kind==ChatKind.Group},key={it.id}){group->ContactCard(group.name,
                        if(group.online)"Membership and current group key ready" else "Enrollment pending · waiting for signed membership and current group key"){
                        TextButton({dispatch(UiAction.Select(group.id))},enabled=group.online){Text("Open group")}
                    }}
                    item{Text("Enrollment uses existing signed membership and epoch keys. Additional device verification may be required. History access is unchanged.",style=MaterialTheme.typography.bodySmall)}
                }
                ContactPage.NewChat->{
                    val contacts=state.conversations.filter{it.kind==ChatKind.Direct}
                    if(contacts.isEmpty())item{Text("No authorized contacts yet. Find People to send a friend request.")}
                    items(contacts,key={it.id}){contact->ContactCard(contact.name,"Account ${contact.id.removePrefix("account:")}"){
                        TextButton({dispatch(UiAction.Select(contact.id))}){Text("Open chat")}
                    }}
                }
            }
        }
    }
}

@Composable private fun ContactEntry(label:String,open:()->Unit){OutlinedButton(open,Modifier.fillMaxWidth()){Text(label)}}
@Composable private fun ContactCard(name:String,detail:String,actions:@Composable RowScope.()->Unit){
    OutlinedCard(Modifier.fillMaxWidth()){
        Column(Modifier.padding(16.dp),verticalArrangement=Arrangement.spacedBy(6.dp)){
            Text(name,style=MaterialTheme.typography.titleMedium)
            Text(detail,style=MaterialTheme.typography.bodySmall)
            Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.spacedBy(8.dp),content=actions)
        }
    }
}
