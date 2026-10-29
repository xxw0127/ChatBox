package com.chatbox.mobile;

import android.content.Intent;
import android.os.Bundle;

import org.qtproject.qt.android.bindings.QtActivity;

/** QtActivity that remembers a conversation requested by a notification tap. */
public class ChatBoxActivity extends QtActivity {
    private static int pendingKind = 0;
    private static long pendingTargetId = 0;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        capture(getIntent());
    }

    @Override
    public void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        capture(intent);
    }

    private static void capture(Intent intent) {
        if (intent == null) {
            return;
        }
        final int kind = intent.getIntExtra("chatbox_kind", 0);
        final long target = intent.getLongExtra("chatbox_target_id", 0L);
        if ((kind == 1 || kind == 2) && target != 0L) {
            pendingKind = kind;
            pendingTargetId = target;
        }
    }

    public static int consumePendingKind() {
        final int kind = pendingKind;
        pendingKind = 0;
        return kind;
    }

    public static long consumePendingTargetId() {
        final long target = pendingTargetId;
        pendingTargetId = 0L;
        return target;
    }
}
