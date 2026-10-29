package com.chatbox.mobile;

import android.content.Context;
import android.content.Intent;
import android.os.Build;

import org.qtproject.qt.android.bindings.QtService;

public class ChatBoxService extends QtService {
    @Override
    public void onCreate() {
        super.onCreate();
        ChatBoxNotifications.createChannels(this);
        ChatBoxNotifications.startForeground(this);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        super.onStartCommand(intent, flags, startId);
        return START_STICKY;
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        // Keep the foreground service alive after the task is swiped away.
        super.onTaskRemoved(rootIntent);
    }

    @Override
    public void onDestroy() {
        ChatBoxNotifications.stopForeground(this);
        super.onDestroy();
    }

    public static void start(Context context) {
        Intent intent = new Intent(context, ChatBoxService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }
}
