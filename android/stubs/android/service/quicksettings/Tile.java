package android.service.quicksettings;

import android.graphics.drawable.Icon;

/** Compile-time stub of the framework class (API 24; getSubtitle / setSubtitle API 29). NOT packaged: the real class of the phone is used at run time. Signatures as in the Android SDK. */
public final class Tile {
    public static final int STATE_UNAVAILABLE = 0, STATE_INACTIVE = 1, STATE_ACTIVE = 2;
    public int getState() { return 0; }
    public void setState(int state) {}
    public Icon getIcon() { return null; }
    public void setIcon(Icon icon) {}
    public CharSequence getLabel() { return null; }
    public void setLabel(CharSequence label) {}
    public CharSequence getSubtitle() { return null; }
    public void setSubtitle(CharSequence subtitle) {}
    public void updateTile() {}
}
