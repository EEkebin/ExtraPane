package io.github.eekebin.extrapane;

import android.view.KeyEvent;

/** Fallback for Android keyboard drivers that omit the Linux scan code. */
final class KeyboardMapping {
    static int evdev(int key){
        if(key>=KeyEvent.KEYCODE_A && key<=KeyEvent.KEYCODE_Z)return new int[]{30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44}[key-KeyEvent.KEYCODE_A];
        if(key>=KeyEvent.KEYCODE_1 && key<=KeyEvent.KEYCODE_9)return key-KeyEvent.KEYCODE_1+2;
        if(key>=KeyEvent.KEYCODE_F1 && key<=KeyEvent.KEYCODE_F10)return key-KeyEvent.KEYCODE_F1+59;
        if(key>=KeyEvent.KEYCODE_NUMPAD_0 && key<=KeyEvent.KEYCODE_NUMPAD_9)return new int[]{82,79,80,81,75,76,77,71,72,73}[key-KeyEvent.KEYCODE_NUMPAD_0];
        switch(key){
            case KeyEvent.KEYCODE_0:return 11;case KeyEvent.KEYCODE_ESCAPE:return 1;
            case KeyEvent.KEYCODE_MINUS:return 12;case KeyEvent.KEYCODE_EQUALS:return 13;
            case KeyEvent.KEYCODE_DEL:return 14;case KeyEvent.KEYCODE_TAB:return 15;
            case KeyEvent.KEYCODE_LEFT_BRACKET:return 26;case KeyEvent.KEYCODE_RIGHT_BRACKET:return 27;
            case KeyEvent.KEYCODE_ENTER:return 28;case KeyEvent.KEYCODE_CTRL_LEFT:return 29;
            case KeyEvent.KEYCODE_SEMICOLON:return 39;case KeyEvent.KEYCODE_APOSTROPHE:return 40;
            case KeyEvent.KEYCODE_GRAVE:return 41;case KeyEvent.KEYCODE_SHIFT_LEFT:return 42;
            case KeyEvent.KEYCODE_BACKSLASH:return 43;case KeyEvent.KEYCODE_COMMA:return 51;
            case KeyEvent.KEYCODE_PERIOD:return 52;case KeyEvent.KEYCODE_SLASH:return 53;
            case KeyEvent.KEYCODE_SHIFT_RIGHT:return 54;case KeyEvent.KEYCODE_NUMPAD_MULTIPLY:return 55;
            case KeyEvent.KEYCODE_ALT_LEFT:return 56;case KeyEvent.KEYCODE_SPACE:return 57;
            case KeyEvent.KEYCODE_CAPS_LOCK:return 58;case KeyEvent.KEYCODE_NUM_LOCK:return 69;
            case KeyEvent.KEYCODE_SCROLL_LOCK:return 70;case KeyEvent.KEYCODE_NUMPAD_SUBTRACT:return 74;
            case KeyEvent.KEYCODE_NUMPAD_ADD:return 78;case KeyEvent.KEYCODE_NUMPAD_DOT:return 83;
            case KeyEvent.KEYCODE_F11:return 87;case KeyEvent.KEYCODE_F12:return 88;
            case KeyEvent.KEYCODE_NUMPAD_ENTER:return 96;case KeyEvent.KEYCODE_CTRL_RIGHT:return 97;
            case KeyEvent.KEYCODE_NUMPAD_DIVIDE:return 98;case KeyEvent.KEYCODE_ALT_RIGHT:return 100;
            case KeyEvent.KEYCODE_MOVE_HOME:return 102;case KeyEvent.KEYCODE_DPAD_UP:return 103;
            case KeyEvent.KEYCODE_PAGE_UP:return 104;case KeyEvent.KEYCODE_DPAD_LEFT:return 105;
            case KeyEvent.KEYCODE_DPAD_RIGHT:return 106;case KeyEvent.KEYCODE_MOVE_END:return 107;
            case KeyEvent.KEYCODE_DPAD_DOWN:return 108;case KeyEvent.KEYCODE_PAGE_DOWN:return 109;
            case KeyEvent.KEYCODE_INSERT:return 110;case KeyEvent.KEYCODE_FORWARD_DEL:return 111;
            case KeyEvent.KEYCODE_META_LEFT:return 125;case KeyEvent.KEYCODE_META_RIGHT:return 126;
            case KeyEvent.KEYCODE_MENU:return 127;default:return 0;
        }
    }
    private KeyboardMapping(){}
}
