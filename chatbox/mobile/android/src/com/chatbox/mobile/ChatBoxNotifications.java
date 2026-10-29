package com.chatbox.mobile;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;

import org.qtproject.qt.android.bindings.QtActivity;

public final class ChatBoxNotifications {
    public static final String CHANNEL_SERVICE = "chatbox_service";
    public static final String CHANNEL_MESSAGES = "chatbox_messages";
    private static final int FOREGROUND_ID = 1000;
    private static final int TEST_ID = 1001;

    private ChatBoxNotifications() {}

    public static void createChannels(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return;
        }
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager == null) {
            return;
        }
        NotificationChannel serviceChannel = new NotificationChannel(
                CHANNEL_SERVICE, "ChatBox 连接", NotificationManager.IMPORTANCE_LOW);
        serviceChannel.setDescription("保持 ChatBox 在线");
        manager.createNotificationChannel(serviceChannel);

        NotificationChannel messageChannel = new NotificationChannel(
                CHANNEL_MESSAGES, "ChatBox 消息", NotificationManager.IMPORTANCE_HIGH);
        messageChannel.setDescription("新消息提醒");
        manager.createNotificationChannel(messageChannel);
    }

    public static void startForeground(Service service) {
        Notification notification = buildForeground(service);
        if (Build.VERSION.SDK_INT >= 34) {
            service.startForeground(FOREGROUND_ID, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_REMOTE_MESSAGING);
        } else {
            service.startForeground(FOREGROUND_ID, notification);
        }
    }

    public static void stopForeground(Service service) {
        service.stopForeground(true);
    }

    public static void notifyTest(Context context) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager == null) {
            return;
        }
        Notification notification = new Notification.Builder(context, CHANNEL_MESSAGES)
                .setSmallIcon(android.R.drawable.stat_notify_chat)
                .setContentTitle("ChatBox")
                .setContentText("F0 通知测试")
                .setAutoCancel(true)
                .setContentIntent(openApp(context))
                .build();
        manager.notify(TEST_ID, notification);
    }

    public static void postMessage(Context context, int id, String title, String body,
                                   int kind, long targetId) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager == null) {
            return;
        }
        Intent intent = new Intent(context, QtActivity.class);
        intent.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        intent.putExtra("chatbox_kind", kind);
        intent.putExtra("chatbox_target_id", targetId);
        PendingIntent pending = PendingIntent.getActivity(context, id, intent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Notification notification = new Notification.Builder(context, CHANNEL_MESSAGES)
                .setSmallIcon(android.R.drawable.stat_notify_chat)
                .setContentTitle(title)
                .setContentText(body)
                .setAutoCancel(true)
                .setContentIntent(pending)
                .build();
        manager.notify(id, notification);
    }

    public static void cancel(Context context, int id) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.cancel(id);
        }
    }

    public static void setForegroundState(Context context, String text) {
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.notify(FOREGROUND_ID, buildForeground(context, text));
        }
    }

    private static Notification buildForeground(Context context) {
        return buildForeground(context, "正在保持连接");
    }

    private static Notification buildForeground(Context context, String text) {
        Intent intent = new Intent(context, QtActivity.class);
        intent.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent pending = PendingIntent.getActivity(context, 0, intent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(context, CHANNEL_SERVICE)
                .setSmallIcon(android.R.drawable.stat_notify_chat)
                .setContentTitle("ChatBox")
                .setContentText(text)
                .setOngoing(true)
                .setContentIntent(pending)
                .build();
    }

    private static PendingIntent openApp(Context context) {
        Intent intent = new Intent(context, QtActivity.class);
        intent.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        return PendingIntent.getActivity(context, 0, intent,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }
}
