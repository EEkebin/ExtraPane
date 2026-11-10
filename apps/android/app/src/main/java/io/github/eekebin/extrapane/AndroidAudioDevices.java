package io.github.eekebin.extrapane;

import android.content.Context;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.os.Build;

import java.util.*;

final class AndroidAudioDevices {
    static List<AudioRouteCatalog.Entry> choices(Context context, boolean input) {
        List<AudioRouteCatalog.Route> routes = new ArrayList<>();
        for (AudioDeviceInfo device :
                context.getSystemService(AudioManager.class)
                        .getDevices(
                                input
                                        ? AudioManager.GET_DEVICES_INPUTS
                                        : AudioManager.GET_DEVICES_OUTPUTS)) {
            if (input ? !device.isSource() : !device.isSink()) continue;
            CharSequence product = device.getProductName();
            routes.add(
                    new AudioRouteCatalog.Route(
                            device.getId(),
                            kind(device.getType()),
                            product == null ? "" : product.toString(),
                            device.getAddress()));
        }
        return AudioRouteCatalog.entries(routes, input, Build.MODEL);
    }

    static String sharingKey(Context context, int id, boolean input) {
        for (AudioDeviceInfo device :
                context.getSystemService(AudioManager.class)
                        .getDevices(
                                input
                                        ? AudioManager.GET_DEVICES_INPUTS
                                        : AudioManager.GET_DEVICES_OUTPUTS))
            if (device.getId() == id) {
                String address = device.getAddress(),
                        product = String.valueOf(device.getProductName());
                return ResourceSharing.audioKey(input, device.getType(), address, product);
            }
        return (input ? "microphone:" : "speaker:") + "unavailable:" + id;
    }

    private static AudioRouteCatalog.Kind kind(int type) {
        return switch (type) {
            case AudioDeviceInfo.TYPE_BUILTIN_EARPIECE -> AudioRouteCatalog.Kind.EARPIECE;
            case AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> AudioRouteCatalog.Kind.SPEAKER;
            case AudioDeviceInfo.TYPE_BUILTIN_MIC -> AudioRouteCatalog.Kind.BUILTIN_MIC;
            case AudioDeviceInfo.TYPE_WIRED_HEADSET -> AudioRouteCatalog.Kind.WIRED_HEADSET;
            case AudioDeviceInfo.TYPE_WIRED_HEADPHONES -> AudioRouteCatalog.Kind.WIRED_HEADPHONES;
            case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> AudioRouteCatalog.Kind.BLUETOOTH_MEDIA;
            case AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> AudioRouteCatalog.Kind.BLUETOOTH_CALL;
            case AudioDeviceInfo.TYPE_BLE_HEADSET -> AudioRouteCatalog.Kind.BLUETOOTH_LE;
            case AudioDeviceInfo.TYPE_BLE_SPEAKER -> AudioRouteCatalog.Kind.BLUETOOTH_SPEAKER;
            case AudioDeviceInfo.TYPE_BLE_BROADCAST -> AudioRouteCatalog.Kind.BLUETOOTH_BROADCAST;
            case AudioDeviceInfo.TYPE_HEARING_AID -> AudioRouteCatalog.Kind.HEARING_AID;
            case AudioDeviceInfo.TYPE_USB_DEVICE,
                    AudioDeviceInfo.TYPE_USB_ACCESSORY,
                    AudioDeviceInfo.TYPE_USB_HEADSET ->
                    AudioRouteCatalog.Kind.USB;
            case AudioDeviceInfo.TYPE_HDMI,
                    AudioDeviceInfo.TYPE_HDMI_ARC,
                    AudioDeviceInfo.TYPE_HDMI_EARC ->
                    AudioRouteCatalog.Kind.HDMI;
            case AudioDeviceInfo.TYPE_DOCK, AudioDeviceInfo.TYPE_DOCK_ANALOG ->
                    AudioRouteCatalog.Kind.DOCK;
            case AudioDeviceInfo.TYPE_LINE_ANALOG,
                    AudioDeviceInfo.TYPE_LINE_DIGITAL,
                    AudioDeviceInfo.TYPE_AUX_LINE ->
                    AudioRouteCatalog.Kind.LINE;
            default -> AudioRouteCatalog.Kind.INTERNAL;
        };
    }
}
