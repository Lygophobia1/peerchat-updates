package com.peerchat.android.ui

import com.peerchat.android.platform.ServerRejected
import com.peerchat.android.platform.tls.ServerChannel

internal fun accountFailureText(failure:Exception,creating:Boolean):String {
    if(ServerChannel.identityChange(failure)!=null)return "Server identity mismatch. Connection rejected; verify trusted server settings."
    if(failure is com.peerchat.android.application.MessagingStartupFailure){
        val cause=failure.cause
        if(cause is ServerRejected&&cause.code in setOf("device_already_registered","device_account_mismatch"))return accountFailureText(cause,creating)
        return "Authentication succeeded, but messaging startup failed. Retry the connection; existing identity and history were preserved."
    }
    if(failure is ServerRejected)return when(failure.code){
        // Server deliberately does not distinguish nonexistent accounts from wrong passwords.
        "login_failed","bad_credentials"->"Username or password was not accepted. Check both, or choose Create Account if you need a new account."
        "account_not_found"->"Account not found. Check the username or choose Create Account."
        "device_already_registered","device_account_mismatch"->"This device identity belongs to another account. Sign in to that account; creating an account cannot move the identity."
        "registration_failed"->"Account creation was rejected. The username may already be in use."
        "invalid_registration"->"Choose a valid username and password."
        "session_invalid","session_superseded"->"The authenticated session is no longer valid. Sign in again."
        else->"The server rejected the request. No device identity or trust settings were changed."
    }
    if(failure is java.io.IOException)return "Server unavailable. Check the connection and try again; saved trust is unchanged."
    return if(creating)"Account creation or messaging startup failed. Existing identity and history were preserved."
        else "Sign-in or messaging startup failed. Existing identity and history were preserved."
}
