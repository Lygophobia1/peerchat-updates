package com.peerchat.android.ui

/** Synthetic, transient fixtures. Never access native handles or platform identity/storage. */
class DemoConversations : ConversationSource {
    override fun initialSnapshot(): List<ConversationUi> = listOf(
        ConversationUi("demo-dm-sammy", "Sammy", ChatKind.Direct, true, "Check this out!", "10:37", 2,
            messages = listOf(
                MessageUi("s1", "demo-sammy", "Sammy", "Hey! Did you get the new game working?", false, "10:21"),
                MessageUi("s2", "demo-local", "You", "Yeah, it’s working great now!", true, "10:23", status = MessageStatus.Read),
                MessageUi("s3", "demo-sammy", "Sammy", "Nice! 日本語も使えるね 👋\nhttps://example.com", false, "10:25"),
                MessageUi("s4", "demo-local", "You", "I’ll send you a screenshot later.", true, "10:26", status = MessageStatus.Failed),
                MessageUi("s4-sent", "demo-local", "You", "Still here!", true, "10:30", status = MessageStatus.Sent),
                MessageUi("s5", "demo-sammy", "Sammy", "Check this out!", false, "10:37")
            )),
        ConversationUi("demo-dm-alex", "Alex", ChatKind.Direct, false, "See you tomorrow", "Yesterday",
            messages = listOf(MessageUi("a1", "demo-alex", "Alex", "See you tomorrow", false, "18:05", "Yesterday"))),
        ConversationUi("demo-dm-long", "Taylor · a really long contact name for layout testing", ChatKind.Direct,
            false, "A longer message, without a narrow desktop sidebar", "Mon", messages = listOf(
                MessageUi("t1", "demo-taylor", "Taylor", "Long messages should wrap comfortably on a phone. ".repeat(18), false, "14:12")
            )),
        ConversationUi("demo-group-gaming", "Gaming Group", ChatKind.Group, true, "Sammy: Ready when you are", "10:40", 3,
            members = listOf(MemberUi("demo-local", "You", true, "Owner"), MemberUi("demo-sammy", "Sammy", true),
                MemberUi("demo-alex", "Alex", false)),
            messages = listOf(
                MessageUi("g1", "demo-sammy", "Sammy", "Ready when you are", false, "10:32"),
                MessageUi("g2", "demo-alex", "Alex", "I’ll be there soon 👾", false, "10:33"),
                MessageUi("g3", "demo-local", "You", "Let’s meet in General Voice later.", true, "10:40", status = MessageStatus.Delivered)
            ),
            voiceRooms = listOf(VoiceRoomUi("general", "General Voice", listOf("Sammy")), VoiceRoomUi("quiet", "Quiet Room", emptyList())),
            shareRooms = listOf(ShareRoomUi("build", "Sammy’s Build Share", "Sammy", 1))),
        ConversationUi("demo-group-work", "Work Crew", ChatKind.Group, true, "Design notes for tomorrow", "Tue",
            members = listOf(MemberUi("demo-local", "You", true), MemberUi("demo-riley", "Riley", true, "Owner")),
            voiceRooms = listOf(VoiceRoomUi("preview-joined", "Planning · joined-state preview", listOf("You"), joined = true)))
    )
}
