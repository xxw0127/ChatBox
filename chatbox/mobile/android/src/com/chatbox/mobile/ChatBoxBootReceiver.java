package com.chatbox.mobile;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class ChatBoxBootReceiver extends BroadcastReceiver {
    private static final String TAG = "ChatBoxBoot";

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            return;
        }
        try {
            ChatBoxService.start(context);
        } catch (Exception error) {
            // Android 15 may reject foreground-service starts from BOOT_COMPLETED.
            Log.w(TAG, "cannot start ChatBoxService at boot", error);
        }
    }
}
