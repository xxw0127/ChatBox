package com.chatbox.mobile;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import android.util.Log;

import java.nio.charset.StandardCharsets;
import java.security.Key;
import java.security.KeyStore;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** AES-GCM secrets backed by AndroidKeyStore; ciphertext lives in prefs. */
public final class ChatBoxKeystore {
    private static final String TAG = "ChatBoxKeystore";
    private static final String KEYSTORE = "AndroidKeyStore";
    private static final String PREFS = "chatbox_secure";
    private static final String KEY_PREFIX = "chatbox_aes_";
    private static final String CIPHER = "AES/GCM/NoPadding";
    private static final int GCM_TAG_BITS = 128;

    private ChatBoxKeystore() {}

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    private static SecretKey getOrCreateKey(String alias) throws Exception {
        final String keyAlias = KEY_PREFIX + alias;
        KeyStore keyStore = KeyStore.getInstance(KEYSTORE);
        keyStore.load(null);
        if (keyStore.containsAlias(keyAlias)) {
            Key key = keyStore.getKey(keyAlias, null);
            if (key instanceof SecretKey) {
                return (SecretKey) key;
            }
        }
        KeyGenerator generator =
                KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE);
        generator.init(new KeyGenParameterSpec.Builder(keyAlias,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build());
        return generator.generateKey();
    }

    public static void putSecret(Context context, String alias, String value) {
        if (value == null) {
            clearSecret(context, alias);
            return;
        }
        try {
            SecretKey key = getOrCreateKey(alias);
            Cipher cipher = Cipher.getInstance(CIPHER);
            cipher.init(Cipher.ENCRYPT_MODE, key);
            byte[] encrypted =
                    cipher.doFinal(value.getBytes(StandardCharsets.UTF_8));
            prefs(context).edit()
                    .putString(alias + ".iv",
                            Base64.encodeToString(cipher.getIV(), Base64.NO_WRAP))
                    .putString(alias + ".data",
                            Base64.encodeToString(encrypted, Base64.NO_WRAP))
                    .apply();
        } catch (Exception error) {
            Log.e(TAG, "putSecret failed for " + alias, error);
        }
    }

    public static String getSecret(Context context, String alias) {
        SharedPreferences preferences = prefs(context);
        final String ivText = preferences.getString(alias + ".iv", null);
        final String dataText = preferences.getString(alias + ".data", null);
        if (ivText == null || dataText == null) {
            return "";
        }
        try {
            SecretKey key = getOrCreateKey(alias);
            Cipher cipher = Cipher.getInstance(CIPHER);
            cipher.init(Cipher.DECRYPT_MODE, key,
                    new GCMParameterSpec(GCM_TAG_BITS,
                            Base64.decode(ivText, Base64.NO_WRAP)));
            byte[] plain = cipher.doFinal(Base64.decode(dataText, Base64.NO_WRAP));
            return new String(plain, StandardCharsets.UTF_8);
        } catch (Exception error) {
            Log.e(TAG, "getSecret failed for " + alias, error);
            return "";
        }
    }

    public static void clearSecret(Context context, String alias) {
        prefs(context).edit()
                .remove(alias + ".iv")
                .remove(alias + ".data")
                .apply();
        try {
            KeyStore keyStore = KeyStore.getInstance(KEYSTORE);
            keyStore.load(null);
            keyStore.deleteEntry(KEY_PREFIX + alias);
        } catch (Exception error) {
            Log.w(TAG, "clearSecret key delete failed for " + alias, error);
        }
    }
}
